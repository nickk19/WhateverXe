// Display 13 core registers that a system sleep resets and the donor never
// programs, set again as Linux i915 sets them on resume. The firmware leaves
// them set at boot; after a sleep they read 0.
//  - CHICKEN_MISC_2 (0x42084) bit 30, CHICKEN_MISC_DISABLE_DPT: Linux sets it
//    on display 13 when it does not use a display page table
//    (intel_dpt_configure). The donor never builds one. With the bit clear,
//    tiled planes fetch through a DPT that does not exist: pipes run, flips
//    land, no underrun, and the output is black.
//  - XELPD_DISPLAY_ERR_FATAL_MASK (0x4421C): all ones (icl_display_core_init,
//    Wa_14011503030).
//  - CHICKEN_DCPR_1 (0x46430) bit 15, DISABLE_FLR_SRC: set with power well 1
//    on Alder Lake-P (hsw_power_well_enable, Wa_16013190616).
//  - PIPE_CHICKEN (0x70038 + 0x1000 * pipe) bit 30,
//    UNDERRUN_RECOVERY_DISABLE_ADLP: "Underrun recovery must always be
//    disabled on display 13+" (icl_set_pipe_chicken), for each powered pipe.
// Each is set before any plane scans out, read-modify-write, and only where it
// is not set already.
#ifndef kern_display13_core_hpp
#define kern_display13_core_hpp
#include <stdint.h>

namespace Display13Core {
constexpr uint32_t ChickenMisc2=0x42084, DisableDPT=1U<<30;
constexpr uint32_t ErrorFatalMask=0x4421C, AllErrors=0xFFFFFFFFU;
constexpr uint32_t ChickenDCPR1=0x46430, DisableFLRSource=1U<<15;
constexpr uint32_t UnderrunRecoveryDisable=1U<<30;
constexpr uint32_t Pipes=3;
constexpr uint32_t pipeChicken(uint32_t pipe) {return 0x70038+0x1000*pipe;}

struct Register {uint32_t reg, bits;};
constexpr Register core[]={{ChickenMisc2,DisableDPT},{ErrorFatalMask,AllErrors},{ChickenDCPR1,DisableFLRSource}};
constexpr uint32_t CoreCount=sizeof(core)/sizeof(core[0]);

// Values before and after, UINT32_MAX for a register not read (unreadable, or
// the pipe not powered).
struct Result {
    uint32_t before[CoreCount+Pipes] {}, after[CoreCount+Pipes] {};
    uint32_t written {};        // Bit i: entry i was written.
};

// Set the bits where they are missing. powered(pipe) says whether a pipe's
// registers can be accessed.
template<class IO,class Powered> Result restore(IO &io,Powered powered) {
    Result r;
    const auto set=[&](uint32_t i,uint32_t reg,uint32_t bits) {
        const auto v=io.read(reg);r.before[i]=r.after[i]=v;
        if(v==UINT32_MAX || (v&bits)==bits)return;
        io.write(reg,v|bits);r.written|=1U<<i;r.after[i]=io.read(reg);
    };
    for(uint32_t i=0;i<CoreCount;++i)set(i,core[i].reg,core[i].bits);
    for(uint32_t pipe=0;pipe<Pipes;++pipe) {
        const auto i=CoreCount+pipe;
        if(powered(pipe))set(i,pipeChicken(pipe),UnderrunRecoveryDisable);
        else r.before[i]=r.after[i]=UINT32_MAX;
    }
    return r;
}
// Every register read back with its bits, or left alone as unreadable.
inline bool complete(const Result &r) {
    for(uint32_t i=0;i<CoreCount+Pipes;++i) {
        const auto bits=i<CoreCount ? core[i].bits : UnderrunRecoveryDisable;
        if(r.after[i]!=UINT32_MAX && (r.after[i]&bits)!=bits)return false;
    }
    return true;
}
}
#endif
