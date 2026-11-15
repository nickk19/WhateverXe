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
//
// Brightness over AUX. The Intel TCON brightness interface (0x340-0x35F) is
// Intel's: the panel offers it to a source that wrote Intel's OUI (00 aa 01)
// to SOURCE_OUI (0x300). The GOP writes the OUI, waits until the panel
// advertises the interface (0x340 version 1 or later, 0x341 bit 4 nits,
// 0x342 bit 0 SDR brightness over AUX), turns it on (0x344 bit 4,
// INTEL_EDP_HDR_TCON_BRIGHTNESS_AUX_ENABLE) and sets the level in nits at
// 0x354. Linux writes the OUI before every SET_POWER D0 and does the rest when
// it enables the backlight (intel_dp_init_source_oui,
// intel_dp_aux_hdr_enable_backlight), and writes the OUI only when the panel
// does not have it already, since writing it can clear the panel's state.
// The donor writes Apple's source identification there and drives the PWM
// only. A panel that lost power (a system sleep) comes back with 0x344 = 0,
// ignores the level at 0x354 and stays dark.
// When the firmware left the interface on under Intel's OUI, WEX keeps
// Intel's OUI on the panel: before the link configuration, as Linux, and in
// place of the donor's source identification. After the panel trains it waits
// for the interface as the GOP and turns it back on; a level that came back 0
// gets the last one read from the panel, or the firmware's.
// Linux also sets SDP override (0x344 bit 7) when the panel offers SDP
// colorimetry (0x341 bit 6) and the source has the gamut metadata DIP (Gen11
// and later), and clears it otherwise (intel_dp_aux_fill_hdr_tcon_params).
// WEX sets 0x344 after the panel trains as Linux does. The ATNA40CT01 offers
// it and powers up with 0x344 = 0x90; the GOP leaves 0x10.
//
// Link rate and depth with -wex-edp-keep. The donor trains the panel at the
// fastest rate it lists and at the depth of its EDID (the ATNA40CT01 at
// 5.4 Gbps and 10 bpc); a kept link must be the one the firmware runs (the
// ATNA40CT01 at 2.7 Gbps and 8 bpc). With -wex-edp-keep WEX shows the donor
// the panel as the firmware runs it: the rates it lists capped at the
// firmware's (Display13EDPKeep::capLinkCaps) and the depth of its EDID and
// mode at the firmware's; HBR and 8 bpc when the firmware's are not known.
// Without it the donor keeps its own rate and depth.
// The firmware, like Linux, selects an eDP 1.4 panel's rate with
// LINK_RATE_SET (0x115), an index into SUPPORTED_LINK_RATES (0x010, eight
// little-endian entries in 200 kHz units, ascending, 0 ends the list); the
// donor only writes LINK_BW_SET (0x100). The ATNA40CT01 follows LINK_RATE_SET:
// the firmware leaves LINK_BW_SET 0x14 next to LINK_RATE_SET 3 and the link
// runs at 2.7 Gbps (DPLL0 at 1350 MHz). The firmware's depth is the one its
// transcoder sends to the panel's DDI (TRANS_DDI_FUNC_CTL) at the boot
// handoff.
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

// Brightness over AUX.
constexpr uint32_t SourceOUI=0x300, SourceOUILength=3, IntelCaps=0x340, IntelControl=0x344, IntelNits=0x354;
constexpr uint8_t IntelOUI[SourceOUILength]={0x00,0xAA,0x01};
constexpr uint8_t IntelNitsCap=0x10, IntelSdrAuxCap=0x01, IntelAuxEnable=0x10;
constexpr uint8_t IntelSdpColorimetryCap=0x40, IntelSdpOverrideAux=0x80; // 0x341 bit 6, 0x344 bit 7
constexpr uint32_t CapsWaitMs=200;  // The GOP's wait for the interface, 1 ms apart.
struct Backlight {
    bool known {};              // The firmware's state was read.
    bool firmwareAux {};        // The firmware left brightness over AUX on, under Intel's OUI, with the caps for it.
    bool firmwareOUI {};        // The firmware left Intel's OUI.
    uint8_t firmwareControl {}; // 0x344 as the firmware left it.
    uint16_t firmwareNits {};   // 0x354/0x355 as the firmware left it.
    uint16_t lastNits {};       // The last level other than 0 read from the panel since.
};
inline bool intelOUI(const uint8_t *oui) {return oui && oui[0]==IntelOUI[0] && oui[1]==IntelOUI[1] && oui[2]==IntelOUI[2];}
inline bool interface(const uint8_t *caps) {return caps[0]>=1 && (caps[1]&IntelNitsCap) && (caps[2]&IntelSdrAuxCap);}
// The firmware's state: 0x300-0x302, 0x340-0x344 and 0x354-0x355 at the first reads of the panel.
inline void firmware(Backlight &b,const uint8_t (&oui)[SourceOUILength],const uint8_t (&caps)[5],const uint8_t (&nits)[2]) {
    b.known=true;b.firmwareOUI=intelOUI(oui);b.firmwareControl=caps[4];b.firmwareNits=uint16_t(nits[0]|nits[1]<<8);
    b.firmwareAux=b.firmwareOUI && interface(caps) && (caps[4]&IntelAuxEnable);
}
// A completed read of the panel: the level it reports, while it has one.
inline void level(Backlight &b,uint32_t address,const uint8_t *data,uint32_t length) {
    if(!b.firmwareAux || !data || address>IntelNits || IntelNits+1-address>=length)return;
    const uint16_t nits=uint16_t(data[IntelNits-address]|data[IntelNits+1-address]<<8);
    if(nits)b.lastNits=nits;
}
// A donor write to the panel that covers SOURCE_OUI: kept off the panel while
// WEX keeps Intel's OUI there.
inline bool replacesOUI(const Backlight &b,uint32_t address,uint32_t length) {
    return b.firmwareAux && length && address<SourceOUI+SourceOUILength && address+length>SourceOUI;
}
// After the panel trained, with 0x344, the level and 0x341 as the panel
// reports them: the level to write first (Linux's four-byte write, delay and
// steps 0), then 0x344 with brightness over AUX on and SDP override as Linux
// sets it.
struct Restore {bool level {};bool control {};uint8_t nits[4] {};uint8_t controlValue {};};
inline Restore restore(const Backlight &b,uint8_t control,uint16_t nits,uint8_t caps1=0) {
    Restore r;
    if(!b.firmwareAux)return r;
    uint8_t wanted=uint8_t((control|IntelAuxEnable)&~IntelSdpOverrideAux);
    if(caps1&IntelSdpColorimetryCap)wanted|=IntelSdpOverrideAux;
    if(wanted==control)return r;
    r.control=true;r.controlValue=wanted;
    const uint16_t level=b.lastNits ? b.lastNits : b.firmwareNits;
    if(!(control&IntelAuxEnable) && !nits && level) {r.level=true;r.nits[0]=uint8_t(level);r.nits[1]=uint8_t(level>>8);}
    return r;
}

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

