#include "Adapters/Outbound/SyntheticInput.h"

#include <mutex>
#include <vector>

#include "PCH.h"
#include "Shared/Hand.h"

namespace IntegratedMagic::detail {
    // Game strings are built on first use, never at DLL load. A BSFixedString built during static initialization
    // resolves a game address before CommonLib's REL::Module instance is constructed, and in Debug builds that
    // construction then wipes the module base, so every later hook address is wrong (spec 010, research R7).
    const RE::BSFixedString& RightAttackEvent() {
        static const RE::BSFixedString name{"Right Attack/Block"};
        return name;
    }

    const RE::BSFixedString& LeftAttackEvent() {
        static const RE::BSFixedString name{"Left Attack/Block"};
        return name;
    }

    namespace {
        const RE::BSFixedString& ShoutUserEvent() {
            static const RE::BSFixedString name{"Shout"};
            return name;
        }
    }

    enum class DirectKind : std::uint8_t { AttackLeft, AttackRight, Shout };

    struct DirectAction {
        DirectKind kind;
        float value;
        float heldSecs;
    };

    struct DirectState {
        std::mutex mutex;
        std::vector<DirectAction> pending;
    };

    static DirectState& GetDirect() {
        static DirectState s;
        return s;
    }

    static void FireAttackDirect(Hand hand, float value, float heldSecs) {
        const bool left = (hand == Hand::Left);
        // A dropped press or release is logged, a held repeat is not (spec 007, contracts/diagnostic-log.md §3).
        [[maybe_unused]] const bool logDrop = (value == 0.0f) || (heldSecs <= 0.0f);

        auto* pc = RE::PlayerControls::GetSingleton();
        if (!pc || !pc->attackBlockHandler) {
            if (logDrop)
                MAGIC_DEBUG_LOG("[Action] synthetic attack dropped: hand={} value={:.1f} reason=no handler",
                                left ? "L" : "R", value);
            return;
        }
        if (!pc->attackBlockHandler->IsInputEventHandlingEnabled()) {
            if (logDrop)
                MAGIC_DEBUG_LOG("[Action] synthetic attack dropped: hand={} value={:.1f} reason=disabled",
                                left ? "L" : "R", value);
            return;
        }

        const auto& ue = left ? LeftAttackEvent() : RightAttackEvent();
        const auto id = left ? kLeftAttackMouseId : kRightAttackMouseId;

        auto* ev = RE::ButtonEvent::Create(RE::INPUT_DEVICE::kMouse, ue, id, value, heldSecs);
        if (!ev) return;
        ev->next = nullptr;

        if (!pc->attackBlockHandler->CanProcess(ev)) {
            if (logDrop)
                MAGIC_DEBUG_LOG("[Action] synthetic attack dropped: hand={} value={:.1f} reason=cannot process",
                                left ? "L" : "R", value);
            return;
        }
        pc->attackBlockHandler->ProcessButton(ev, &pc->data);
    }

    static void FireShoutDirect(float value, float heldSecs) {
        auto* pc = RE::PlayerControls::GetSingleton();
        if (!pc || !pc->shoutHandler) return;
        if (!pc->shoutHandler->IsInputEventHandlingEnabled()) {
            MAGIC_DEBUG_LOG("[SyntheticInput] FireShoutDirect: blocked - IsInputEventHandlingEnabled=false (value={:.2f})", value);
            return;
        }

        auto* ev = RE::ButtonEvent::Create(RE::INPUT_DEVICE::kKeyboard, ShoutUserEvent(), 0, value, heldSecs);
        if (!ev) return;
        ev->next = nullptr;

        const bool canProcess = pc->shoutHandler->CanProcess(ev);
        if (value == 0.0f) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            [[maybe_unused]] const RE::FormID selPower = (player && player->GetActorRuntimeData().selectedPower)
                                                             ? player->GetActorRuntimeData().selectedPower->GetFormID()
                                                             : 0u;
            MAGIC_DEBUG_LOG("[SyntheticInput] FireShoutDirect: stopShout held={:.3f} canProcess={} selectedPower={:#010x}",
                            heldSecs, canProcess, selPower);
        }
        if (canProcess) pc->shoutHandler->ProcessButton(ev, &pc->data);
    }

    void DispatchAttack(Hand hand, float value, float heldSecs) {
        auto& st = GetDirect();
        std::scoped_lock lk(st.mutex);
        st.pending.push_back({hand == Hand::Left ? DirectKind::AttackLeft : DirectKind::AttackRight, value, heldSecs});
    }

    void DispatchShout(float value, float heldSecs) {
        auto& st = GetDirect();
        std::scoped_lock lk(st.mutex);
        st.pending.push_back({DirectKind::Shout, value, heldSecs});
    }

    struct StreamState {
        std::mutex mutex;
        std::vector<RE::ButtonEvent*> pending;
    };

    static StreamState& GetStream() {
        static StreamState s;
        return s;
    }

    void EnqueueRetainedEvent(RE::INPUT_DEVICE dev, std::uint32_t idCode, const RE::BSFixedString& userEvent,
                              float value, float heldSecs) {
        auto* ev = RE::ButtonEvent::Create(dev, userEvent, idCode, value, heldSecs);
        if (!ev) return;
        auto& st = GetStream();
        std::scoped_lock lk(st.mutex);
        st.pending.push_back(ev);
    }

    static RE::InputEvent* DrainStream(RE::InputEvent* head) {
        std::vector<RE::ButtonEvent*> local;
        {
            auto& st = GetStream();
            std::scoped_lock lk(st.mutex);
            local.swap(st.pending);
        }
        if (local.empty()) return head;

        RE::InputEvent* synthHead = nullptr;
        RE::InputEvent* synthTail = nullptr;
        for (auto* ev : local) {
            if (!ev) continue;
            ev->next = nullptr;
            if (!synthHead)
                synthHead = synthTail = ev;
            else {
                synthTail->next = ev;
                synthTail = ev;
            }
        }
        if (!synthHead) return head;
        if (head) synthTail->next = head;
        return synthHead;
    }

    RE::InputEvent* FlushSyntheticInput(RE::InputEvent* head) {
        std::vector<DirectAction> direct;
        {
            auto& st = GetDirect();
            std::scoped_lock lk(st.mutex);
            direct.swap(st.pending);
        }
        for (const auto& a : direct) {
            switch (a.kind) {
                using enum DirectKind;
                case AttackLeft:
                    FireAttackDirect(Hand::Left, a.value, a.heldSecs);
                    break;
                case AttackRight:
                    FireAttackDirect(Hand::Right, a.value, a.heldSecs);
                    break;
                case Shout:
                    FireShoutDirect(a.value, a.heldSecs);
                    break;
            }
        }

        return DrainStream(head);
    }
}