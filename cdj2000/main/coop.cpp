// Round-robin cooperative tasks (see coop.h).
#include "coop.h"
#include "net_hw.h"
#include "tmu.h"

namespace {
struct Ctx { uint32_t r[8], pr, mach, macl, gbr, fpscr; float fr[4]; };
static_assert(sizeof(Ctx) == 68, "coop_switch's layout");
struct Task { Ctx ctx; bool live; const char *name; };
constexpr int kMax = 4;
Task tasks[kMax] = {{{}, true, "main"}};   // task 0 is whoever called first: main()
int ntasks = 1, cur = 0;
alignas(8) uint8_t stacks[kMax - 1][32 * 1024];
}

extern "C" void coop_switch(Ctx *from, Ctx *to);
extern "C" void coop_trampoline();

extern "C" void coop_spawn(void (*fn)(void *), void *arg, uint32_t, const char *name)
{
    if (ntasks >= kMax) return;
    Task &t = tasks[ntasks];
    t = Task{};
    t.live = true;
    t.name = name;
    t.ctx.r[0] = reinterpret_cast<uint32_t>(fn);
    t.ctx.r[1] = reinterpret_cast<uint32_t>(arg);
    t.ctx.r[7] = reinterpret_cast<uint32_t>(stacks[ntasks - 1] + sizeof stacks[0]);   // r15
    t.ctx.pr = reinterpret_cast<uint32_t>(&coop_trampoline);
    t.ctx.fpscr = 0x00080000;
    ntasks++;
}

extern "C" void coop_exit()
{
    tasks[cur].live = false;
    for (;;) coop_yield();
}

extern "C" void coop_yield()
{
    (void)tmu_us64();               // keep the clock's wrap count current
    net_poll();
    int next = cur;
    do { next = (next + 1) % ntasks; } while (!tasks[next].live && next != cur);
    if (next == cur) return;
    const int from = cur;
    cur = next;
    coop_switch(&tasks[from].ctx, &tasks[next].ctx);
}