// The firmware's link format on the panel, as -wex-edp-keep shows it to the donor.
constexpr uint32_t SupportedLinkRates=0x010, RateEntries=8, LinkRateSet=0x115;
constexpr uint32_t BWUnit=1350; // One LINK_BW_SET step, 0.27 Gbps, in the table's 200 kHz units.
struct Firmware {
    uint32_t depth {};          // Bits per component the firmware sends, 0 unknown.
    uint32_t func {UINT32_MAX}; // Its TRANS_DDI_FUNC_CTL at the boot handoff.
    uint32_t rate {};           // The rate it runs, in 200 kHz units, 0 unknown.
    uint8_t bw {};              // That rate as a LINK_BW_SET code, 0 if the donor has none.
    uint8_t linkBW {}, rateSet {}; // LINK_BW_SET and LINK_RATE_SET as the firmware left them.
    bool read {};               // The panel's link settings were read (once, at the donor's first read).
};
inline bool standardBW(uint32_t code) {return code==0x06 || code==0x0A || code==0x14 || code==0x1E;}
// The rate the firmware runs the panel at, in 200 kHz units, 0 if unknown.
// A valid index into the panel's table is the rate select method when
// LINK_BW_SET is 0 and, when it is not, a later choice than a stale
// LINK_BW_SET; index 0 next to a LINK_BW_SET is that LINK_BW_SET's rate. A
// table that does not ascend is no table.
inline uint32_t firmwareRate(const uint8_t *table,uint8_t linkBW,uint8_t rateSet) {
    if(table && (rateSet || !linkBW)) {
        uint32_t entries=0,previous=0;
        for(;entries<RateEntries;++entries) {
            const uint32_t rate=table[2*entries]|(uint32_t(table[2*entries+1])<<8);
            if(!rate)break;
            if(rate<=previous) {entries=0;break;}
            previous=rate;
        }
        if(rateSet<entries)return table[2*rateSet]|(uint32_t(table[2*rateSet+1])<<8);
        if(!linkBW)return 0;
    }
    return standardBW(linkBW) ? linkBW*BWUnit : 0;
}
// That rate as a LINK_BW_SET code; 0 when the donor cannot set it.
inline uint8_t firmwareBW(uint32_t rate) {
    return rate && !(rate%BWUnit) && standardBW(rate/BWUnit) ? uint8_t(rate/BWUnit) : 0;
}
// The depth a transcoder sends to the panel's DDI: enabled, that DDI's port
// select (bits 30:27, DDI + 1), DisplayPort SST (bits 26:24 = 2), bits per
// component from bits 22:20. 0 when the transcoder does not drive it so.
inline uint32_t firmwareDepth(uint32_t func,uint32_t ddi) {
    static constexpr uint32_t bpc[8]={8,10,6,12,0,0,0,0};
    if(func==UINT32_MAX || !(func&0x80000000U) || ((func>>27)&0xF)!=ddi+1 || ((func>>24)&7)!=2)return 0;
    return bpc[(func>>20)&7];
}
// The format WEX shows the donor: with -wex-edp-keep the firmware's rate and
// depth when both are known, HBR and 8 bpc otherwise; without it the panel's
// own (0).
inline uint8_t viewBW(const Firmware &f,bool keep) {return !keep ? 0 : f.bw && f.depth ? f.bw : 0x0A;}
inline uint32_t viewDepth(const Firmware &f,bool keep) {return !keep ? 0 : f.bw && f.depth ? f.depth : 8;}
}
#endif
