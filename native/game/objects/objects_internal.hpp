#pragma once
// Class-method dispatch of the object framework (objects.cpp -> objects_classes*.cpp).
#include "game/objects/objects.hpp"

namespace baroque::game {

void objClassConstruct(Chr& c);                 // ctor of the class (after FUN_00128590)
void objClassInit(Chr& c);                      // vt+0x1c
void objClassTick(Chr& c);                      // vt+0xc
bool objClassActive(const Chr& c);              // vt+0x20
int objClassContact(Chr& c, Chr* other, uint32_t m1, uint32_t m2, uint32_t m3);   // vt+0x24
void helperTick(Chr& c);                        // vt+0xc of the type-5 helper objects
// second part (objects_classes2.cpp)
void objClassConstruct2(Chr& c);
void objClassInit2(Chr& c);
void objClassTick2(Chr& c);
int objClassContact2(Chr& c, Chr* other, uint32_t m1);
void helperTick2(Chr& c);
// projectiles.cpp (ObjClass::Projectile)
void projConstruct(Chr& c);
void projTick(Chr& c);
void projLaunch(Chr& c, Chr& owner, const Vec3f& at, int bone);
void projDestroy(Chr& c);                       // vtable +8 (class destructor) side effects
void projConstruct2(Chr& c);
void projTick2(Chr& c);
void projLaunch2(Chr& c, Chr& owner, const Vec3f& at, int bone);

}  // namespace baroque::game
