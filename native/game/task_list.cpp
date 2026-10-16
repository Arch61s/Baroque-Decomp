#include "game/task_list.hpp"
namespace baroque::game {
namespace { TaskList gTasks; }
TaskList& tasks() { return gTasks; }

// orig FUN_002073e0 (work/decompiled/002073e0.c): append at tail unless the remove bit is set; clears 0x30000000, sets bit0
void TaskList::insert(Task* t, int slot) {
    if ((t->flags & 0x10000000u) == 0) {
        if (head == nullptr) { head = t; t->prev = nullptr; }
        else { tail->next = t; t->prev = tail; }
        t->next = nullptr;
        tail = t;
        t->slot = slot;
    }
    t->flags &= 0xcfffffffu;
    t->flags |= 1u;
}
// orig FUN_00207380 (work/decompiled/00207380.c)
void TaskList::unlink(Task* t) {
    if (t->prev == nullptr) head = t->next; else t->prev->next = t->next;
    if (t->next == nullptr) tail = t->prev; else t->next->prev = t->prev;
    t->slot = 0;
    t->flags &= 0xeffffffeu;
}
// orig FUN_00101bd0 task walk: remove flagged tasks (free when 0x20000000) else call update
void TaskList::run() {
    walking |= 1u;
    Task* t = head;
    while (t != nullptr) {
        Task* nextTask;
        if ((t->flags & 0x10000000u) == 0) {
            t->update();
            nextTask = t->next;
        } else {
            nextTask = t->next;
            unlink(t);
            if (t->flags & 0x20000000u) { delete t; --count; }   // FUN_00101200 = free
        }
        t = nextTask;
    }
    walking &= ~1u;
}
void TaskList::clear() {
    while (head != nullptr) { Task* t = head; unlink(t); delete t; }
    count = 0;
}
}
