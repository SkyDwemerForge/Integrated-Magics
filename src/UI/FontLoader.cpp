#include "UI/FontLoader.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "Config/StyleConfig.h"
#include "PCH.h"

namespace FontLoader {

    namespace {
        using Clock = std::chrono::steady_clock;

        // A settled size is rebuilt once, so a burst of clicks on the size control costs one rebuild (spec 011, R2).
        constexpr float kFontRebuildSettleSec = 0.25f;
        // D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION.
        constexpr int kMaxAtlasSide = 16384;
        // ImGui's built-in font (ProggyClean) size, used when the font file is missing.
        constexpr float kDefaultFontSizePx = 13.f;

        // The font file, read once at init: rebuilds add fonts from memory, with no file I/O (R3).
        std::vector<unsigned char> g_fontData;
        float g_baseSizePx = kDefaultFontSizePx;
        // The name font size in the current atlas, 0 = no name font. A size that failed the guard stays here too, so it
        // is not retried until the size changes.
        float g_builtNamePx = 0.f;
        float g_pendingNamePx = -1.f;
        Clock::time_point g_pendingSince{};
        ImFont* g_nameFont = nullptr;

        float WantedNamePx() {
            const auto& st = IntegratedMagic::StyleConfig::Get();
            if (!st.showSpellNamesInHud || std::abs(st.spellNameScale - 1.f) < 0.001f) return 0.f;
            return std::round(g_baseSizePx * st.spellNameScale * 2.f) / 2.f;
        }

        // The base font family at `px`: the main font, then one merged font per character set turned on. Returns the
        // main font.
        ImFont* AddFamily(float px) {
            auto& io = ImGui::GetIO();
            if (g_fontData.empty()) {
                ImFontConfig cfg;
                cfg.SizePixels = px;
                return io.Fonts->AddFontDefault(&cfg);
            }

            const auto& fc = IntegratedMagic::StyleConfig::Get().font;
            const int dataSize = static_cast<int>(g_fontData.size());
            void* data = g_fontData.data();

            ImFontConfig mainCfg;
            mainCfg.FontDataOwnedByAtlas = false;
            ImFont* font = io.Fonts->AddFontFromMemoryTTF(data, dataSize, px, &mainCfg, GetGlyphRangesDefault());

            ImFontConfig mergeCfg;
            mergeCfg.MergeMode = true;
            mergeCfg.FontDataOwnedByAtlas = false;
            const auto merge = [&](bool on, const ImWchar* ranges) {
                if (on) io.Fonts->AddFontFromMemoryTTF(data, dataSize, px, &mergeCfg, ranges);
            };
            merge(fc.rangePolish, GetGlyphRangesPolish());
            merge(fc.rangeCyrillic, GetGlyphRangesCyrillic());
            merge(fc.rangeJapanese, GetGlyphRangesJapanese());
            merge(fc.rangeChineseSimplified, GetGlyphRangesChineseSimplified());
            merge(fc.rangeKorean, GetGlyphRangesKorean());
            merge(fc.rangeGreek, GetGlyphRangesGreek());
            return font;
        }

        bool AtlasFits() {
            const auto& atlas = *ImGui::GetIO().Fonts;
            return atlas.TexWidth <= kMaxAtlasSide && atlas.TexHeight <= kMaxAtlasSide;
        }

        // Rebuilds the whole atlas. The base font is always added first, with the same parameters, so it stays the
        // default font and every other text keeps its look. The next ImGui_ImplDX11_NewFrame uploads the atlas.
        void BuildAtlas(float namePx) {
            auto& io = ImGui::GetIO();
            [[maybe_unused]] const auto start = Clock::now();

            io.Fonts->Clear();
            AddFamily(g_baseSizePx);
            g_nameFont = namePx > 0.f ? AddFamily(namePx) : nullptr;
            const bool built = io.Fonts->Build();

            if (namePx > 0.f && (!built || !AtlasFits())) {
                if (!built)
                    spdlog::warn("[HUD] FontLoader: atlas build failed (names={} px), names use the base font", namePx);
                else
                    spdlog::warn("[HUD] FontLoader: spell name font too large ({}x{}), names use the base font",
                                 io.Fonts->TexWidth, io.Fonts->TexHeight);
                io.Fonts->Clear();
                AddFamily(g_baseSizePx);
                g_nameFont = nullptr;
                io.Fonts->Build();
            }
            g_builtNamePx = namePx;

            ImGui_ImplDX11_InvalidateDeviceObjects();

            [[maybe_unused]] const auto ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
            [[maybe_unused]] const std::string names = g_nameFont ? std::format("{} px", namePx) : std::string("none");
            MAGIC_DEBUG_LOG("[HUD] FontLoader: atlas built (base={} px, names={}, scale={}) {}x{} in {} ms",
                            g_baseSizePx, names, IntegratedMagic::StyleConfig::Get().spellNameScale, io.Fonts->TexWidth,
                            io.Fonts->TexHeight, ms);
        }
    }

    void LoadFontsFromConfig() {
        const auto& fc = IntegratedMagic::StyleConfig::Get().font;

        g_fontData.clear();
        std::error_code ec;
        if (!fc.path.empty() && std::filesystem::exists(fc.path, ec)) {
            std::ifstream file(fc.path, std::ios::binary);
            g_fontData.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }

        if (g_fontData.empty()) {
            g_baseSizePx = kDefaultFontSizePx;
            MAGIC_DEBUG_LOG("[HUD] FontLoader::LoadFontsFromConfig: font not found, using default");
        } else {
            g_baseSizePx = fc.size;
            MAGIC_DEBUG_LOG("[HUD] FontLoader::LoadFontsFromConfig: loaded font '{}' size {}", fc.path, fc.size);
        }

        // A saved size is built now, with no settle time, so it shows from the first frame.
        BuildAtlas(WantedNamePx());
        g_pendingNamePx = -1.f;
    }

    void RebuildIfNeeded() {
        if (!ImGui::GetCurrentContext()) return;

        const float wanted = WantedNamePx();
        if (wanted == g_builtNamePx) {
            g_pendingNamePx = -1.f;
            return;
        }

        const auto now = Clock::now();
        if (wanted != g_pendingNamePx) {
            g_pendingNamePx = wanted;
            g_pendingSince = now;
            return;
        }

        if (std::chrono::duration<float>(now - g_pendingSince).count() < kFontRebuildSettleSec) return;

        BuildAtlas(wanted);
        g_pendingNamePx = -1.f;
    }

    ImFont* SpellNameFont() { return g_nameFont; }

}
