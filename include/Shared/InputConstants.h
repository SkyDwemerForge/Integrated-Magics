#pragma once

inline constexpr int kMaxCode = 400;
inline constexpr int kMouseButtonBase = 256;

// Mouse button ids as reported by RE::BSWin32MouseDevice::Keys; stored as kMouseButtonBase + id.
inline constexpr int kMouseWheelUpId = 8;    // kWheelUp
inline constexpr int kMouseWheelDownId = 9;  // kWheelDown
inline constexpr int kMouseFirstNoVkId = 5;  // kButton5: first mouse button with no Windows virtual key
inline constexpr int kMouseLastNoVkId = 7;   // kButton7: last mouse button with no Windows virtual key
inline constexpr int kMouseHeartbeatTimeoutMs = 500;

// The wheel sends a press with no release: it is never a held input nor a hotkey (spec 003).
[[nodiscard]] constexpr bool IsMouseWheelId(int id) noexcept {
    return id == kMouseWheelUpId || id == kMouseWheelDownId;
}
// Buttons with no Windows key state; their stuck-key recovery uses a heartbeat (spec 003, research R6).
[[nodiscard]] constexpr bool IsMouseNoVkId(int id) noexcept {
    return id >= kMouseFirstNoVkId && id <= kMouseLastNoVkId;
}

// Combo tolerance for combos of 2+ buttons (spec 004): the time allowed between two consecutive buttons of a combo.
// A fresh press of a combo button is held back this long before it goes to the game, and a multi-button slot waits
// this long after its full combo before it activates, so a longer combo completed right after can still win.
inline constexpr float kComboToleranceSec = 0.05f;
// Wait before a single-key slot activates while still held (Require exclusive hotkey on).
inline constexpr float kSingleKeyAcceptDelaySec = 0.04f;
// Max hold for a button while a slot containing it is still pending (spec 004, research R3b).
inline constexpr float kMaxContestedHoldSec = 0.5f;
// Max hold while a chord is in progress: 2+ buttons of a combo held but not complete (spec 004, research R3c).
// Combos have at most 3 buttons, so this is two gaps of kComboToleranceSec.
inline constexpr float kMaxChordHoldSec = 2.f * kComboToleranceSec;
inline constexpr float kPressBothAtSameTimeWindowSec = 0.2f;

inline constexpr int kDIK_W = 0x11;
inline constexpr int kDIK_A = 0x1E;
inline constexpr int kDIK_S = 0x1F;
inline constexpr int kDIK_D = 0x20;
inline constexpr int kDIK_Escape = 0x01;
