// What the C++ library needs from a freestanding image: a heap, operator
// new/delete, and libstdc++'s error hooks (exceptions are off, so each one
// stops the deck with a message on the console).
#include <stddef.h>
#include <stdint.h>
#include <new>
#include <string>
#include "con.h"

// libstdc++ declares std::string's members extern (built into libstdc++.so);
// we link no libstdc++, so they are made here.
template class std::__cxx11::basic_string<char>;

extern "C" char _end[];

namespace {
// First fit over a free list, merging neighbours on free.  The heap runs from
// the end of the image to 1 MB under the top of DRAM (the main stack's).
struct Block { size_t size; Block *next; bool free; };
constexpr uintptr_t kHeapTop = 0xA7F00000u;
Block *head;

void heap_init()
{
    head = reinterpret_cast<Block *>((reinterpret_cast<uintptr_t>(_end) + 15) & ~uintptr_t(15));
    head->size = kHeapTop - reinterpret_cast<uintptr_t>(head) - sizeof(Block);
    head->next = nullptr;
    head->free = true;
}

[[noreturn]] void halt(const char *why)
{
    con_printf("\nfatal: %s\n", why);
    for (;;) {}
}
} // namespace

extern "C" void *malloc(size_t n)
{
    if (!head) heap_init();
    n = (n + 15) & ~size_t(15);
    for (Block *b = head; b; b = b->next) {
        if (!b->free || b->size < n) continue;
        if (b->size >= n + sizeof(Block) + 16) {                // split
            Block *rest = reinterpret_cast<Block *>(reinterpret_cast<char *>(b + 1) + n);
            rest->size = b->size - n - sizeof(Block);
            rest->next = b->next;
            rest->free = true;
            b->next = rest;
            b->size = n;
        }
        b->free = false;
        return b + 1;
    }
    return nullptr;
}

extern "C" void free(void *p)
{
    if (!p) return;
    Block *b = static_cast<Block *>(p) - 1;
    b->free = true;
    for (Block *c = head; c; c = c->next)                       // merge free neighbours
        while (c->free && c->next && c->next->free) { c->size += sizeof(Block) + c->next->size; c->next = c->next->next; }
}

extern "C" void *calloc(size_t n, size_t size)
{
    char *p = static_cast<char *>(malloc(n * size));
    if (p) for (size_t i = 0; i < n * size; i++) p[i] = 0;
    return p;
}

extern "C" void *realloc(void *p, size_t n)
{
    if (!p) return malloc(n);
    Block *b = static_cast<Block *>(p) - 1;
    if (b->size >= n) return p;
    char *q = static_cast<char *>(malloc(n));
    if (q) { for (size_t i = 0; i < b->size; i++) q[i] = static_cast<char *>(p)[i]; free(p); }
    return q;
}

void *operator new(size_t n) { void *p = malloc(n); if (!p) halt("out of memory"); return p; }
void *operator new[](size_t n) { return operator new(n); }
void operator delete(void *p) noexcept { free(p); }
void operator delete[](void *p) noexcept { free(p); }
void operator delete(void *p, size_t) noexcept { free(p); }
void operator delete[](void *p, size_t) noexcept { free(p); }

namespace std {
void __throw_bad_alloc() { halt("bad_alloc"); }
void __throw_bad_array_new_length() { halt("bad_array_new_length"); }
void __throw_length_error(const char *s) { halt(s); }
void __throw_logic_error(const char *s) { halt(s); }
void __throw_out_of_range(const char *s) { halt(s); }
void __throw_out_of_range_fmt(const char *s, ...) { halt(s); }
void __throw_invalid_argument(const char *s) { halt(s); }
void __throw_bad_function_call() { halt("bad_function_call"); }
} // namespace std

extern "C" void __cxa_pure_virtual() { halt("pure virtual call"); }

// Static constructors, from the linker script's .init_array.
extern "C" void (*__init_array_start[])();
extern "C" void (*__init_array_end[])();
extern "C" void run_constructors()
{
    for (auto f = __init_array_start; f != __init_array_end; f++) (*f)();
}
