# Feature Specification: The build type decides whether debug logging is compiled in

**Feature Branch**: `fix/debug-build-define` (created from `main` at `857f8ce`, after spec 009 merged as #78)

**Created**: 2026-11-21

**Status**: Implemented. The debug build loads in game again (validated 2026-11-21, research R7); see the
implementation log in `tasks.md`. The in-game release session (T021, TEST_MATRIX 0.6) is still open.

**Input**: User description: "vamos resolver o problema da build de debug de forma de forma que eu não precise ficar
colocando ou removendo comentário no "define DEBUG" e o próprio tipo de build escolhido já definir a variavel ou não"

## Background & Investigation Findings *(context)*

Source: a review of `main` at `857f8ce`.

1. **How debug logging is switched today**: by one line in the plugin's shared header (`include/PCH.h:12`,
   `#define DEBUG`).
   - **The line is active**: the plugin's 278 debug log calls (in 26 files) and 11 debug-only diagnostic blocks are
     compiled in.
   - **The line is commented out**: all of them compile out.
   - **The build presets** (`debug` and `release`) do not set it. The build type has no effect on it.
2. **What the developer does**: they comment the line out for a public release build, and restore it for a test build.
   - **History**: the line was committed both commented out (for example `f5f2c71`, `bf0ff7f`, `ed68117`, `6188de8`)
     and active.
   - **The risk**: forgetting the edit ships a release that writes debug logs, or makes a test build with none.
3. **Today on `main`**: the line is active, so the release build writes debug logs. Constitution Principle VI says debug
   logging MUST compile out of release builds.
4. **The debug build fails at game start-up**.
   - **What SKSE reports**: "fatal error occurred while loading plugin".
   - **Where it stops**: the plugin's log ends before the first hook is installed, so the fault is inside the hook
     installation.
   - **Release builds**: the same code loads fine.
   - **Root cause**: not known.
   - **Consequence**: specs 002–009 were validated in game on the release build with the line active, with the user's
     approval each time.
   - **This spec fixes it** (Clarifications, FR-008).
5. **Always-on lines**: some lines are written in every build, whatever the line says. Examples are `[Hooks] ...
   installed`, the hook space report, warnings, errors and `Logger iniciado.`.

## Clarifications

### Session 2026-11-21

- Q: Which build has debug logs and loads in the game (FR-008)? → A: The debug build. Its start-up crash is fixed in
  this spec, so it becomes the build for in-game validation, as Constitution Principle VI says. No build type is added.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Choosing the build type is enough (Priority: P1)

The developer picks the debug build to test and the release build to ship. The debug build writes the debug logs, and
the release build does not. No source file is edited between the two.

**Why this priority**: this is the request. A manual edit before every build is easy to forget, and both mistakes
reach players or waste a test session.

**Independent Test**: from a clean checkout with no edits, build each type, start the game, and read the log.

**Acceptance Scenarios**:

1. **Given** a checkout with no local edits, **When** the developer builds the debug build and starts the game, **Then**
   the log has the debug lines (for example `[Input] LoadHotkeyCache:` and `[SpellSystem]` lines).
2. **Given** the same checkout, **When** the developer builds the release build and starts the game, **Then** the log
   has only the always-on lines.
3. **Given** the developer switches between the two build types several times, **When** each one is built, **Then** each
   result matches its type, with no edit and no leftover from the other type.

---

### User Story 2 - The debug build loads in the game (Priority: P1)

The developer validates every spec in game by reading the debug logs, so the debug build must load in the game.

**Why this priority**: today only the release build loads, and it has debug logs only because of the manual line. Once
the build type decides, the release build loses them. Without this story, no build is left to validate a spec.

**Independent Test**: build the debug build, start the game through SKSE, reach the main menu, load a save and read the
log.

**Acceptance Scenarios**:

1. **Given** the debug build is installed, **When** the game starts, **Then** SKSE loads the plugin with no error,
   every hook is installed, and the log has the debug lines.
2. **Given** the same, **When** the developer runs a TEST_MATRIX case, **Then** the log shows the lines that case
   checks.

---

### User Story 3 - The release build ships clean (Priority: P2)

A player installs the release build. Its log has only the always-on lines, and the build contains no debug-only
diagnostic code.

**Why this priority**: it is the constitution's rule and the other side of User Story 1. It is P2 because today's
release builds work. They only write extra lines.

