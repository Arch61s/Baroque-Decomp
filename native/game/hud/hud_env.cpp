// Reads of engine / scene / menu / option globals for the HUD (kept in their own unit so the headless items check
// can link the module without the front-end scenes; see checks/items_check_main.cpp).
#include "game/hud/hud_api.hpp"
#include "game/engine.hpp"
#include "game/fade.hpp"
#include "game/options.hpp"
#include "game/scene_manager.hpp"
#include "game/ui/menu.hpp"
#include "game/ui/message_box.hpp"

namespace baroque::game {

HudEnvironment hudReadEnvironment() {
    HudEnvironment e;
    e.sceneIsGame = scenes().isCurrent("Game....");   // uRam0053ea90 == "Game...."
    e.systemMenuOpen = ui::menu().isOpen();           // FUN_00167d70(0x51b140)
    e.messageBoxOpen = ui::messageBox().isOpen();     // iRam0053d734
    e.fading = fade().frames != 0;                    // iRam004e7898
    e.paused = engine().paused();                     // FUN_00102020(0x4e6640)
    e.pauseFrames = engine().pauseFrames;             // engine +0x98
    e.cursorPosOption = static_cast<int>(options().cursorPos);   // iRam0051af34
    const ButtonConfig& b = buttons();
    e.buttonTalk = b.slot[0];                         // 0x4e65c0
    e.buttonItemScreen = b.slot[3];                   // 0x4e65d8
    e.buttonNext = b.slot[4];                         // 0x4e65e0
    e.buttonPrev = b.slot[6];                         // 0x4e65f0
    return e;
}

}  // namespace baroque::game
