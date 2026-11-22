# Contract: start-up breadcrumbs and the load fault report (research R3)

Debug build only: both are compiled out of the release build with the other debug code. They stay after the crash is
fixed, for any later fault during start-up.

## §1. Breadcrumbs

One `MAGIC_DEBUG_LOG` line before each start-up step, so the last line in the log names the step that faulted. The
logger flushes every info line, so a line is on disk before its step runs.

| Line | Before |
|---|---|
| `[Startup] StyleConfig::Load` | loading `styles.ini`. Nothing can be logged earlier: `Logger iniciado.` marks the logger's start |
| `[Startup] RegisterListener` | registering the SKSE message handler |
| `[Startup] Install_Hooks (game module base=0x<hex>)` | the hook installation. A base of 0 means a static object reached the game at DLL load (research R7) |
| `[Hooks] installing <Name>` | each `Install()` in `Install_Hooks`, before its `installed` line |
| `[Startup] SKSEPluginLoad done` | returning `true` to SKSE |

## §2. The fault report

**Where**: a structured exception filter around the body of `SKSEPluginLoad`. It logs, then returns
`EXCEPTION_CONTINUE_SEARCH`. So SKSE still handles the fault exactly as today ("fatal error occurred while loading
plugin"), and the plugin stays disabled.

**Lines**, once per fault:

```text
[Startup] load fault: code=0x<8 hex> (<name>) at <module>+0x<rva>
[Startup] load fault: access violation, <read|write|execute> at 0x<16 hex>     (only for 0xC0000005)
[Startup] load fault: frame <n>: <module>+0x<rva> <symbol>+0x<disp> (<file>:<line>)
```

- **`<name>`**: `access violation`, `breakpoint` (an assertion's Retry), `stack overflow`, `C++ exception`,
  `illegal instruction`, or `other`.
- **Frames**: at most 24, from the faulting instruction outward, walked from the fault's own context.
- **Symbols**: read from the PDB that the debug build copies next to the DLL. When a frame has no symbol, the line ends
  at `<module>+0x<rva>`.

## §3. Bounds

- **Once**: a second fault in the same load is not reported, so the log cannot loop.
- **Nothing written on success**: a normal load writes only the breadcrumbs.
- **The filter never handles the fault**: it never returns `EXCEPTION_EXECUTE_HANDLER`, so there is no new recovery
  path.
