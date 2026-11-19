# Data Model: Fix startup crash from hook-space (trampoline) exhaustion

This feature has no persisted data. The "entities" are the plugin's hook inventory, the single hook-space
reservation, and the startup report written to `IntegratedMagic.log`.

## Hook

An interception point installed once at plugin load by `IntegratedMagic::Hooks::Install_Hooks()`.

| Field | Description |
|---|---|
| name | Log name, e.g. `PollInputDevicesHook` |
| kind | `call5` (5-byte call patch through the trampoline), `call6`, `jmp5`, `vtable`, `wndproc` |
| hookSpaceBytes | `call5`/`jmp5` = 14, `call6` = 8, `vtable`/`wndproc` = 0 |
| target | Address Library ID (SE / AE) + offset, or vtable entry + slot |
| installedAt | Plugin load (`SKSEPluginLoad`), except `WndProcHook` (installed from `D3DInitHook` once the window exists) |

### Current inventory (after this feature)

| # | Hook | Kind | Bytes | Target |
|---|---|---|---|---|
| 1 | PollInputDevicesHook | call5 | 14 | `RelocationID(67315, 68617)` + `VariantOffset(0x7B, 0x7B, 0x81)` |
| 2 | PlayerAnimGraphHook | vtable | 0 | `VTABLE_PlayerCharacter[2]`, slot 1 (`BSTEventSink<BSAnimationGraphEvent>::ProcessEvent`) |
| 3 | D3DInitHook | call5 | 14 | `RelocationID(75595, 77226)` + `VariantOffset(0x9, 0x275, 0x9)` |
| 4 | DXGIPresentHook | call5 | 14 | `RelocationID(75461, 77246)` + `VariantOffset(0x9, 0x9, 0x15)` |
| 5 | MagicCasterInterruptHook | vtable | 0 | `VTABLE_ActorMagicCaster[0]`, slot 8 |
| 6 | MagicCasterStartCastHook | vtable | 0 | `VTABLE_ActorMagicCaster[0]`, slot 6 |
| 7 | WndProcHook | wndproc | 0 | `SetWindowLongPtr` on the game window |

**Total hook space required**: 42 bytes.

### Validation rules

- The sum of `hookSpaceBytes` over all hooks MUST be ≤ 75% of `HookSpaceReservation.capacity` (spec SC-003),
  enforced at compile time by `static_assert` in `include/Hooks.h` and reported at runtime.
- Each hook installs exactly once per process. Game load or new game MUST NOT install it again.

## HookSpaceReservation

The single executable memory block used by every trampoline-consuming hook (`SKSE::GetTrampoline()`).

| Field | Description |
|---|---|
| capacity | `IntegratedMagic::Hooks::kHookSpaceBytes` = 128 |
| used | `SKSE::GetTrampoline().allocated_size()` after all hooks installed (expected 42) |
| source | SKSE shared branch pool (preferred). Falls back to `Trampoline::create` near the game module when the pool is exhausted (library) or the SKSE interface is missing (plugin, R4) |

### Lifecycle

```
SKSEPluginLoad
  └─ SKSE::Init(skse, {.trampoline = true, .trampolineSize = 128})
       ├─ interface present → AllocateFromBranchPool(128) → (null → create(128))
       └─ interface absent  → nothing allocated
  └─ Install_Hooks()
       ├─ empty()? → warn + create(128)              [R4]
       ├─ for each hook: EnsureHookSpace(bytes) → write → log "installed"   [R5, R6]
       └─ log summary used/capacity (+ warn if > 75%)                         [R6]
```

State never changes after `Install_Hooks()` returns.

## StartupHookReport

Log lines written to `%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\IntegratedMagic.log` during
plugin load. The exact format is defined in [contracts/hook-installation.md](contracts/hook-installation.md).

| Field | Description |
|---|---|
| per-hook line | `[Hooks] <Name> installed` (info) |
| fallback line | `[Hooks] SKSE trampoline interface unavailable ...` (warn), only when R4 triggers |
| summary line | `[Hooks] hook space: <used> / <capacity> B used (<pct>%), SKSE trampoline interface: <yes/no>` (info) |
| high-usage line | `[Hooks] hook space usage above 75% ...` (warn), only when exceeded |
| failure line | critical message from `EnsureHookSpace`, followed by the MessageBox and process termination |
