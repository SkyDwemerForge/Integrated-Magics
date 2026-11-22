#pragma once

#include <imgui.h>

#include <sstream>
#include <string>
#include <vector>

#include "Config/StyleConfig.h"
#include "UI/FontLoader.h"

namespace IntegratedMagic::HUD {

    inline void DrawSpellLabel(const char* text, ImVec2 slotCenter, float slotRadius, ImVec2 dirTowardCenter,
                               ButtonLabelCorner position, float padding) {
        if (!text || text[0] == '\0') return;
        const auto& st = StyleConfig::Get();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (!dl) return;

        // Spell names use the spell name font (spec 011, R4). Unconditional pair: no return until the PopFont below.
        ImGui::PushFont(FontLoader::SpellNameFont());

        ImVec2 dir{0.f, 1.f};
        switch (position) {
            case ButtonLabelCorner::Top:
                dir = {0.f, -1.f};
                break;
            case ButtonLabelCorner::Bottom:
                dir = {0.f, 1.f};
                break;
            case ButtonLabelCorner::Left:
                dir = {-1.f, 0.f};
                break;
            case ButtonLabelCorner::Right:
                dir = {1.f, 0.f};
                break;
            case ButtonLabelCorner::TowardCenter:
                dir = dirTowardCenter;
                break;
            case ButtonLabelCorner::AwayFromCenter:
                dir = {-dirTowardCenter.x, -dirTowardCenter.y};
                break;
        }

        const float maxW = slotRadius * 2.f;
        const float lineH = ImGui::GetTextLineHeight();

        std::vector<std::string> lines;
        {
            const char* wordStart = text;
            const char* cur = text;
            std::string currentLine;

            auto flushWord = [&](const char* end) {
                std::string word(wordStart, end);
                if (word.empty()) return;
                std::string candidate = currentLine.empty() ? word : currentLine + " " + word;
                if (ImGui::CalcTextSize(candidate.c_str()).x <= maxW) {
                    currentLine = std::move(candidate);
                } else {
                    if (!currentLine.empty()) lines.push_back(currentLine);

                    currentLine = std::move(word);
                }
            };

            for (; *cur; ++cur) {
                if (*cur == ' ') {
                    flushWord(cur);
                    wordStart = cur + 1;
                }
            }
            flushWord(cur);
            if (!currentLine.empty()) lines.push_back(currentLine);
        }

        if (!lines.empty()) {
            const float totalH = lineH * static_cast<float>(lines.size());

            const float anchorX = slotCenter.x + dir.x * (slotRadius + padding);
            const float anchorY = slotCenter.y + dir.y * (slotRadius + padding);

            float blockY = anchorY - totalH * 0.5f;
            if (dir.y < -0.1f)
                blockY = anchorY - totalH;
            else if (dir.y > 0.1f)
                blockY = anchorY;

            // The window never cuts a name off: its reserve does not follow the number of wrapped lines (spec 011, R5).
            dl->PushClipRectFullScreen();
            for (const auto& line : lines) {
                const ImVec2 ts = ImGui::CalcTextSize(line.c_str());

                float x = anchorX - ts.x * 0.5f;
                if (dir.x < -0.1f)
                    x = anchorX - ts.x;
                else if (dir.x > 0.1f)
                    x = anchorX;

                if (st.textShadowEnabled)
                    dl->AddText({x + st.textShadowOffsetX, blockY + st.textShadowOffsetY}, st.textShadowColor,
                                line.c_str());
                dl->AddText({x, blockY}, st.textColor, line.c_str());

                blockY += lineH;
            }
            dl->PopClipRect();
        }

        ImGui::PopFont();
    }
}