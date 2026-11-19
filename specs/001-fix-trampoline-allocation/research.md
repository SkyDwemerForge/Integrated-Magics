# Phase 0 Research: Fix startup crash from hook-space (trampoline) exhaustion

All findings below were verified against the checked-out sources (`extern/CommonLibVR-ng` at v16.9.0, compared
with v10.1.0 `39f9d07a6` and the pre-`8aaf4bb` pointer `e0e0c867b`) and the project history. No NEEDS
CLARIFICATION items remain.

---

## R1. Root cause

**Decision**: the crash is hook-space exhaustion when the second call-site hook installs.

**Evidence**:
- `src/SKSE/Trampoline.cpp:164` is `Trampoline::do_allocate`: `if (a_size > free_size()) report_and_fail("Failed to
  handle allocation request")`.
- `extern/Detours/HookUtil.hpp` calls `SKSE::AllocTrampoline(14)` inside every `write_call` overload.
- CommonLib `src/SKSE/API.cpp` (v10.1.0 and v16.9.0): `AllocTrampoline` sets `info.trampolineSize` and calls
  `InitTrampoline()`, whose body runs inside a function-local `std::call_once`. **Only the first request allocates.**
  At `e0e0c867b` (Dec 2025), `AllocTrampoline` called `set_trampoline`/`create` on every call, so each hook got a
  fresh block. The change came in upstream `25b34f485` / `e51effcdc` (2026-08-21).
- `Install_Hooks()` order: `PollInputDevicesHook` (call-site, takes the only 14 bytes) → `PlayerAnimGraphHook`
  (vtable, 0 bytes) → `D3DInitHook` (call-site, 0 bytes free → terminate).
