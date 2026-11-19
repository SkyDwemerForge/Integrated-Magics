# Implementation Plan: Fix startup crash from hook-space (trampoline) exhaustion

**Branch**: `feat/patch-for-spell-channeling-skip` (feature dir `specs/001-fix-trampoline-allocation`) | **Date**:
2026-11-19 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/001-fix-trampoline-allocation/spec.md`

## Summary

The plugin terminates at load with `Trampoline.cpp(164): Failed to handle allocation request`. Since CommonLib's
2026-08-21 change (in v10.1.0+, now v16.9.0), only the first hook-space request is honored. The plugin requests
14 bytes per call-site hook, so the second hook (D3D init) finds the trampoline full.

The fix:

- Make **one reservation of 128 bytes** through `SKSE::Init(..., { .trampoline = true, .trampolineSize })`.
- Remove all per-hook `SKSE::AllocTrampoline` calls from `HookUtil.hpp` and replace them with a named space
  pre-check.
- Fall back to the plugin's own allocation when SKSE's trampoline interface is missing.
- Log each installed hook and the reservation usage.
- Fix the latent `write_jmp` under-reservation.
- Remove the unused `AnimEventAdapter` include. The adapter needs no instance.
- Document the policy in `.claude/CLAUDE.md` and add TEST_MATRIX section 0.

Research: [research.md](research.md).

## Technical Context

**Language/Version**: C++23, MSVC 14.52 (Visual Studio 2026), `/W4 /permissive-`

**Primary Dependencies**: CommonLibSSE-NG v16.9.0 (`extern/CommonLibVR-ng`), SKSE64, spdlog 1.17 / fmt 12.2,
Microsoft Detours + `HookUtil.hpp` (vendored in `extern/Detours`), ImGui 1.89.1

**Storage**: N/A (no persisted data changes)

**Testing**: no automated harness against the game. `cmake` builds of `release` + `debug`, then manual in-game
validation with `docs/TEST_MATRIX.md` and DEBUG log review (Constitution VI)

**Target Platform**: Windows x64, Skyrim SE 1.5.97 and AE 1.6.x through Address Library. VR disabled.

**Project Type**: SKSE plugin (single DLL `IntegratedMagic.dll`)

**Performance Goals**: startup only. One extra reservation and about 8 log lines at load; zero per-frame cost

**Constraints**:

- One DLL for SE and AE.
- No hardcoded offsets.
- Hook targets unchanged.
- Hooks must chain originals.
- Hook-space usage ≤ 75% of the reservation.

**Scale/Scope**: 7 hooks (3 call-site × 14 B = 42 B; 3 vtable; 1 WndProc). About 10 source/doc files touched.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle / gate | Assessment | Status |
| --- | --- | --- |
| **I. Layered architecture** | Changes are confined to the inbound hook layer (`Adapters/Inbound/*`, `Hooks.*`, `HookUtil.hpp`) and the entry point (`plugin.cpp`). No Domain, Application or UI logic changes; no new game mutation in `Domain/`. | ✅ Pass |
| **II. Main-thread state machine integrity** | Hooks install during `SKSEPluginLoad` on the loader thread, before gameplay. `MagicState` and its invariants are untouched. | ✅ Pass |
| **III. Never strand the player** | The plugin either installs every hook or stops at load with a named error. It never runs with only some hooks installed. Session/restore logic is unchanged. | ✅ Pass |
| **IV. Dual-cast synchronization** | `PumpAutomatic`, `PumpCastPhase`, `OnCasterInterrupt` and `SyntheticInput` are not modified. The input polling hook's thunk is unchanged; only how it is installed changes. Section 1 run as a smoke regression, not as a gate trigger. | ✅ Pass |
| **V. Opt-in & backward compatibility** | No user-visible behavior change, so no patch flag needed. INI and persistence formats are untouched. Hooks still call the originals. The reservation prefers SKSE's shared branch pool (cooperative with other plugins). | ✅ Pass |
| **VI. Observable & verified in-game** | Adds release-visible `[Hooks]` startup lines, adds TEST_MATRIX section 0, and the spec lists the affected cases. | ✅ Pass |
| **Tech constraints** | Address Library IDs unchanged; no new third-party dependency; no per-frame work added; `.clang-format` style. | ✅ Pass |
| **Merge gates** | Plan includes `release` + `debug` builds, TEST_MATRIX cases, the 5.1 baseline, and the `.claude/CLAUDE.md` update. | ✅ Planned |

**Pre-design result**: PASS, no violations.

**Post-design re-check (after Phase 1)**:

- The design adds two symbols to `include/Hooks.h` (constants plus a `static_assert`) and one helper to
  `HookUtil.hpp`.
- It uses a library-supported init option, keeps `UI/` from mutating state, and adds no new dependency.
- No principle changes status. **PASS.**

## Project Structure

### Documentation (this feature)

```text
specs/001-fix-trampoline-allocation/
├── spec.md                       # /speckit-specify output
├── plan.md                       # This file
├── research.md                   # Phase 0: R1–R9 decisions
├── data-model.md                 # Phase 1: hook inventory, reservation, startup report
├── quickstart.md                 # Phase 1: build + in-game validation guide
├── contracts/
│   └── hook-installation.md      # Phase 1: reservation, helper API, log-line contract
├── checklists/
│   └── requirements.md           # Spec quality checklist
└── tasks.md                      # Phase 2 (/speckit-tasks, not created here)
```

### Source Code (repository root)

```text
include/
└── Hooks.h                                   # + kCallHookBytes, kCallHookCount, kHookSpaceBytes, static_assert   (FR-001)

src/
├── plugin.cpp                                # SKSE::Init(skse, {.trampoline = true, .trampolineSize = kHookSpaceBytes})  (FR-001/002)
├── Hooks.cpp                                 # − AnimEventAdapter include (FR-009); empty() fallback (FR-006); usage summary + 75% warn (FR-004)
└── Adapters/Inbound/
    ├── PollInputDevicesHook.cpp              # + "[Hooks] PollInputDevicesHook installed" (FR-004)
    ├── D3DInitHook.cpp                       # MAGIC_DEBUG_LOG → spdlog::info for the "installed" line (FR-004)
    ├── DXGIPresentHook.cpp                   # same (FR-004)
    ├── PlayerAnimGraphHook.cpp               # + installed line (FR-004); behavior unchanged (FR-008)
    ├── MagicCasterInterruptHook.cpp          # + installed line (FR-004)
    └── MagicCasterStartCastHook.cpp          # + installed line (FR-004)

extern/Detours/
└── HookUtil.hpp                              # − all SKSE::AllocTrampoline; + detail::EnsureHookSpace; source_location;
                                              #   write_jmp checks 14 B (FR-002/005/007)

.claude/CLAUDE.md                             # Hook installation & hook-space policy; animation event path (FR-011)
docs/TEST_MATRIX.md                           # + section "0. Startup & hooks" (FR-012)
```

**Structure decision**: the existing single-project SKSE plugin layout. The reservation size lives in
`include/Hooks.h` beside `Install_Hooks()` so the hook list and its budget change together. `plugin.cpp` only
passes the constant to `SKSE::Init`.

**Note**: `src/plugin.cpp` already has staged user changes (`SKSE_PLUGIN_LOAD` macro). The implementation edits the
same function body, so review the combined diff before committing.

## Complexity Tracking

No Constitution Check violations; nothing to justify.
