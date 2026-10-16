// Engine tick FUN_00101bd0 (work/decompiled/00101bd0.c), translated subset in the original call order.
#include "game/engine.hpp"
#include "alchemy/enbaya.hpp"
#include "game/actor_anim.hpp"
#include "game/ui/menu.hpp"
#include "game/character/ai.hpp"

#include "game/fade.hpp"
#include "game/hud/hud_api.hpp"
#include "game/pad.hpp"
#include "game/save_manager.hpp"
#include "game/sound_stub.hpp"
#include "game/task_list.hpp"
#include "game/talk/talk_manager.hpp"
#include "game/ui/message_box.hpp"
#include "game/ui/text.hpp"
#include "platform/input.hpp"

namespace baroque::game {

namespace {
Engine gEngine;
}

Engine& engine() { return gEngine; }

// orig FUN_00102020
bool Engine::paused() const { return (flags & 1u) != 0 || (0 < pauseFrames && pauseFrames < 4); }

// orig FUN_00101bd0. Calls not listed are not translated yet and are skipped (UNVERIFIED: no effect in the
// front-end scenes): FUN_001ed450, FUN_001a1dc0 (talk), camera host +0x29c,
// the far-plane block (needs the player), render passes
// (done by the host after the tick), FUN_00106680 (texture manager), FUN_00117870, *(0x53ec50)+0xc.
void Engine::tick() {
    // FUN_00112c60 (glyph release after the frame's render) runs in the host loop before each tick (PORT, main.cpp;
    // AUDIT_FRONTEND F3), so text drawn by a scene before this call reaches the presentation.
    saveManager().poll();                 // FUN_00116ff0(0x5093d0)
    aiGlobalTick();                       // FUN_001ed450
    tasks().run();                        // task list walk (0x4e6614)
    fade().update();                      // FUN_00106840(0x4e7890)
    if (eventFrame) eventFrame(paused()); // FUN_0016d3b0(0x51b620, FUN_00102020(engine)) event manager tick
    ui::messageBox().update();            // FUN_001ac4a0(0x53d730)
    hudGameTick();                        // FUN_00156de0(0x517b60), FUN_00150ba0(0x5179c0), FUN_0015c380 centre + log
    ui::uiTick();                         // FUN_0015c380 help bar, FUN_00167d80 menu, FUN_00168010 brightness
    if (pauseFrames == 0) hudPauseOverlay(paused());   // FUN_00160af0(FUN_00102020(engine)) when +0x98 == 0
    // FUN_001075a0(0x4e78e0, paused): the 3-D pass steps the actor clock lRam004e78e0 by one frame (0x1fca055 ns)
    // when not paused; the port draws after the tick, so only the clock step happens here.
    if (!paused()) ActorAnim::advanceClock(alchemy::enbaya::kFrameNs);
    // render passes happen here in the original (FUN_001075a0 .. host +0x290); the port draws after the tick
    pad().update(platform::pollInput());  // FUN_0020b860(0x4e6280): pad sampled at the END of the tick
    if (soundFrame) soundFrame();         // FUN_001150b0(0x4e9790) sound per-frame
    if (talkManager().active != 0) pauseFrames = 0;   // iRam0053c6e8 (talk manager 0x53c6e0 +8): a conversation clears +0x98
    if (pauseFrames != 0) --pauseFrames;  // +0x98 countdown
    ++tickCount;
}

}  // namespace baroque::game
