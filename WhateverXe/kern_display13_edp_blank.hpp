// -wex-edp-blank (only together with -wex-edp-keep): display sleep for the
// kept firmware eDP link without running the donor's Doze -> Wake transition.
// That transition stops the link, runs a complete mode set and leaves OLED
// eDP 1.4 panels (Samsung ATNA family) dark. Instead, keep the pipe,
// transcoder and link running and only stop scanning out: Doze clears the
// primary plane and cursor enables (the pipe then emits its black background,
// an OLED emits no light), Wake restores them.
// System sleep stays the donor's: Off is entered through prepareToEnterSleep
// and left through prepareToExitSleep, which own IOGraphics' sleep/wake
// handshake, and the display hardware comes back reset.
// First implemented and verified on a Galaxy Book3 Ultra by vikenzo196
// (fork commit 4c2a262); the same idea as the "backlight only" DPMS of
// NootedGreen. Plane/cursor registers are latched by the following SURF/BASE write.
#ifndef kern_display13_edp_blank_hpp
#define kern_display13_edp_blank_hpp
#include <stdint.h>

namespace Display13EDPBlank {
constexpr uint32_t PlaneCtl=0x70180, PlaneSurf=0x7019C; // PLANE_CTL_1_A / PLANE_SURF_1_A
constexpr uint32_t CurCtl=0x70080, CurBase=0x70084;     // CUR_CTL_A / CUR_BASE_A
constexpr uint32_t PlaneEnable=0x80000000U;
constexpr uint32_t CursorMode=0x27; // MCURSOR_MODE_MASK; 0 disables the cursor.
// The scanout chain a blank keeps running: TRANSCONF_A, the panel DDI's
// DDI_BUF_CTL and the panel power sequencer's PP_STATUS.
constexpr uint32_t TransConf=0x70008, PPStatus=0xC7200;
constexpr uint32_t ddiBufCtl(uint32_t ddi) {return 0x64000+0x100*ddi;}
// Donor framebuffer power states.
constexpr uint32_t Off=0, Doze=1, Wake=2;

// Why a blank was released without a register write.
enum class Reason : uint32_t { None, SystemSleep, NotRunning, NotScannedOut };
struct State {
    bool blanked {};
    uintptr_t fb {};              // The framebuffer whose panel is blanked.
    uint32_t plane {}, cursor {}; // Values seen when blanking.
    uint32_t blanks {}, unblanks {}, releases {}, sleepReleases {};
    Reason lastRelease {};
};
enum class Action : uint32_t { Native, Blank, Unblank, Keep, Release };
struct Decision {
    Action action;
    Reason reason;
};

// The pipe, the panel's DDI and the panel power are on. A reset or unpowered
// display reads 0 (or all ones): after a system sleep or a hibernation the
// software state can still describe the adopted boot panel.
template<class IO> bool running(IO &io,uint32_t ddi,uint32_t (&seen)[3]) {
    seen[0]=io.read(TransConf);seen[1]=ddi<9 ? io.read(ddiBufCtl(ddi)) : UINT32_MAX;seen[2]=io.read(PPStatus);
    for(auto value:seen)if(value==UINT32_MAX)return false;
    return (seen[0]&0xC0000000U)==0xC0000000U && (seen[1]&0x80000000U) && (seen[2]&0x80000000U);
}

// eligible: the framebuffer's path is the adopted boot panel path, attached,
// and its latest mode-set decision kept the link (software state).
// scanout: pipe A still drives the adopted boot panel path (software state).
// live: the hardware agrees (running above).
// owner: the framebuffer is the one whose panel is blanked.
// current: the donor's recorded power state, requested: the new one.
inline Decision decide(bool eligible,bool scanout,bool live,bool owner,uint32_t current,uint32_t requested,bool blanked) {
    // Another framebuffer (an external display, an offline connector) never
    // touches the blanked panel: only the panel's own callback brings it back.
    if(blanked && !owner)return {Action::Native,Reason::None};
    // System sleep and its wake are the donor's, and a blank never spans them.
    if(requested==Off || current==Off)
        return blanked ? Decision{Action::Release,Reason::SystemSleep} : Decision{Action::Native,Reason::None};
    if(blanked) {
        // Nothing left to restore: pipe A no longer scans the panel out, or
        // the hardware stopped under the software state.
        if(!scanout)return {Action::Release,Reason::NotScannedOut};
        if(!live)return {Action::Release,Reason::NotRunning};
        // The panel's own wake never leaves it dark, kept link or not.
        if(requested==Wake)return {Action::Unblank,Reason::None};
        return {eligible && requested<Wake && current<=Wake ? Action::Keep : Action::Native,Reason::None};
    }
    if(!eligible || !live || requested>Wake || current>Wake)return {Action::Native,Reason::None};
    if(requested==Doze)return {Action::Blank,Reason::None};
    // Already awake: nothing to do. A wake from a native doze stays native.
    return {current==Wake ? Action::Keep : Action::Native,Reason::None};
}

template<class IO> void blank(IO &io,State &s) {
    s.plane=io.read(PlaneCtl);s.cursor=io.read(CurCtl);
    io.write(CurCtl,s.cursor&~CursorMode);io.write(CurBase,io.read(CurBase));
    io.write(PlaneCtl,s.plane&~PlaneEnable);io.write(PlaneSurf,io.read(PlaneSurf));
    s.blanked=true;++s.blanks;
}
// Restore only the enables: the donor may legitimately reprogram format,
// tiling or cursor size while the panel is dark (a later flip or mode set).
template<class IO> void unblank(IO &io,State &s) {
    const auto plane=io.read(PlaneCtl);
    io.write(PlaneCtl,plane|(s.plane&PlaneEnable));io.write(PlaneSurf,io.read(PlaneSurf));
    const auto cursor=io.read(CurCtl);
    if(!(cursor&CursorMode) && (s.cursor&CursorMode)) {
        io.write(CurCtl,(cursor&~CursorMode)|(s.cursor&CursorMode));io.write(CurBase,io.read(CurBase));
    }
    s.blanked=false;s.fb=0;++s.unblanks;
}
// Drop the blank without writing a register: the donor's own transition
// follows, and its display disable or mode set reprograms the plane.
inline void release(State &s,Reason reason) {
    s.blanked=false;s.fb=0;++s.releases;s.lastRelease=reason;
    if(reason==Reason::SystemSleep)++s.sleepReleases;
}
}
#endif
