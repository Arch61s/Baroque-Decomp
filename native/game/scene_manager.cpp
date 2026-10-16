#include "game/scene_manager.hpp"
#include "platform/platform.hpp"
#include <cstring>
namespace baroque::game {
namespace { SceneManager gScenes; }
SceneManager& scenes() { return gScenes; }
SceneName sceneName(const char* s) {
    SceneName n = 0;
    for (int i = 0; i < 8 && s[i] != '\0'; ++i) n |= static_cast<SceneName>(static_cast<unsigned char>(s[i])) << (8 * i);
    return n;
}
std::string sceneNameString(SceneName n) {
    std::string s(8, ' ');
    for (int i = 0; i < 8; ++i) s[static_cast<size_t>(i)] = static_cast<char>((n >> (8 * i)) & 0xff);
    return s;
}
// orig FUN_001009a0 (table append; FUN_00100100 registers the ten scenes)
void SceneManager::registerScene(const char* name, Scene* scene) {
    if (count_ < 16) table_[count_++] = Entry{sceneName(name), scene};
}
void SceneManager::replaceScene(const char* name, Scene* scene) {
    const int i = sceneIndex(sceneName(name));
    if (i >= 0) table_[i].scene = scene;
    else registerScene(name, scene);
}
// orig FUN_00206550 (work/decompiled/00206550.c)
void SceneManager::requestScene(SceneName next) {
    next_ = next;
    flags_ = (flags_ & ~1u) | 1u;
}
int SceneManager::sceneIndex(SceneName n) const {
    for (int i = 0; i < count_; ++i) if (table_[i].name == n) return i;
    return -1;
}
// orig FUN_00206570 prologue: *(mgr+0x20) = initial
void SceneManager::start(SceneName initial) {
    next_ = initial;
    needInit_ = true;
    finished_ = false;
}
// orig FUN_00206570 loop top: prev = cur; cur = next; next = 0; find the getter (index -1 when unknown -> the
// original would call garbage; the port logs and quits); Init
void SceneManager::loopTop() {
    previous_ = current_;
    current_ = next_;
    next_ = 0;
    const int idx = sceneIndex(current_);
    if (idx < 0) {
        platform::log("scene manager: unknown scene '" + sceneNameString(current_) + "'");
        flags_ |= 2;
        finished_ = true;
        scene_ = nullptr;
        return;
    }
    scene_ = table_[idx].scene;
    scene_->init();
    needInit_ = false;
}
// one body iteration of FUN_00206570: do { Update } while (!(flags & 1) && !(flags & 2)); Exit; flags &= ~1; return if bit1
void SceneManager::step() {
    if (finished_) return;
    if (needInit_) { loopTop(); if (finished_) return; }
    scene_->update();
    if ((flags_ & 1u) != 0 || (flags_ & 2u) != 0) {
        scene_->exit();
        flags_ &= 0xfeu;
        if (flags_ & 2u) { finished_ = true; return; }
        needInit_ = true;
    }
}
}
