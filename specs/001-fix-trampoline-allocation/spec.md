# Feature Specification: Fix startup crash from hook-space (trampoline) exhaustion

**Feature Branch**: `feat/patch-for-spell-channeling-skip` (current branch; no dedicated branch created)

**Created**: 2026-11-19

**Status**: Implemented (code + docs, builds green). In-game validation pending: T001, T015, T018, T021, T024

**Input**: User description: "O integrated Magic deu um erro de trampoline (164), falhou em alocar memoria.
@src/Hooks.cpp Não esta instanciando AnimEventAdapter. Não é necessario instanciar ele? Por que o trampoline esta
falhando? Vamos incluir todos esses problemas a serem investigados e resolvidos nessa spec"

## Background & Investigation Findings *(context)*

Diagnostic findings gathered before writing this spec. They explain the defect; they do not prescribe the fix.

1. **The error**: at plugin load the game shows "Failed to handle allocation request" raised from the hook
   library's trampoline (source line 164). It means a hook asked for more hook space than was left.
2. **How the plugin reserves hook space today**: there is no upfront reservation. Each call-site hook asks for
   14 bytes right before it is installed, which only works if every request grants a fresh block.
3. **What changed**: the hook library (CommonLibSSE-NG) changed its behavior on 2026-08-21 (upstream commits
   `e51effcdc` / `25b34f485`, shipped in v10.1.0 and later). Since then **only the first reservation request is
   honored** and later requests are silently ignored. The plugin moved to that library version in commit `8aaf4bb`
   (2026-09-30) and is now on v16.9.0, which behaves the same way.
4. **Why it crashes**: the plugin installs three call-site hooks (input polling, D3D init, frame present), which
   need 3 × 14 = 42 bytes. The input polling hook installs first and takes the only 14 bytes granted. The D3D init
   hook comes next, finds 0 bytes free, and the game terminates.
