# Integrated Magic Constitution

## Core Principles

### I. Layered Architecture (Ports & Adapters)

Code MUST respect the dependency direction
`Inbound Hooks → Input → Application → Domain`, with `Adapters/Outbound/` carrying effects to the
game and `UI/` only reading state.

- `Domain/` (`MagicState` and helpers) decides *what* happens and returns intents and plans
  (`SlotPressAction`, equip intents, `RestoreSnapshotPlan`, exit results). Game-mutating effects
  (equip/unequip, synthetic input, animation graph variables, draw/sheathe) MUST be executed by
  `Application::` controllers through `Adapters/Outbound/`.
- `Domain/` MAY read game state (player, magic casters, inventory) through the shared helpers
  (`CasterUtil`, `InventoryUtil`, `SpellClassify`).
- New code MUST NOT add direct game mutations to `Domain/`. The existing direct calls in
  `src/Domain/MagicStateLifecycle.cpp` (`ApplySkipEquipAnimReturn` and `DrawWeaponMagicHands` on the
  sheathe-restore path) are known debt: they MUST NOT be expanded and SHOULD be moved into an intent
  when that code is next changed.
- Configuration MUST be consumed through the `Config/Ports/` interfaces. A new option MUST be added
  to `Config.h`, its Port interface, and `ConfigAdapter.cpp` (INI load/save) in the same change.
- `UI/` MUST NOT mutate `MagicState`; it renders a `HudView` and performs assignments only through
  `MagicAssign` and the persistence APIs.

