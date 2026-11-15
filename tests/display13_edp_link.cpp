// The panel's link training as the firmware and Linux run it: the donor's view
// of the receiver caps (training interval, ASSR, equalization pattern), the
// start of its link configuration and the order of its training pattern writes.
// Bytes are the Samsung ATNA40CT01's (IdeaPad Slim 5 14IAH8) as the donor
// reads them.
#include "kern_display13_edp_link.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace Display13EDPLink;
static const uint8_t panelCaps[16]={0x14,0x14,0xc2,0xc1,0,0,1,0xc0,2,0,0,0,0x3f,0x09,0x80,0};
int main() {
    unsigned cases=0;
    // getDPCDInfo: the 16-byte caps keep the extended caps flag at 0x00E and
    // show TPS3 without TPS4, the pair at 0x00D shows interval 0 and no ASSR,
    // and so does the interval alone.
    {
        State s;uint8_t caps[16],ext[16],pair[2]={0x09,0x80},interval[1]={0x80};
        memcpy(caps,panelCaps,16);memcpy(ext,panelCaps,16);
        view(s,ReceiverCaps,caps,16);view(s,ExtendedCaps,ext,16);
        assert(caps[0xE]==0x80 && ext[0xE]==0x80 && caps[0xD]==0x08 && ext[0xD]==0x08);
        assert(caps[2]==0xc2 && caps[3]==0x41 && ext[3]==0x41 && s.tps3 && s.tps4 && s.tps4Hidden==2);
        for(unsigned i=0;i<16;++i)if(i!=3 && i!=0xD)assert(caps[i]==panelCaps[i] && ext[i]==panelCaps[i]);
        view(s,ConfigCap,pair,2);
        assert(pair[0]==0x08 && pair[1]==0x00);
        view(s,AuxReadInterval,interval,1);
        assert(interval[0]==0x00 && s.assrHidden==3 && s.intervalMasked==2 && s.tps4);
        ++cases;
    }
    // Only bit 7 of the interval goes: a real interval code (4 ms units) stays.
    {
        State s;uint8_t pair[2]={0x00,0x84},plain[2]={0x00,0x02};
        view(s,ConfigCap,pair,2);view(s,ConfigCap,plain,2);
        assert(pair[1]==0x04 && plain[1]==0x02 && s.intervalMasked==1);
        ++cases;
    }
    // Without TPS4 ASSR stays; only bit 0 of 0x00D goes when it does.
    {
        State s;uint8_t caps[16],pair[2]={0x09,0x80};memcpy(caps,panelCaps,16);caps[3]=0x41;
        view(s,ReceiverCaps,caps,16);view(s,ConfigCap,pair,2);
        assert(!s.tps4 && !s.assrHidden && caps[0xD]==0x09 && pair[0]==0x09 && pair[1]==0x00);
        State t;uint8_t all[16];memcpy(all,panelCaps,16);all[0xD]=0xFF;
        view(t,ReceiverCaps,all,16);assert(all[0xD]==0xFE && all[3]==0x41);
        ++cases;
    }
    // The GOP's equalization pattern: TPS4 goes only next to TPS3, from either
    // copy, in any order; a panel without TPS3 keeps TPS4.
    {
        State s;uint8_t caps[16];memcpy(caps,panelCaps,16);caps[2]=0x82;
        view(s,ReceiverCaps,caps,16);
        assert(caps[3]==0xc1 && s.tps4 && !s.tps3 && !s.tps4Hidden && caps[0xD]==0x08);
        State t;uint8_t lanes[1]={0xc2},spread[1]={0xc1},ext[16];memcpy(ext,panelCaps,16);ext[2]=0x82;
        view(t,MaxLaneCount,lanes,1);view(t,MaxDownspread,spread,1);
        assert(spread[0]==0x41 && t.tps4Hidden==1);
        view(t,ExtendedCaps,ext,16);assert(ext[3]==0x41 && t.tps4Hidden==2);  // TPS3 already seen
        ++cases;
    }
    // TPS4 in either copy, in any order, holds for both.
    {
        State s;uint8_t ext[16],caps[16];memcpy(ext,panelCaps,16);memcpy(caps,panelCaps,16);caps[3]=0x41;
        view(s,ExtendedCaps,ext,16);view(s,ReceiverCaps,caps,16);
        assert(s.tps4 && s.assrHidden==2 && ext[0xD]==0x08 && caps[0xD]==0x08);
        State t;uint8_t tps[1]={0xc1},cap[1]={0x09};
        view(t,0x2203,tps,1);view(t,0x220D,cap,1);assert(t.tps4 && cap[0]==0x08);
        ++cases;
    }
    // Reads around these bytes, short reads and missing buffers change nothing
    // else: the caps up to 0x00C lose TPS4 only.
    {
        State s;s.tps4=true;
        uint8_t head[13];memcpy(head,panelCaps,13);view(s,ReceiverCaps,head,13);
        assert(head[3]==0x41 && !memcmp(head,panelCaps,3) && !memcmp(head+4,panelCaps+4,9));
        assert(!s.assrHidden && !s.intervalMasked && s.tps4Hidden==1);
        uint8_t status[6]={0x77,0,0x81,0,0,0};const uint8_t before[6]={0x77,0,0x81,0,0,0};
        view(s,0x202,status,6);assert(!memcmp(status,before,6));
        uint8_t span[4]={0x3f,0x09,0x80,0};view(s,0x00C,span,4);  // keeps the flag
        assert(span[1]==0x08 && span[2]==0x80 && !s.intervalMasked);
        view(s,ConfigCap,nullptr,2);uint8_t one[1]={0x80};view(s,AuxReadInterval,one,0);
        assert(one[0]==0x80);
        ++cases;
    }
    // The link configuration starts with the donor's LINK_BW_SET write, alone,
    // with the lane count or inside a block; nothing else starts it.
    {
        const uint8_t bw[1]={0x0A},link[2]={0x0A,0x82},block[4]={0,0x0A,0x82,0},pattern[1]={0x21};
        assert(startsLinkConfig(LinkBWSet,bw,1) && startsLinkConfig(LinkBWSet,link,2) && startsLinkConfig(0xFF,block,4));
        assert(!startsLinkConfig(0x101,link,1) && !startsLinkConfig(0x102,pattern,1) && !startsLinkConfig(0xFE,block,2));
        assert(!startsLinkConfig(LinkBWSet,nullptr,1) && !startsLinkConfig(LinkBWSet,bw,0));
        assert(!startsLinkConfig(ChannelCoding,bw,1) && !startsLinkConfig(SetPower,bw,1));
        ++cases;
    }
    // Training pattern order: the donor's clock recovery and equalization.
    // TRAINING_PATTERN_SET is held and goes with the next TRAINING_LANEx_SET.
    {
        Order o;const uint8_t cr[1]={0x21},eq[1]={0x23},tps4[1]={0x07},lanes[4]={0,0,0,0},off[1]={0};
        uint8_t out[1+MaxLanes];
        assert(write(o,TrainingPatternSet,cr,1)==Step::Hold && o.held && o.pattern==0x21 && clockRecovery(o.pattern));
        assert(write(o,TrainingLaneSet,lanes,4)==Step::Join && !o.held);
        assert(join(o.pattern,lanes,4,out)==5 && out[0]==0x21 && !out[1] && !out[4]);
        assert(write(o,TrainingLaneSet,lanes,4)==Step::Pass && read(o)==Step::Pass);
        assert(write(o,TrainingPatternSet,eq,1)==Step::Hold && !clockRecovery(o.pattern));
        assert(write(o,TrainingLaneSet,lanes,2)==Step::Join && join(o.pattern,lanes,2,out)==3 && out[0]==0x23);
        assert(write(o,TrainingPatternSet,tps4,1)==Step::Hold && !clockRecovery(o.pattern));
        assert(write(o,TrainingLaneSet,lanes,1)==Step::Join);
        assert(write(o,TrainingPatternSet,off,1)==Step::Pass && !o.held);  // the end of training goes out
        assert(o.holds==3 && o.joined==3 && !o.delivered && !o.replaced);
        ++cases;
    }
    // Any other access delivers the held write first; another
    // TRAINING_PATTERN_SET, alone or in a block, replaces it.
    {
        Order o;const uint8_t cr[1]={0x21},eq[1]={0x23},link[2]={0x0A,0x82},block[3]={0x21,0,0},five[5]={};
        write(o,TrainingPatternSet,cr,1);
        assert(read(o)==Step::Deliver && !o.held && o.delivered==1 && read(o)==Step::Pass);
        write(o,TrainingPatternSet,cr,1);assert(write(o,LinkBWSet,link,2)==Step::Deliver && o.delivered==2);
        write(o,TrainingPatternSet,cr,1);assert(write(o,TrainingPatternSet,eq,1)==Step::Hold && o.pattern==0x23 && o.replaced==1);
        assert(write(o,0x101,block,3)==Step::Pass && o.replaced==2 && !o.held);         // covers 0x102
        write(o,TrainingPatternSet,cr,1);assert(write(o,TrainingLaneSet,five,5)==Step::Deliver);  // too long to join
        write(o,TrainingPatternSet,cr,1);assert(write(o,0x104,five,2)==Step::Deliver);
        write(o,TrainingPatternSet,cr,1);assert(write(o,TrainingLaneSet,nullptr,4)==Step::Deliver);
        assert(write(o,TrainingPatternSet,block,3)==Step::Pass && !o.held);               // a block is not held
        assert(write(o,TrainingPatternSet,nullptr,1)==Step::Pass && write(o,TrainingPatternSet,cr,0)==Step::Pass);
        uint8_t out[1+MaxLanes];
        assert(!join(0x21,five,5,out) && !join(0x21,nullptr,2,out) && !join(0x21,five,0,out));
        ++cases;
    }
    // intel_wait_ddi_buf_active: an enabled, idle buffer is polled 10 us apart
    // for at most 10 ms; an active, disabled or unreadable one is not waited for.
    {
        uint32_t reg=0x80000082,activeAfter=250,waited=0;std::vector<uint32_t> reads;
        const auto rd=[&](uint32_t r){reads.push_back(r);return reg;};
        const auto delay=[&](uint32_t us){waited+=us;if(waited>=activeAfter)reg&=~BufferIdle;};
        assert(waitBufferActive(rd,delay,0)==250 && waited==250 && reads.front()==0x64000);
        reads.clear();reg=0x80000082;waited=0;activeAfter=UINT32_MAX;
        assert(waitBufferActive(rd,delay,1)==BufferWaitUs && waited==BufferWaitUs && reads.front()==0x64100);
        assert(reads.size()==BufferWaitUs/BufferPollUs+1);
        for(uint32_t v:{0x80000002U,0x00000080U,UINT32_MAX}) {
            reg=v;waited=0;assert(!waitBufferActive(rd,delay,0) && !waited);
        }
        ++cases;
    }
    printf("display13_edp_link: ok (%u cases)\n",cases);
    return 0;
}
