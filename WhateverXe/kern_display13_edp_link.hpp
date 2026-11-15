// The internal eDP panel's link training, brought in line with the firmware's
// GOP driver (IntelGopDriverAdlP) and Linux i915 (intel_dp_link_training.c,
// tgl_ddi_pre_enable_dp) where the donor's AppleIntelPort::linkTraining
// differs from both. The donor still runs the training; WEX changes its view
// of the panel's receiver caps, adds the DPCD writes Linux makes before
// training and orders the training pattern writes as the GOP and Linux do.
//
// Training delays. getDPCDInfo reads EDP_CONFIGURATION_CAP and
// TRAINING_AUX_RD_INTERVAL as a pair (0x00D, two bytes) and setupPort takes
// the whole interval byte as a count of 4 ms. In a DP 1.4 receiver bit 7 of
// that byte is EXTENDED_RECEIVER_CAPABILITY_FIELD_PRESENT, not part of the
// interval: the Samsung ATNA40CT01 reports 0x80, the donor computes 512 ms,
// clamps it to 65 ms and then waits it twice (IOSleep plus IODelay), so every
// clock recovery and equalization step waits about 130 ms instead of the
// 100 us and 400 us of an interval of 0. In reads that start at 0x00D or 0x00E
// the donor sees bits 6:0 only. The 16-byte receiver caps keep the flag: the
// donor reads it there to fetch the extended caps at 0x2200.
//
// ASSR. The donor enables the alternate scrambler seed reset whenever the
// panel advertises it (0x00D bit 0). Linux never enables it and the firmware
// leaves it off; a panel that advertises TPS4 does not see it offered.
//
// Equalization pattern. The GOP equalizes with TPS3 whenever the panel
// supports it (0x002 bit 6) and with TPS4 only otherwise; the donor, like
// Linux, prefers TPS4 (0x003 bit 7). The donor does not see TPS4 next to TPS3,
// so it equalizes with TPS3 as the firmware does.
//
// Before the link configuration (the donor's LINK_BW_SET write) Linux wakes
// the receiver (SET_POWER 0x600 = D0) and selects 8b/10b channel coding
// (MAIN_LINK_CHANNEL_CODING_SET 0x108 = 1); the donor writes neither.
//
// Training pattern order. The GOP and Linux (intel_dp_set_link_train) put the
// pattern on the main link first and only then set TRAINING_PATTERN_SET in the
// sink, Linux in one write with TRAINING_LANEx_SET, and Linux enters clock
// recovery once the DDI buffer has left idle (intel_ddi_prepare_link_retrain).
// The donor sets TRAINING_PATTERN_SET first, then the source pattern and the
// drive levels, enables the buffer on its first clock recovery pass and writes
// TRAINING_LANEx_SET. With that order the ATNA40CT01 recovered the clock only
// 130 to 265 ms after the pattern was set, and lost it at equalization. WEX
// holds the donor's TRAINING_PATTERN_SET and sends it with the next
// TRAINING_LANEx_SET, as Linux does. Any other access to the panel first
// delivers the held write; a new TRAINING_PATTERN_SET replaces it.
#ifndef kern_display13_edp_link_hpp
#define kern_display13_edp_link_hpp
#include <stdint.h>

