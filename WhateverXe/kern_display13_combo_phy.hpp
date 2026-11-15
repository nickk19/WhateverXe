// Display 13 combo PHY initialization after the display lost power, as Linux
// i915 does it (intel_combo_phy.c, icl_combo_phys_init, display version 12+).
// The firmware initializes the combo PHYs at boot. A system sleep powers the
// display off and the PHYs come back uninitialized: PORT_COMP_DW0 COMP_INIT
// clear, process monitor references, IREFGEN, the ODCC/DCC settings and
// CL_POWER_DOWN_ENABLE at their reset values. The donor's restorePowerWellsState,
// which re-enables them through enableDisplayEngine/enableComboPhy, is replaced
// by WEX's Display 13 restore, so on the eDP/DP path nothing initialized them;
// the DSI path restores PHY A from its own boot image.
#ifndef kern_display13_combo_phy_hpp
#define kern_display13_combo_phy_hpp
#include <stdint.h>

namespace Display13ComboPHY {
constexpr uint32_t Count=2; // PHY A (DDI A) and PHY B (DDI B).
constexpr uint32_t base(uint32_t phy) {return phy ? 0x6C000 : 0x162000;}
constexpr uint32_t phyMisc(uint32_t phy) {return 0x64C00+4*phy;}
constexpr uint32_t CompDW0=0x100, CompDW1=0x104, CompDW3=0x10C, CompDW8=0x120, CompDW9=0x124, CompDW10=0x128;
constexpr uint32_t ClDW5=0x014, TxDW8Group=0x6A0, TxDW8Lane0=0x8A0, PcsDW1Group=0x604, PcsDW1Lane0=0x804;
constexpr uint32_t CompInit=1U<<31, IRefGen=1U<<24, ClPowerDownEnable=1U<<4, DeIOCompPowerDown=1U<<23;
constexpr uint32_t OdccClkSel=1U<<31, OdccDivMask=3U<<29, OdccDiv2=1U<<29, DccModeMask=3U<<20, RunDccOnce=0;
constexpr uint32_t ProcessMask=7U<<26, VoltageMask=3U<<24;

// Process monitor reference values by PORT_COMP_DW3 voltage and process.
struct Procmon {uint32_t dw1,dw9,dw10;};
inline Procmon procmon(uint32_t dw3) {
    switch(dw3&(ProcessMask|VoltageMask)) {
    case 1U<<24: return {0x00000000,0x86E172C7,0x77CA5EAB};             // 0.95 V dot0
    case (1U<<24)|(1U<<26): return {0x00000000,0x93F87FE1,0x8AE871C5};  // 0.95 V dot1
    case 2U<<24: return {0x00000000,0x98FA82DD,0x89E46DC1};             // 1.05 V dot0
    case (2U<<24)|(1U<<26): return {0x00440000,0x9A00AB25,0x8AE38FF1};  // 1.05 V dot1
    default: return {0x00000000,0x62AB67BB,0x51914F96};                 // 0.85 V dot0 (and unknown)
    }
}
// PHY A drives the compensation resistors for B and C.
constexpr bool master(uint32_t phy) {return phy==0;}

// icl_combo_phy_enabled: powered up and compensated.
template<class IO> bool enabled(IO &io,uint32_t phy) {
    const auto misc=io.read(phyMisc(phy)),comp=io.read(base(phy)+CompDW0);
    if(misc==UINT32_MAX || comp==UINT32_MAX)return false;
    return !(misc&DeIOCompPowerDown) && (comp&CompInit);
}

// icl_combo_phys_init for one PHY. Returns whether the PHY reads back
// initialized; false without a write when a register is unreadable.
template<class IO> bool init(IO &io,uint32_t phy) {
    const auto b=base(phy);
    const uint32_t regs[]={phyMisc(phy),b+TxDW8Lane0,b+PcsDW1Lane0,b+CompDW1,b+CompDW3,b+CompDW8,b+CompDW0,b+ClDW5};
    for(auto reg:regs)if(io.read(reg)==UINT32_MAX)return false;
    io.write(phyMisc(phy),io.read(phyMisc(phy))&~DeIOCompPowerDown);
    io.write(b+TxDW8Group,(io.read(b+TxDW8Lane0)&~OdccDivMask)|OdccClkSel|OdccDiv2);
    io.write(b+PcsDW1Group,(io.read(b+PcsDW1Lane0)&~DccModeMask)|RunDccOnce);
    const auto ref=procmon(io.read(b+CompDW3));
    io.write(b+CompDW1,(io.read(b+CompDW1)&~0x00FF00FFU)|ref.dw1);
    io.write(b+CompDW9,ref.dw9);
    io.write(b+CompDW10,ref.dw10);
    if(master(phy))io.write(b+CompDW8,io.read(b+CompDW8)|IRefGen);
    io.write(b+CompDW0,io.read(b+CompDW0)|CompInit);
    io.write(b+ClDW5,io.read(b+ClDW5)|ClPowerDownEnable);
    return enabled(io,phy);
}
}
#endif
