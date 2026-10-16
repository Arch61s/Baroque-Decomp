#pragma once
// PORT (not in the original): hold-to-skip for EVT events, with a "Hold START to skip" prompt in the bottom-right
// corner drawn with the game's own font (text object 0x4e7e90, FUN_00112e80) on the UI sprite layer 0x4e68c0.
//
// The original skips an event with a single START press (FUN_0013af80 -> FUN_0016d1a0(0x51b620, 0)), and only when
// the event was seen before (+0x20d78 == 0) and the script allows it (+0x20d74, op 193). That path is unchanged.
// Holding START (or the left mouse button) for kHoldTicks ticks additionally calls the original skip with force,
// FUN_0016d1a0(0x51b620, 1), which also skips first-time events: fade to black over 0x23 ticks, then the last cue
// block runs with force (flags set by the event are applied) and FUN_0016cbf0 ends the event.
// Report: work/reports/EVENTS_NATIVE.md section 7.
namespace baroque::game {

struct EventManager;

// PORT: called from the FUN_0013af80 host (hud/pause.cpp, through pauseEventHoldHook) on every tick its input gate
// lets through while an event runs; the hold only counts on such ticks (same gating as the original START skip).
void eventSkipHoldEligible();

// PORT: hold counter, skip request and prompt drawing; called once per engine tick right after FUN_0016d3b0
// (eventEngineTick), also while paused (the glyph sprites are rebuilt every tick).
void eventSkipPromptTick(EventManager* mgr, bool paused);

// PORT: state for tests / logs: hold progress 0..1 and prompt opacity 0..1
float eventSkipHoldProgress();
float eventSkipPromptAlpha();

}  // namespace baroque::game