- Project commit `6188de8` (#64) removed the upfront `SKSE::AllocTrampoline(64)` from `Hooks.cpp` while adding the
  D3D init and present hooks.

**Alternatives considered**: a layout or ABI break from the v16.9.0 update. Rejected: the message and line match
`do_allocate` exactly, and the vtable and trampoline code paths the plugin uses did not change between v10.1.0 and
v16.9.0 (only the order of the patch write inside `write_5branch`/`write_6branch` changed).

---

## R2. How to reserve hook space

**Decision**: reserve once through `SKSE::Init(skse, { .trampoline = true, .trampolineSize =
IntegratedMagic::Hooks::kHookSpaceBytes })` in `SKSEPluginLoad`, and remove every `SKSE::AllocTrampoline` call from
`HookUtil.hpp`.

**Rationale**:
- `SKSE::AllocTrampoline` is `[[deprecated("Use SKSE::Init(..., {}) instead")]]` in v16.9.0 (`include/SKSE/API.h`).
- `InitInfo { bool trampoline{false}; std::size_t trampolineSize{0}; }` is the supported path. `Init` queries
  SKSE's `TrampolineInterface` first and then calls `InitTrampoline()`, which tries SKSE's shared branch pool
  (`AllocateFromBranchPool`) and falls back to `Trampoline::create` near the game module when the pool is exhausted.
- One reservation, made before any hook installs, does not depend on how many times the library honors a request
  (spec FR-002). The size constant lives in `include/Hooks.h`, next to the hook list it has to cover.
- `.log` keeps its default (`true`), the same as today's `SKSE::Init(skse)`, so logging behavior is unchanged.

**Alternatives considered**:
- *Keep a single `SKSE::AllocTrampoline(N)` at the top of `Install_Hooks()`*: works today, but uses a deprecated
  API that may be removed in a later library update.
- *Own `SKSE::Trampoline` instance per hook*: more memory regions and diverges from the library's design; no benefit.
- *Detours for every hook*: larger change, different failure modes, and it does not address the reservation policy.

---

## R3. Reservation size

**Decision**: `kHookSpaceBytes = 128`.

**Rationale**:
- Consumption per hook kind (from `Trampoline.cpp`): `write_call<5>` / `write_branch<5>` allocate a
  `TrampolineAssembly` = **14 bytes** (`static_assert(sizeof(TrampolineAssembly) == 0xE)`). `write_call<6>` /
  `write_branch<6>` allocate a `std::uintptr_t` = **8 bytes**. vtable and WndProc hooks consume **0**.
- Current need: 3 × `write_call<5>` = **42 bytes** → 33% of 128, well within spec SC-003 (≤ 75%).
- Leaves room for about 4 more 14-byte hooks before reaching 75%. The memory cost is negligible.
- A `static_assert` in `Hooks.h` ties the size to a hand-maintained count of call-site hooks.

**Alternatives considered**: exactly 42 bytes (no margin; the next hook breaks it); 64 bytes (66%, one more hook
exceeds 75%); 256+ bytes (no benefit; wastes the shared SKSE pool other plugins use).

---

## R4. SKSE trampoline interface unavailable

**Decision**: after `SKSE::Init`, at the start of `Install_Hooks()`, if `SKSE::GetTrampoline().empty()`, log a
warning and call `SKSE::GetTrampoline().create(kHookSpaceBytes)`.

**Rationale**: `InitTrampoline()` only allocates inside `if (const auto intfc = GetTrampolineInterface())`. With no
interface it allocates nothing and reports nothing. `Trampoline::create` allocates near the game's `.text` segment
and fails loudly ("failed to create trampoline") if it cannot. Logging first means the plugin log records why the
fallback ran (spec FR-006).

**Alternatives considered**: fail immediately when the interface is missing (needlessly refuses environments where
our own allocation would work).

---

## R5. Named failure instead of the library's generic message

**Decision**: add `Hook::stl::detail::EnsureHookSpace(std::size_t a_bytes, std::source_location a_loc)` in
`HookUtil.hpp`, called by every helper that consumes hook space before it writes. If the trampoline is empty or
`free_size() < a_bytes`, it calls `SKSE::stl::report_and_fail` with:
`"Integrated Magic: not enough hook space to install hook at {function}: need {n} B, free {f} B of {c} B reserved"`.

**Rationale**:
- `report_and_fail` (`include/SKSE/Impl/PCH.h`) logs the message at `critical` to the default spdlog logger (our
  `IntegratedMagic.log`), shows a MessageBox captioned with the DLL file name, and terminates. Checking before
  `write_call` lets the plugin's message replace `Trampoline.cpp(164): Failed to handle allocation request`.
- A `std::source_location` default argument on the helpers captures the caller (for example
  `IntegratedMagic::Inbound::D3DInitHook::Install`) without changing any call site.
- A hard stop matches spec FR-005 and the Assumptions: a partially hooked plugin would break Constitution
  Principle III.

**Alternatives considered**: explicit `std::string_view` name parameter (clearer, but changes all call sites and
duplicates the names the Install functions already log); continuing without the hook (leaves the HUD or input
dead and invites hangs).

---

## R6. Startup hook report

**Decision**:
- Each `Install()` logs `spdlog::info("[Hooks] <Name> installed")` once its patch is written: input polling, D3D
  init, present, player anim graph, magic caster interrupt, magic caster start cast.
- `Install_Hooks()` ends with
  `spdlog::info("[Hooks] hook space: {} / {} B used ({:.0f}%), SKSE trampoline interface: {}", ...)`, using
  `allocated_size()`, `capacity()` and `GetTrampolineInterface() != nullptr`.
- If usage is above 75%, it logs `spdlog::warn` (spec SC-003).

**Rationale**: these lines must appear in release builds, so they use `spdlog::info`/`warn` directly rather than
`MAGIC_DEBUG_LOG`. Today `include/PCH.h` defines `DEBUG` unconditionally, but that is a separate concern and may
change. The `[Hooks]` prefix matches existing log lines (Constitution Principle VI).

**Alternatives considered**: CommonLib's `Trampoline::log_stats()` (private, and logs at `debug` level, which is
filtered at the plugin's `info` level).

---

## R7. Jump-style helper (`write_jmp`)

**Decision**: keep both `write_jmp` overloads, remove their `AllocTrampoline(8)` calls, and have them call
`EnsureHookSpace(14, loc)` before `write_branch<5>`.

**Rationale**: `write_branch<5>` allocates 14 bytes, so the old 8-byte request was wrong under any library
semantics (spec FR-007). Routing every helper through the same check keeps the helper API consistent. The helpers
are unused today, so the change carries no runtime risk.

**Alternatives considered**: delete `write_jmp` (also satisfies FR-007, but removes a generic utility likely to be
reused for future hooks).

---

## R8. AnimEventAdapter

**Decision**: no instance or registration is needed. Remove the unused `#include
"Adapters/Inbound/AnimEventAdapter.h"` from `src/Hooks.cpp` and document the event path in `.claude/CLAUDE.md`.

**Evidence**:
- `include/Adapters/Inbound/AnimEventAdapter.h` declares only the free function
  `AnimListener::HandleAnimEvent(const RE::BSAnimationGraphEvent*)`. It has no state and no sink object.
- `src/Adapters/Inbound/PlayerAnimGraphHook.cpp` patches `RE::VTABLE_PlayerCharacter[2]`, slot 1, and calls
  `AnimListener::HandleAnimEvent` after the original handler returns.
- `PlayerCharacter` vtable order: [0] primary (`TESForm` chain), [1] `BSHandleRefObject`,
  [2] `BSTEventSink<BSAnimationGraphEvent>`, [3] `IAnimationGraphManagerHolder`. Slot 1 of a `BSTEventSink` is
  `ProcessEvent` (slot 0 is the destructor).
- `VTABLE_PlayerCharacter` and `VTABLE_ActorMagicCaster` are byte-identical in v10.1.0 and v16.9.0
  (`include/RE/Offsets_VTABLE.h`).

**Alternatives considered**: turning the adapter into a registered `BSTEventSink` (redundant with the vtable hook
and would deliver events twice).

---

## R9. Verification approach

**Decision**: compile both `release` and `debug` presets, then validate in game with the DEBUG log using the new
TEST_MATRIX section "0. Startup & hooks" plus the affected cases listed in the spec. Do a one-off negative check
(temporarily lower `kHookSpaceBytes` in a local debug build, not committed) to confirm the named failure (SC-006).

**Rationale**: Constitution Principle VI: no automated harness can load the game, so `docs/TEST_MATRIX.md` plus log
review is the acceptance suite.

**Alternatives considered**: CommonLib's Catch2 `[integration]` tests (they test the library, not this plugin's
load sequence).
