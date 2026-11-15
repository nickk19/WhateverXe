// Display 13 core registers set again after a system sleep, as Linux i915 sets
// them on resume. Values are the IdeaPad Slim 5 14IAH8's: as the firmware
// leaves them at boot, and as they read after a system sleep.
#include "kern_display13_core.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
using namespace Display13Core;
struct IO {
    std::map<uint32_t,uint32_t> r;std::vector<uint32_t> writes;
    uint32_t read(uint32_t reg) {auto i=r.find(reg);return i==r.end() ? UINT32_MAX : i->second;}
    void write(uint32_t reg,uint32_t v) {assert(r.count(reg));r[reg]=v;writes.push_back(reg);}
};
static IO afterSleep() {
    IO io;io.r[ChickenMisc2]=0;io.r[ErrorFatalMask]=0;io.r[ChickenDCPR1]=0x2008;
    io.r[pipeChicken(0)]=0x80;io.r[pipeChicken(1)]=0;io.r[pipeChicken(2)]=0;
    return io;
}
int main() {
    unsigned cases=0;
    const auto pipeA=[](uint32_t pipe){return pipe==0;};
    // After a sleep: each bit back, read-modify-write, pipe A only while the
    // other pipes are off; the firmware's values where it has only these bits.
    {
        auto io=afterSleep();const auto r=restore(io,pipeA);
        assert(io.r[ChickenMisc2]==0x40000000 && io.r[ErrorFatalMask]==0xFFFFFFFF);
        assert(io.r[ChickenDCPR1]==0xA008 && io.r[pipeChicken(0)]==0x40000080);
        assert(!io.r[pipeChicken(1)] && !io.r[pipeChicken(2)]);
        assert((io.writes==std::vector<uint32_t>{ChickenMisc2,ErrorFatalMask,ChickenDCPR1,pipeChicken(0)}));
        assert(r.written==0xF && !r.before[0] && r.after[0]==0x40000000 && r.before[3]==0x80 && r.after[3]==0x40000080);
        assert(r.before[4]==UINT32_MAX && r.after[5]==UINT32_MAX && complete(r));
        ++cases;
    }
    // The firmware's state at boot: nothing to write.
    {
        IO io;io.r[ChickenMisc2]=0x40000000;io.r[ErrorFatalMask]=0xFFFFFFFF;io.r[ChickenDCPR1]=0xA408;
        io.r[pipeChicken(0)]=0x40000080;io.r[pipeChicken(1)]=0x40000000;io.r[pipeChicken(2)]=0;
        const auto r=restore(io,[](uint32_t pipe){return pipe<2;});
        assert(io.writes.empty() && !r.written && complete(r) && io.r[ChickenDCPR1]==0xA408);
        ++cases;
    }
    // Other bits stay; every powered pipe gets its bit.
    {
        auto io=afterSleep();io.r[ChickenMisc2]=0x00000005;io.r[ChickenDCPR1]=0x00400000;io.r[pipeChicken(2)]=0x8000;
        const auto r=restore(io,[](uint32_t){return true;});
        assert(io.r[ChickenMisc2]==0x40000005 && io.r[ChickenDCPR1]==0x00408000);
        assert(io.r[pipeChicken(1)]==0x40000000 && io.r[pipeChicken(2)]==0x40008000 && r.written==0x3F);
        ++cases;
    }
    // Unreadable registers are not written; a write that does not stick is reported.
    {
        IO io;io.r[ErrorFatalMask]=0;
        const auto r=restore(io,[](uint32_t){return false;});
        assert((io.writes==std::vector<uint32_t>{ErrorFatalMask}) && r.before[0]==UINT32_MAX && r.written==2 && complete(r));
        struct Stuck : IO {void write(uint32_t reg,uint32_t) {writes.push_back(reg);}} stuck;
        stuck.r=afterSleep().r;
        const auto s=restore(stuck,pipeA);
        assert(s.written==0xF && !complete(s) && !s.after[0]);
        ++cases;
    }
    assert(CoreCount==3 && pipeChicken(1)==0x71038 && pipeChicken(2)==0x72038);
    printf("display13_core: ok (%u cases)\n",cases);
    return 0;
}
