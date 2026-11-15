// Display 13 combo PHY initialization after a power loss, against Linux's
// icl_combo_phys_init. Register values are the IdeaPad Slim 5 14IAH8's PHY A:
// as the firmware leaves it at boot, and as it reads after a system sleep.
#include "kern_display13_combo_phy.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
using namespace Display13ComboPHY;
struct IO {
    std::map<uint32_t,uint32_t> r;std::vector<uint32_t> writes;std::vector<uint32_t> unreadable;
    uint32_t read(uint32_t reg) {
        for(auto u:unreadable)if(u==reg)return UINT32_MAX;
        assert(r.count(reg));return r[reg];
    }
    void write(uint32_t reg,uint32_t v) {assert(r.count(reg));r[reg]=v;writes.push_back(reg);}
};
// PHY A after a system sleep (the TX/PCS group registers mirror lane 0).
static IO afterSleep(uint32_t dw3=0xc0608227) {
    IO io;const auto b=base(0);
    io.r[phyMisc(0)]=0x23000000;io.r[b+CompDW0]=0x00005f41;io.r[b+CompDW1]=0x81440404;io.r[b+CompDW3]=dw3;
    io.r[b+CompDW8]=0x000d0280;io.r[b+CompDW9]=0x0e9d3ebf;io.r[b+CompDW10]=0xf98721aa;io.r[b+ClDW5]=0x1204044b;
    io.r[b+TxDW8Lane0]=0x50033cfc;io.r[b+TxDW8Group]=0x50033cfc;io.r[b+PcsDW1Lane0]=0x1c000004;io.r[b+PcsDW1Group]=0x1c000004;
    return io;
}
int main() {
    unsigned cases=0;
    // The firmware's state at boot is initialized: nothing to do.
    {
        IO io;const auto b=base(0);
        io.r[phyMisc(0)]=0x23000000;io.r[b+CompDW0]=0x80005f2f;
        assert(enabled(io,0) && io.writes.empty());
        io.r[phyMisc(0)]=0x23800000;assert(!enabled(io,0));    // DE_IO_COMP_PWR_DOWN
        io.r[phyMisc(0)]=0x23000000;io.r[b+CompDW0]=0x00005f2f;assert(!enabled(io,0));
        ++cases;
    }
    // After a system sleep: Linux's sequence and values (0.85 V, dot0 on this
    // part, as PORT_COMP_DW3 reports), master PHY A.
    {
        auto io=afterSleep();const auto b=base(0);
        assert(!enabled(io,0) && init(io,0));
        assert(io.r[b+CompDW0]==0x80005f41 && io.r[b+CompDW9]==0x62AB67BB && io.r[b+CompDW10]==0x51914F96);
        assert(io.r[b+CompDW1]==0x81000400);                     // the firmware's value at boot
        assert(io.r[b+CompDW8]==0x010d0280);                     // IREFGEN, as at boot
        assert(io.r[b+ClDW5]==0x1204045b);                       // CL_POWER_DOWN_ENABLE, as at boot
        assert(io.r[b+TxDW8Group]==0xB0033cfc);                  // ODCC clock select, divide by 2
        assert(io.r[b+PcsDW1Group]==0x1c000004 && io.r[phyMisc(0)]==0x23000000);
        // Linux's order: PHY_MISC, TX_DW8, PCS_DW1, procmon, IREFGEN, COMP_INIT, CL_DW5.
        const std::vector<uint32_t> order={phyMisc(0),b+TxDW8Group,b+PcsDW1Group,b+CompDW1,b+CompDW9,b+CompDW10,b+CompDW8,b+CompDW0,b+ClDW5};
        assert(io.writes==order);
        ++cases;
    }
    // PHY B is not a master: no IREFGEN. PHY_MISC power-down is cleared.
    {
        IO io;const auto b=base(1);
        io.r[phyMisc(1)]=0x23800000;io.r[b+CompDW0]=0x00005f41;io.r[b+CompDW1]=0x81440404;io.r[b+CompDW3]=0xc0608227;
        io.r[b+CompDW8]=0x000d0280;io.r[b+CompDW9]=0;io.r[b+CompDW10]=0;io.r[b+ClDW5]=0x1204044b;
        io.r[b+TxDW8Lane0]=0x50033cfc;io.r[b+TxDW8Group]=0;io.r[b+PcsDW1Lane0]=0x1c300004;io.r[b+PcsDW1Group]=0;
        assert(init(io,1));
        assert(io.r[b+CompDW8]==0x000d0280 && io.r[phyMisc(1)]==0x23000000 && io.r[b+PcsDW1Group]==0x1c000004);
        for(auto w:io.writes)assert(w!=b+CompDW8);
        assert(base(1)==0x6C000 && phyMisc(1)==0x64C04);
        ++cases;
    }
    // Every voltage/process combination Linux knows, and its fallback.
    {
        const uint32_t dw3s[]={0U,1U<<24,(1U<<24)|(1U<<26),2U<<24,(2U<<24)|(1U<<26),(2U<<24)|(2U<<26)};
        const Procmon want[]={{0,0x62AB67BB,0x51914F96},{0,0x86E172C7,0x77CA5EAB},{0,0x93F87FE1,0x8AE871C5},
                              {0,0x98FA82DD,0x89E46DC1},{0x00440000,0x9A00AB25,0x8AE38FF1},{0,0x62AB67BB,0x51914F96}};
        for(unsigned i=0;i<6;++i) {
            const auto p=procmon(dw3s[i]|0xc0008227);
            assert(p.dw1==want[i].dw1 && p.dw9==want[i].dw9 && p.dw10==want[i].dw10);
        }
        auto io=afterSleep((2U<<24)|(1U<<26));assert(init(io,0));
        assert(io.r[base(0)+CompDW1]==0x81440400);  // bits 23:16 and 7:0 replaced
        ++cases;
    }
    // An unreadable register: no write at all.
    {
        auto io=afterSleep();io.unreadable.push_back(base(0)+CompDW3);
        assert(!init(io,0) && io.writes.empty());
        auto other=afterSleep();other.unreadable.push_back(phyMisc(0));
        assert(!init(other,0) && other.writes.empty() && !enabled(other,0));
        ++cases;
    }
    printf("display13_combo_phy: ok (%u cases)\n",cases);
    return 0;
}
