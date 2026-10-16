// Game side of the sound host's listener (head of FUN_001150b0) and of FUN_00115470's area test (FUN_00124c70).
#include "game/sound_listener.hpp"

#include "audio/sound_system.hpp"
#include "game/camera.hpp"
#include "game/character/character.hpp"
#include "game/dungeon.hpp"
#include "game/event/event_manager.hpp"
#include "game/free_camera.hpp"
#include "game/game_world.hpp"
#include "game/player_move.hpp"

namespace baroque::game {

namespace {

// orig FUN_001150b0 (work/decompiled/001150b0.c), the part before the tracked 3-D SE loop:
//   if (iRam0051b620 == 0) { if (iRam0050b624 != 0) FUN_00116220(snd, cam +0x7c, cam +0x58); }
//   else FUN_00116220(snd, FUN_0016bde0(0x51b620) = +0x40, FUN_0016bdf0(0x51b620) = +0x1c);
//   if (iRam0050b144 != 0) +0x334 = (cell = *(player +0x30)) ? ((*cell & 0xf000) == 0x1000 ? *cell & 0xf : -1) : -1;
void listenerFrame(audio::SoundSystem& snd) {
    if (!eventActive()) {
        const PlayerControl* ctl = playerCtl();                  // 0x50b620; +4 = camera task (0x50b624)
        if (ctl != nullptr && ctl->camera != nullptr) {
            const Camera& cam = *ctl->camera;
            float right[3] = {cam.right[0], cam.right[1], cam.right[2]};   // +0x58
            // PORT (free mouse camera, CAMERA_NATIVE.md 7): while the free camera is the presented gameplay camera the
            // pan follows its right axis, so a sound on the screen's left plays on the left. The position stays the
            // original camera eye (+0x7c): same boom length around the player, same distance attenuation.
            if (freeCameraActive()) {
                float back[3];
                freeCameraAxes(back, right);
            }
            snd.setListener(cam.eye, right);                     // FUN_00116220(snd, +0x7c, +0x58)
        }
    } else {
        const EventManager& ev = eventManager();
        snd.setListener(ev.eye, ev.basis[0]);                    // FUN_00116220(snd, +0x40, +0x1c)
    }
    if (const Chr* p = characters().player()) {                 // iRam0050b144
        uint32_t area = 0xffffffffu;
        if (p->cell != nullptr) {                                // *(player +0x30)
            const uint32_t w = *p->cell;
            if ((w & 0xf000u) == 0x1000u) area = w & 0xfu;
        }
        snd.setListenerArea(area);                               // +0x334
    }
}

// FUN_00124c70(pos, &x, &y) as FUN_00115470 calls it: the cell word under `pos` (false = no cell / no floor).
bool areaLookup(const float pos[3], uint32_t& word) {
    Dungeon* d = gameWorld().dungeon;
    if (d == nullptr) return false;
    int cx = 0, cy = 0;
    const uint32_t* cell = d->cellAtWorld(pos, &cx, &cy);
    if (cell == nullptr) return false;
    word = *cell;
    return true;
}

}  // namespace

void soundListenerInstall(audio::SoundSystem* snd) {
    if (snd == nullptr) return;
    snd->setListenerSource(&listenerFrame);
    snd->setAreaLookup(&areaLookup);
}

}  // namespace baroque::game