**Independent Test**: build the release build, play a short session (start, load a save, open the SKSE menu, use a
slot) and read the log.

**Acceptance Scenarios**:

1. **Given** the release build is installed, **When** the player plays, **Then** the log has no debug lines, and every
   always-on line is still there.

---

### Edge Cases

- **Code that only feeds debug logs**: values computed only for a debug log line must not trigger new compiler warnings
  when the logs are off.
- **Debug-only diagnostic blocks**: they follow the same rule as the debug log lines.
- **Third-party code built with the plugin** (the game library, the menu framework, ImGui): the switch must not change
  how it is built.
- **A build folder configured before this change**: reconfiguring it applies the new rule, and nothing stale keeps the
  old setting.
- **The copy to the mod folder**: each build copies its DLL to the MO2 mod folder, so the last build made is the one
  the game loads. This stays as today.
- **An old branch merged later** that still has the manual line: after the merge, the build type still decides.
- **The crash fix and the release build**: the cause of the crash may sit in code both builds share. Its fix must not
  change how the release build behaves in game.
- **The debug build in play**: it is slower than the release build. It only has to be usable for the TEST_MATRIX cases,
  not as fast as the release build.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The build type MUST decide whether debug logging is compiled in: on for the debug build and off for the
  release build.
- **FR-002**: Switching between the build types MUST need no source file edit. The manual line is removed.
- **FR-003**: The release build MUST contain no debug log lines and no debug-only diagnostic code (Constitution VI).
- **FR-004**: The always-on lines MUST stay in every build: hook installation, the hook space report, warnings, errors
  and the logger start line.
- **FR-005**: The debug-only diagnostic blocks MUST follow the same rule as the debug log lines.
- **FR-006**: Both build types MUST compile with no new warnings (constitution merge gate 1), including code that only
  feeds debug logs.
- **FR-007**: The switch MUST apply only to the plugin's own code, not to the third-party code built with it.
- **FR-008**: The debug build MUST load in the game with no SKSE error and every hook installed, so it is the build for
  in-game validation. Its start-up crash is fixed in this spec. No build type is added.
- **FR-009**: `.claude/CLAUDE.md` ("Debug logging") MUST say that the debug build has the debug logs and the release
  build does not, and that specs are validated on the debug build. Notes saying the debug build fails to load MUST be
  removed once it loads.
- **FR-010**: No setting, INI key, saved data or gameplay behavior changes. Only which log lines are written changes.
- **FR-011**: `docs/TEST_MATRIX.md` section 0 MUST gain cases for both builds: the release build's log has no debug
  lines, and the debug build loads with every hook installed and writes debug lines (Constitution VI).
- **FR-012**: The crash fix MUST NOT change the release build's behavior in game. FR-003 is the only change to the
  release build.
- **FR-013**: The cause of the crash MUST be recorded in the spec's research, with the log or debugger evidence for
  it.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Switching between the build types needs 0 source file edits, 10 times out of 10.
- **SC-002**: In a release-build session (start, load a save, open the SKSE menu, use a slot), the log has 0 debug
  lines and every always-on line.
- **SC-003**: In the same session on the debug build, the log has the debug lines of each component used (`[Input]`,
  `[State]`, `[FLOW]`, `[SpellSystem]`, `[UI]`).
- **SC-004**: Both build types compile with 0 new warnings.
- **SC-005**: The debug build loads and reaches the main menu in 3 game starts out of 3, with no SKSE error and every
  hook installed.
- **SC-006**: On the debug build, TEST_MATRIX section 0 and one slot cast from section 1 pass in game.

## Assumptions

- **Build types**: the two existing ones, `debug` and `release`, built locally. There is no CI build.
- **Scope**: how debug logging is switched, and the debug build's start-up crash (FR-008). The log file, its
  location, level and format stay as they are.
- **Where the crash fix goes**: the plugin's own code or build settings. If the cause is in a third-party library
  (the game library, the menu framework, Detours), the plan reports it and asks before changing that library.
- **Gameplay**: no change in either build, apart from the log lines.
- **The copy to MO2**: unchanged, so the last build made is installed.
- **Validating this spec**: the release build is checked in game through the always-on lines (SC-002), and the debug
  build through SC-003, SC-005 and SC-006. From then on, specs are validated on the debug build, as the constitution
  says, and the release-build exception of specs 002–009 ends.