5. **When the safety net was lost**: commit `6188de8` (#64, font customization, 2026-03-26) removed the previous
   upfront 64-byte reservation in the same change that added the two render hooks. Under the old library behavior
   the per-hook requests hid this.
6. **AnimEventAdapter**: it does **not** need to be instantiated or registered. It is a stateless function that the
   player animation-graph hook calls directly for every animation event the player receives. That hook patches the
   player's animation event sink entry in the game's vtable, which was verified to be the correct entry and unchanged
   between library v10.1.0 and v16.9.0. The adapter is referenced in the hook installation unit without being used,
   which makes it look like a missing registration.
7. **Hooks that are not affected**: the vtable hooks (player animation graph, magic caster interrupt, magic caster
   start cast) and the window procedure hook use no hook space. Their vtable entries were checked and did not change
   in the library update.
8. **Latent defect**: the helper for jump-style hooks reserves 8 bytes, but a 5-byte jump needs 14. Nothing uses it
   today, but it would reintroduce this crash the first time it is used.
9. **Silent gap**: if SKSE's shared trampoline interface is unavailable, the library skips the reservation entirely
   without reporting anything. Every call-site hook would then fail.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Game starts with Integrated Magic enabled (Priority: P1)

A player launches Skyrim SE or AE through SKSE with Integrated Magic installed. The game reaches the main menu
without any error dialog, and once a save is loaded every feature that depends on the plugin's hooks works: slot
hotkeys are detected, the HUD is drawn, and casts complete.

**Why this priority**: today the plugin terminates the game at load. Nothing else in the mod can be used until this
is fixed.

**Independent Test**: launch the game with SKSE, Address Library, SKSE Menu Framework and Integrated Magic, reach
the main menu, load a save, see the HUD, and cast a spell from a slot hotkey.

**Acceptance Scenarios**:

1. **Given** a load order with Integrated Magic enabled, **When** the game is launched, **Then** it reaches the main
   menu with no "Failed to handle allocation request" (or any other) error dialog from the plugin.
2. **Given** the game reached the main menu, **When** a save is loaded, **Then** the slot HUD is rendered and
   pressing a slot hotkey equips and casts the assigned spell.
3. **Given** a successful launch, **When** the plugin log is opened, **Then** it lists every hook the plugin
   installs, each marked as installed, and the hook space reserved and used.

---

### User Story 2 - Animation-driven behavior keeps working (Priority: P2)

A player uses slots whose sessions end on animation events: casts that finish on cast stop, shouts that finish on
shout stop, restores that wait for the sheathe animation, and sessions interrupted by block or stagger. Every
session ends and the player's previous gear comes back.

**Why this priority**: this answers the AnimEventAdapter question with evidence. If animation events did not reach
the plugin, sessions would hang and gear would not be restored (Constitution Principle III). It only matters once
P1 lets the game start.

**Independent Test**: run the animation-dependent TEST_MATRIX cases (7.10, 8.1, 8.2, 9.6) and confirm in the debug
log that the player's animation event tags are received.

**Acceptance Scenarios**:

1. **Given** a slot activated from sheathed hands, **When** the session ends, **Then** the restore waits for the
   sheathe animation and the previous equipment is restored (TEST_MATRIX 7.10).
2. **Given** an active slot session, **When** the player blocks or is staggered, **Then** the session ends and gear
   is restored (TEST_MATRIX 8.1, 8.2).
3. **Given** a shout slot, **When** the shout ends, **Then** the session finishes and the voice slot is restored
   (TEST_MATRIX 9.6).
4. **Given** the hook installation code and the developer guide, **When** a developer reads them, **Then** it is
   clear that animation events are delivered by the player animation-graph hook and the adapter needs no instance
   or registration.

---

### User Story 3 - Hook problems are diagnosable and cannot silently come back (Priority: P3)

A developer adds a new hook, or updates the hook library again. If the hook space is not enough, or a hook cannot be
installed, they find out at startup from a message that names the plugin and the hook, not from a generic library
error, and they know where the reservation is defined and how to size it.

**Why this priority**: this prevents a repeat of the regression (the reservation was lost in #64 and the library
change exposed it months later). It is valuable but not needed to unblock players.

**Independent Test**: temporarily lower the reservation below what the installed hooks need in a debug build, launch,
and confirm the message identifies the plugin, the hook and the shortfall. Restore the value afterwards.

**Acceptance Scenarios**:

1. **Given** a reservation smaller than the installed hooks need, **When** the game starts, **Then** the failure
   message identifies Integrated Magic, the hook that could not be installed, and the space required vs. available.
2. **Given** the developer guide, **When** a developer adds a call-site hook, **Then** the guide states where the
   reservation is defined, how much each hook kind consumes, and that the reservation must be updated.

---

### Edge Cases

- **Shared pool exhausted**: other SKSE plugins in a large load order may have used up SKSE's shared branch pool. The
  plugin must still get its hook space by falling back to its own allocation near the game module.
- **SKSE trampoline interface unavailable** (older SKSE or an unusual loader): the hook library silently skips the
  reservation. The plugin must still reserve space or stop with a clear message, never fail generically later.
- **Repeated reservation requests ignored**: the fix must not depend on the library honoring more than one
  reservation, so it survives future library updates in either direction.
- **SE vs AE**: hook addresses and offsets differ per runtime; both must install every hook.
- **Other mods hooking the same call sites** (input polling, present): the original call chain must be preserved
  (Constitution Principle V).
- **Load game / new game after startup**: hooks are installed once at plugin load and must not be re-installed or
  consume more space on later game events.
- **Latent jump-hook helper**: a future use of the jump-style helper must not under-reserve.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The plugin MUST reserve hook space once during startup, before installing any hook, large enough for
  every call-site hook it installs plus a safety margin.
- **FR-002**: Hook installation MUST work whether the hook library honors or ignores repeated reservation requests.
  No hook may rely on its own reservation request being granted.
- **FR-003**: On both Skyrim SE and AE, every plugin hook MUST install at load: input polling, D3D init, frame
  present, player animation graph, magic caster interrupt, magic caster start cast.
- **FR-004**: At startup the plugin log MUST record, for each hook, whether it was installed, plus the total hook
  space reserved and the amount used after all hooks are installed.
- **FR-005**: If the reservation cannot be obtained or a hook cannot be installed, the plugin MUST report an error
  that names Integrated Magic and the specific hook (and, for space problems, required vs. available space) instead
  of relying on the library's generic message.
- **FR-006**: If SKSE's shared trampoline interface is unavailable or its pool is exhausted, the plugin MUST still
  obtain its hook space from its own allocation, or report per FR-005. It MUST NOT continue in a state where hooks
  fail later.
- **FR-007**: The helper for jump-style hooks MUST reserve the space a jump actually needs, or be removed if unused,
  so it cannot reintroduce this failure.
- **FR-008**: Animation events received by the player MUST keep reaching the spell state machine through the player
  animation-graph hook, with no adapter instance or registration step.
- **FR-009**: The hook installation unit MUST reference only what it installs. The unused reference to the animation
  event adapter MUST be removed.
- **FR-010**: Existing gameplay behavior MUST be unchanged: slot activation, casting, HUD, popup, restore and
  persistence behave as before (TEST_MATRIX baseline, Constitution Principle V).
- **FR-011**: The developer guide (`.claude/CLAUDE.md`) MUST document the hook-space policy: where the reservation
  lives, how it is sized per hook kind, what to update when adding a hook, and how animation events reach the
  plugin.
- **FR-012**: `docs/TEST_MATRIX.md` MUST gain a "Startup & hooks" section covering: launch to main menu, all hooks
  reported in the log, launch with a large load order, and both SE and AE.

### Key Entities

- **Hook**: an interception point the plugin installs at load. Attributes: name, kind (call-site, vtable, window
  procedure), hook space it consumes (call-site only), runtime-specific address.
- **Hook space reservation**: the single block of executable memory set aside for call-site hooks. Attributes:
  size, source (SKSE shared pool or the plugin's own allocation), amount used.
- **Startup hook report**: the log entries written at load. Attributes: per-hook result, reserved size, used size,
  error details on failure.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 10 out of 10 consecutive launches with Integrated Magic enabled reach the main menu with no error
  dialog from the plugin, on each available runtime (SE and/or AE).
- **SC-002**: On every launch the startup log reports 100% of the plugin's hooks as installed.
- **SC-003**: After startup, hook space used is no more than 75% of the hook space reserved.
- **SC-004**: In the user's full mod list (the environment where the crash was reported), the game starts and a slot
  cast works on the first save load.
- **SC-005**: The animation-dependent TEST_MATRIX cases 7.10, 8.1, 8.2 and 9.6 pass, and the existing baseline case
  5.1 is unchanged.
- **SC-006**: With the reservation deliberately set too small (debug verification only), the failure message names
  the plugin and the hook in 100% of attempts.

### Verification (TEST_MATRIX)

Per Constitution Principle VI, the affected and new cases are:

- **New section "0. Startup & hooks"**: 0.1 launch to main menu; 0.2 log lists every hook as installed with reserved
  and used space; 0.3 launch with a large SKSE load order; 0.4 repeat on SE and AE.
- **Affected (input polling hook)**: 6.1–6.7.
- **Affected (D3D init / present hooks)**: 10.1, 10.4, 10.5, 10.6, 10.8.
- **Affected (animation events)**: 7.10, 8.1, 8.2, 9.6.
- **Regression baseline**: 5.1.

## Assumptions

- The error the user reported is the one described in Findings 1–4. The plugin log and the error dialog text match
  "Failed to handle allocation request" from the hook library.
- It reproduces with every build that includes the hook library update from commit `8aaf4bb` onward, including the
  current v16.9.0 build.
- Target runtimes are Skyrim SE and AE through Address Library. Skyrim VR is out of scope (disabled in the build).
- Hook targets, addresses and behavior stay as they are; only how hook space is reserved, reported and documented
  changes.
- A hard stop with a clear message is acceptable when hook installation truly cannot proceed. Running with part of
  the hooks missing would leave the mod half-working, which Constitution Principle III forbids.
- Players have a typical SKSE setup in which the shared trampoline interface is present. The fallback in FR-006
  covers the cases where it is not.
