#pragma once
// Scene manager 0x53ea80 (SCENES.md 0.1, FUN_00206570 / FUN_00206550 / FUN_001009a0 register).
// Names are 8-byte strings ("BootMC..", "Credit..", "Title...", "Adv.....", "Option..", "EvMovie.", "NameEntr",
// "GameLoad", "Game....", "Ending.."). Layout: +0 table begin, +4 table end (16-byte entries {name, getter}),
// +0x10 current name (uRam0053ea90), +0x18 previous name (uRam0053ea98), +0x20 next name (uRam0053eaa0),
// +0x2c flags (bit0 = change requested, bit1 = quit).
// FUN_00206570 is a blocking loop (Init; do Update while !bit0; Exit). The port drives it one Update per logic
// tick through step(), preserving the order of every call.
#include <cstdint>
#include <string>
namespace baroque::game {
struct Scene {
    virtual ~Scene() = default;
    virtual void init() = 0;     // vtable +0x8
    virtual void exit() = 0;     // vtable +0xc
    virtual void update() = 0;   // vtable +0x10 (calls the engine tick FUN_00101bd0 itself)
};
// 8-byte scene name packed little-endian exactly like the original 64-bit compares
using SceneName = uint64_t;
SceneName sceneName(const char* eightChars);
std::string sceneNameString(SceneName n);
class SceneManager {
public:
    void registerScene(const char* name, Scene* scene);   // FUN_001009a0
    void replaceScene(const char* name, Scene* scene);    // port: swap a placeholder for its translated scene
    void requestScene(SceneName next);                    // FUN_00206550
    void requestScene(const char* name) { requestScene(sceneName(name)); }
    void requestQuit() { flags_ |= 2; }
    void start(SceneName initial);                        // FUN_00206570 entry: +0x20 = initial, then loop top
    void step();                                          // one iteration body: (Init) Update [Exit]
    bool quitRequested() const { return (flags_ & 2) != 0; }
    bool finished() const { return finished_; }
    SceneName current() const { return current_; }        // uRam0053ea90
    SceneName previous() const { return previous_; }      // uRam0053ea98
    SceneName next() const { return next_; }              // uRam0053eaa0
    bool isCurrent(const char* name) const { return current_ == sceneName(name); }
    bool isPrevious(const char* name) const { return previous_ == sceneName(name); }
    bool isNext(const char* name) const { return next_ == sceneName(name); }
    Scene* currentScene() const { return scene_; }
    int sceneIndex(SceneName n) const;
    uint32_t flags() const { return flags_; }
    bool initPending() const { return needInit_; }      // PORT: Exit ran, next Init not yet (main.cpp cursor hold)
private:
    void loopTop();
    struct Entry { SceneName name; Scene* scene; };
    Entry table_[16]{};
    int count_ = 0;
    SceneName current_ = 0, previous_ = 0, next_ = 0;
    uint32_t flags_ = 0;
    Scene* scene_ = nullptr;
    bool needInit_ = true;
    bool finished_ = false;
};
SceneManager& scenes();
}