Rationale: the architecture rework (#67) split decisions from effects so that cast-flow bugs can be
diagnosed from state and logs alone. Letting game calls drift back into the domain recreates the
coupling that rework removed.

### II. Main-Thread State Machine Integrity

- Every call into `MagicState` MUST happen on the game's main thread (hooks and frame callbacks).
  Work done on any other thread MUST NOT touch `MagicState` and MUST hand results back through a
  main-thread task.
- `KeyStateStore` MUST be written only by `PollInputDevicesHook`; other code MAY only read it.
- These invariants MUST hold after every transition:
  - `_session.active` implies `_session.activeSlot >= 0`.
  - `RestoreContext` is meaningful only while a session is active and is cleared by
    `ResetSessionState()`.
  - `OnSlotPressed` is never re-entered while `_inSlotSetup` is true.
- New state MUST live in the existing structs (`SessionState`, `HandMode`, `RestoreContext`,
  `ShoutState`, `AutoAttackState`) and MUST be reset on every exit path, including `ForceExit`. A flag
  that can survive into the next session is a defect.
- New phases, flags, or invariants MUST be documented in `.claude/CLAUDE.md` in the same change.

Rationale: flags leaking across sessions are the recurring root cause of "slot stuck active" bugs.

### III. Never Strand the Player

Every activation MUST end with the player in a controllable state and wearing their previous gear.

- Every session MUST have a reachable exit: normal finish, key release or second press, interruption
  (block, bash, stagger, knockdown, death, menu open, game load), or the safety timeout
  (`activeTimeoutSecs`). Any new wait state (anim event, `castStop`, sheathe, spell fire) MUST have
  its own timeout or fallback so a missing game event cannot hang the session.
- On exit, everything marked dirty (`dirtyLeft`, `dirtyRight`, `dirtyShout`, `prevExtraEquipped`)
  MUST be restored from the snapshot. The only exception is `ForceExitNoRestore` when the player or
  another mod equips something the plugin did not: the plugin MUST NOT overwrite foreign equipment.
- Every synthetic attack or shout input sent DOWN MUST be released UP on every exit path.
- The plugin MUST NOT duplicate, lose, or leave behind items it equipped (bound weapons included).

Rationale: players rely on the mod mid-combat. A spell stuck in hand or a lost weapon is worse than a
missed cast; several past fixes (#46, #52, 769799d, 7819c49) addressed exactly these failure modes.

### IV. Dual-Cast Synchronization (NON-NEGOTIABLE)

While `_session.isDualCasting` is true:

- Both hands MUST go DOWN on the same frame.
- Any UP dispatched for one hand (pre-cast interrupt, redispatch timeout, restart) MUST be paired
  with an UP for the other hand in the same frame, and both MUST go DOWN together on the next frame
  (`pendingRestartNextFrame`).
- Any change to `PumpAutomatic`, `PumpCastPhase`, `OnCasterInterrupt`, or `SyntheticInput` MUST be
  verified against `docs/TEST_MATRIX.md` section 1 (dual-cast regression) before merge.

Rationale: if the hands desync, the game's behavior graph leaves dual-cast mode and fires two
single casts. This principle cannot be waived through Complexity Tracking.

### V. Opt-In Behavior & Backward Compatibility

- Any change that alters the game's native cast, equip, or input flow in a way some users may not
  want (skipping animations, skipping channeling, exclusive hotkeys, simultaneous-press windows) MUST
  be a patch flag in `MagicConfig` / `IPatchSettings`, default `false`, and exposed in the in-game
  Patches tab (`src/UI/MENU.cpp`). With the flag off, behavior MUST be identical to before the change.
- INI sections and keys MUST NOT be renamed or removed without a migration that still reads the old
  key. New keys MUST have safe defaults when absent.
- Persisted data (`SaveSpellDB` per-save slots, `SpellSettingsDB` global spell settings) MUST remain
  loadable across plugin versions; existing users' assignments MUST survive an update. Format changes
  MUST be versioned and ship with a migration path.
- The plugin MUST coexist with other mods: hooks MUST call the original function; input events not
  claimed by a slot combo MUST be passed through or replayed (`ReplaySystem`); compatibility with known
  mods (e.g., Skip Equip Animations) MUST be handled by detection or a patch flag, never by assuming
  the other mod is absent.

Rationale: users update mid-playthrough with large load orders. Silent behavior changes or lost slot
assignments break saves and generate the bug reports this project spends most of its time on.

### VI. Observable and Verified In-Game

- Every state transition, phase change, synthetic input dispatch, and restore decision MUST emit a
  `MAGIC_DEBUG_LOG` line with a bracketed component prefix (`[FLOW]`, `[State]`, `[Input]`,
  `[SaveLoad]`, `[Action]`, `[SpellSystem]`, ...). Debug logging MUST compile out of release builds.
- There is no automated test harness against the running game; `docs/TEST_MATRIX.md` is the
  acceptance suite. Each spec MUST list the TEST_MATRIX cases it affects and add new cases for new
  behavior. A feature is done only when those cases pass in-game on a DEBUG build and the log output
  has been reviewed.
- Logic with no game dependency (hotkey matching, edge detection, timing windows, cooldown math)
  SHOULD stay free of `RE::` types so it can be unit-tested if a harness is introduced.

Rationale: game timing (animation events, caster states) cannot be faithfully mocked, so logs plus
the manual matrix are the only reliable evidence that a cast flow works.

## Technical Constraints

- **Toolchain**: C++23 on MSVC (Visual Studio 2022) with `/W4 /permissive- /Zc:preprocessor`;
  CMake presets `debug` and `release` (Ninja); vcpkg triplet `x64-windows-static-md`.
- **Game library**: CommonLibSSE-NG through the `extern/CommonLibVR-ng` submodule, VR disabled.
- **Runtimes**: one DLL (`IntegratedMagic.dll`) MUST support both Skyrim SE and AE. Game addresses
  MUST be resolved through Address Library (`REL::RelocationID`, `REL::VariantOffset`) with both SE
  and AE IDs; hardcoded single-runtime offsets are forbidden.
- **Dependencies**: ImGui (HUD on the DXGI Present hook), Microsoft Detours, SKSE Menu Framework,
  XInput. Any new third-party dependency MUST be justified in the plan's Complexity Tracking.
- **Frame budget**: `SpellSystemController::OnFrame`, input filtering, and `HudManager::DrawHudFrame`
  run every frame. They MUST NOT perform file I/O, blocking waits, or unbounded allocations;
  persistence writes happen only when data changes.
- **Limits**: `Config::kMaxSlots = 64`; hotkey combos of at most 3 keys per device (keyboard/mouse
  and gamepad).
- **Style**: `.clang-format` (Google-based, 4-space indent, 120 columns, indented namespaces).
  Identifiers, comments in code, and log messages are written in English.

## Development Workflow & Quality Gates

- **Branches**: `feat/<topic>` or `fix/<topic>` off `main`, merged through a pull request.
- **Commits**: Conventional Commits with a scope and optional gitmoji, e.g.
  `feat(HUD): :sparkles: ...`, `fix(Spell): :bug: ...`, `chore(deps): ...`.
- **Spec-driven flow**: features and non-trivial fixes go through `/speckit-specify` →
  (`/speckit-clarify`) → `/speckit-plan` → `/speckit-tasks` → `/speckit-implement`. Trivial fixes
  (typos, one-line corrections) MAY skip it. Notes in `.claude/tasks/*.md` are inputs to specs.
- **Merge gates** (all MUST pass):
  1. Both `debug` and `release` presets build with no new warnings.
  2. The TEST_MATRIX cases listed in the spec pass in-game; changes to the cast pipeline also pass
     section 1.
  3. With every new patch flag OFF, baseline behavior (TEST_MATRIX 5.1) is unchanged.
  4. `.claude/CLAUDE.md` is updated whenever architecture, state fields, invariants, or config
     options change.
- **Plugin versioning**: `project(IntegratedMagic VERSION x.y.z)` in `CMakeLists.txt` follows semver:
  MAJOR for breaking INI or persisted-format changes, MINOR for features and new patches, PATCH for
  fixes.

## Governance

- This constitution supersedes other development practices. `.claude/CLAUDE.md` is the runtime
  guidance file (architecture map, file lookup, debugging tips); when it conflicts with this
  document, the constitution wins and `CLAUDE.md` MUST be corrected.
- **Amendments** are made with `/speckit-constitution` and merged through a pull request that reviews
  the Sync Impact Report and updates any affected documents (`.claude/CLAUDE.md`,
  `docs/TEST_MATRIX.md`).
- **Versioning policy**: MAJOR when a principle is removed or redefined incompatibly; MINOR when a
  principle or section is added or materially expanded; PATCH for clarifications and wording.
- **Compliance**: every `/speckit-plan` MUST complete the Constitution Check against Principles I–VI.
  A violation MUST be justified in the plan's Complexity Tracking table or the plan is rejected;
  Principle IV (NON-NEGOTIABLE) cannot be waived. Pull request reviews MUST confirm the merge gates.
- **Review cadence**: the constitution is revisited at each MINOR or MAJOR plugin release.

**Version**: 1.0.0 | **Ratified**: 2026-11-19 | **Last Amended**: 2026-11-19
