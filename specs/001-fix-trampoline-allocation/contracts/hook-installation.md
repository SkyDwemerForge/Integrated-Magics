# Contract: Hook installation and hook-space reservation

Internal developer contract for anyone adding or changing a hook in Integrated Magic. It also defines the startup
log lines that act as the plugin's diagnostic interface for users and bug reports.

## 1. Reservation

- **Owner**: `include/Hooks.h`.
- **Symbols**:
  - `IntegratedMagic::Hooks::kCallHookBytes = 14`: hook space per `write_call<5>` / `write_jmp`.
  - `IntegratedMagic::Hooks::kCallHookCount`: number of trampoline-consuming hooks installed by `Install_Hooks()`.
  - `IntegratedMagic::Hooks::kHookSpaceBytes = 128`: total reservation.
  - `static_assert(kCallHookCount * kCallHookBytes * 4 <= kHookSpaceBytes * 3)`, the 75% budget.
- **Where it is requested**: only in `SKSEPluginLoad`, via
  `SKSE::Init(skse, { .trampoline = true, .trampolineSize = IntegratedMagic::Hooks::kHookSpaceBytes })`.
- **Forbidden**: calling `SKSE::AllocTrampoline` anywhere, or creating a second `SKSE::Trampoline` for hooks.

## 2. Helper API (`extern/Detours/HookUtil.hpp`, namespace `Hook::stl`)

| Helper | Hook space consumed | Precondition | On insufficient space |
|---|---|---|---|
| `write_call<T, 5>(src \| VariantID \| RelocationID [, offset])` | 14 B | reservation exists | `report_and_fail` with the named message (§4) |
| `write_call<T, 6>(...)` | 8 B | reservation exists | same |
| `write_jmp<T>(src \| VariantID [, offset])` | 14 B | reservation exists | same |
| `write_vfunc<...>(...)` | 0 | — | — |
| `write_detour*<T>(...)` | 0 (Detours manages its own memory) | — | existing Detours error messages |

- Helpers MUST NOT request or reserve hook space. They only check it (`EnsureHookSpace`) and consume it.
- Every helper takes a trailing `std::source_location a_loc = std::source_location::current()`, so failure
  messages name the calling `Install()` function without call-site changes.

## 3. Adding a hook (checklist)

1. Install it from `IntegratedMagic::Hooks::Install_Hooks()` (or from a hook that runs later, as `WndProcHook` does).
2. If it consumes hook space, increment `kCallHookCount` (or adjust the byte math for `call6`). The `static_assert`
   fails the build when the 75% budget is exceeded; raise `kHookSpaceBytes` in that case.
3. Log `spdlog::info("[Hooks] <Name> installed")` after the patch is written.
4. Add the hook to `.claude/CLAUDE.md` and to TEST_MATRIX section 0.

## 4. Startup log lines (diagnostic interface)

Written to `IntegratedMagic.log` at `info` unless noted. The prefix is always `[Hooks]`.

```
[Hooks] PollInputDevicesHook installed
[Hooks] PlayerAnimGraphHook installed
[Hooks] D3DInitHook installed
[Hooks] DXGIPresentHook installed
[Hooks] MagicCasterInterruptHook installed
[Hooks] MagicCasterStartCastHook installed
[Hooks] hook space: 42 / 128 B used (33%), SKSE trampoline interface: yes
```

Conditional lines:

```
(warn)     [Hooks] SKSE trampoline interface unavailable or reservation empty; allocating 128 B near the game module
(warn)     [Hooks] hook space usage above 75%: <used> / <capacity> B - raise kHookSpaceBytes
(critical) Integrated Magic: not enough hook space to install hook at <function>: need <n> B, free <f> B of <c> B reserved
```

The critical line is followed by a MessageBox captioned `IntegratedMagic.dll` whose body is
`<file>(<line>): <message>`, and then the process terminates (CommonLib `report_and_fail`).

## 5. Compatibility guarantees

- Hook targets (Address Library IDs, offsets, vtable slots) are unchanged by this contract.
- Every call-site hook calls the original function (`T::func`); vtable hooks call the saved original.
- The reservation prefers SKSE's shared branch pool, the cooperative mechanism for multiple plugins.