namespace Display13EDPLink {
constexpr uint32_t ReceiverCaps=0x000, ExtendedCaps=0x2200;
constexpr uint32_t MaxLaneCount=0x002, MaxDownspread=0x003, ConfigCap=0x00D, AuxReadInterval=0x00E;
constexpr uint32_t LinkBWSet=0x100, TrainingPatternSet=0x102, TrainingLaneSet=0x103, ChannelCoding=0x108, SetPower=0x600;
constexpr uint8_t TPS3=0x40, TPS4=0x80, ASSR=0x01, ExtendedCapsPresent=0x80;
constexpr uint8_t PowerD0=0x01, Coding8b10b=0x01, PatternMask=0x0F, PatternTPS1=0x01;
constexpr uint32_t MaxLanes=4;

struct State {
    bool tps3 {};               // The panel advertised TPS3 in either copy of the caps.
    bool tps4 {};               // The panel advertised TPS4 in either copy of the caps.
    uint32_t assrHidden {};     // Reads ASSR was removed from.
    uint32_t intervalMasked {}; // Reads the interval's bit 7 was removed from.
    uint32_t tps4Hidden {};     // Reads TPS4 was removed from.
};

// A completed DPCD read of the panel, before the donor sees it.
inline void view(State &s,uint32_t address,uint8_t *data,uint32_t length) {
    if(!data || !length)return;
    const auto at=[&](uint32_t reg)->uint8_t * {return reg>=address && reg-address<length ? data+(reg-address) : nullptr;};
    const uint32_t bases[]={ReceiverCaps,ExtendedCaps};
    for(const auto base:bases) {
        if(auto b=at(base+MaxLaneCount))if(*b&TPS3)s.tps3=true;
        if(auto b=at(base+MaxDownspread))if(*b&TPS4)s.tps4=true;
    }
    bool assr=false,tps4=false;
    for(const auto base:bases) {
        auto b=at(base+ConfigCap);
        if(b && s.tps4 && (*b&ASSR)) {*b&=~ASSR;assr=true;}
        auto d=at(base+MaxDownspread);
        if(d && s.tps3 && (*d&TPS4)) {*d&=~TPS4;tps4=true;}
    }
    if(assr)++s.assrHidden;
    if(tps4)++s.tps4Hidden;
    if(address==ConfigCap || address==AuxReadInterval) {
        auto b=at(AuxReadInterval);
        if(b && (*b&ExtendedCapsPresent)) {*b&=~ExtendedCapsPresent;++s.intervalMasked;}
    }
}

// A donor write to the panel about to be sent: true when it sets LINK_BW_SET,
// so the link configuration starts and the writes Linux makes before it are due.
inline bool startsLinkConfig(uint32_t address,const uint8_t *data,uint32_t length) {
    return data && length && LinkBWSet>=address && LinkBWSet-address<length;
}

// The training pattern order.
struct Order {
    bool held {};
    uint8_t pattern {};         // The held TRAINING_PATTERN_SET.
    uint32_t holds {};          // TRAINING_PATTERN_SET writes held.
    uint32_t joined {};         // Sent with TRAINING_LANEx_SET.
    uint32_t delivered {};      // Sent alone before another access.
    uint32_t replaced {};       // Superseded by another TRAINING_PATTERN_SET write.
};
enum class Step {
    Pass,                       // Send the donor's access as it is.
    Hold,                       // Keep the donor's TRAINING_PATTERN_SET; report it written.
    Join,                       // Write TRAINING_PATTERN_SET followed by the donor's TRAINING_LANEx_SET.
    Deliver,                    // Write the held TRAINING_PATTERN_SET alone, then the donor's access.
};
// A donor write to the panel about to be sent.
inline Step write(Order &o,uint32_t address,const uint8_t *data,uint32_t length) {
    if(address==TrainingPatternSet && length==1 && data && (data[0]&PatternMask)) {
        if(o.held)++o.replaced;
        o.held=true;o.pattern=data[0];++o.holds;
        return Step::Hold;
    }
    if(!o.held)return Step::Pass;
    o.held=false;
    if(address==TrainingLaneSet && data && length && length<=MaxLanes) {++o.joined;return Step::Join;}
    if(address<=TrainingPatternSet && TrainingPatternSet-address<length) {++o.replaced;return Step::Pass;}
    ++o.delivered;return Step::Deliver;
}
// A donor read of the panel about to be sent.
inline Step read(Order &o) {
    if(!o.held)return Step::Pass;
    o.held=false;++o.delivered;return Step::Deliver;
}
// The write Join sends: the held TRAINING_PATTERN_SET, then the lanes.
// Returns its length.
inline uint32_t join(uint8_t pattern,const uint8_t *lanes,uint32_t length,uint8_t (&out)[1+MaxLanes]) {
    if(!lanes || !length || length>MaxLanes)return 0;
    out[0]=pattern;
    for(uint32_t i=0;i<length;++i)out[1+i]=lanes[i];
    return 1+length;
}
// Clock recovery (TPS1) starts once the DDI buffer is active.
inline bool clockRecovery(uint8_t pattern) {return (pattern&PatternMask)==PatternTPS1;}

// DDI_BUF_CTL: enabled, and idle until the buffer is active.
constexpr uint32_t BufferEnable=1U<<31, BufferIdle=1U<<7, BufferWaitUs=10000, BufferPollUs=10;
constexpr uint32_t bufferControl(uint32_t ddi) {return 0x64000+0x100*ddi;}
// intel_wait_ddi_buf_active: poll an enabled buffer until it leaves idle, at
// most 10 ms. Returns the microseconds waited, BufferWaitUs on a timeout and
// 0 for a buffer that is not enabled or already active.
template<class Read,class Delay> uint32_t waitBufferActive(Read read,Delay delay,uint32_t ddi) {
    uint32_t waited=0;
    for(;;) {
        const auto v=read(bufferControl(ddi));
        if(v==UINT32_MAX || !(v&BufferEnable) || !(v&BufferIdle) || waited>=BufferWaitUs)return waited;
        delay(BufferPollUs);waited+=BufferPollUs;
    }
}
}
#endif
