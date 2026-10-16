#pragma once
// Engine task list (head 0x4e6614, tail 0x4e6618, count 0x4e661c, walk flag 0x4e6620). Each task: +0 vtable,
// +4 prev, +8 next, +0xc update slot, +0x10 flags (bit0 = inserted, 0x10000000 = remove, 0x20000000 = free).
#include <cstdint>
namespace baroque::game {
struct Task {
    virtual ~Task() = default;
    virtual void update() = 0;   // vtable +0xc
    Task* prev = nullptr;        // +4
    Task* next = nullptr;        // +8
    int slot = 0;                // +0xc
    uint32_t flags = 0;          // +0x10
};
struct TaskList {
    Task* head = nullptr;        // 0x4e6614
    Task* tail = nullptr;        // 0x4e6618
    int count = 0;               // 0x4e661c
    uint32_t walking = 0;        // 0x4e6620 bit0 set while iterating
    void insert(Task* t, int slot);   // FUN_002073e0
    void unlink(Task* t);             // FUN_00207380
    void run();                       // the walk in FUN_00101bd0
    void clear();                     // frees every task (port helper used at shutdown)
};
TaskList& tasks();
}
