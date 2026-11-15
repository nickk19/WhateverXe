#include "kern_display13_edp_blank.hpp"
#include <assert.h>
#include <map>
#include <stdio.h>
#include <vector>
using namespace Display13EDPBlank;
struct IO {
    std::map<uint32_t,uint32_t> r;
    std::vector<uint32_t> writes;
    uint32_t read(uint32_t reg) {assert(r.count(reg));return r[reg];}
    void write(uint32_t reg,uint32_t v) {assert(r.count(reg));r[reg]=v;writes.push_back(reg);}
};
static IO live() {
    IO io;
    io.r[PlaneCtl]=0x84001008U; io.r[PlaneSurf]=0x00200000U;
    io.r[CurCtl]=0x04000027U; io.r[CurBase]=0x01230000U;
    io.r[TransConf]=0xC0000000U; io.r[ddiBufCtl(0)]=0x80000002U; io.r[ddiBufCtl(1)]=0U; io.r[PPStatus]=0x80000008U;
    return io;
}
static Action act(bool eligible,bool scanout,bool live,bool owner,uint32_t current,uint32_t requested,bool blanked) {
    const auto d=decide(eligible,scanout,live,owner,current,requested,blanked);
    assert((d.action==Action::Release)==(d.reason!=Reason::None));
    return d.action;
}
static Reason why(bool eligible,bool scanout,bool live,bool owner,uint32_t current,uint32_t requested,bool blanked) {
    return decide(eligible,scanout,live,owner,current,requested,blanked).reason;
}
int main() {
    // Display sleep (eligible, scanout, live, owner, current, requested, blanked).
    assert(act(false,true,true,false,2,1,false)==Action::Native);
    assert(act(true,true,true,false,2,1,false)==Action::Blank);
    assert(act(true,true,true,true,1,2,true)==Action::Unblank);
    assert(act(true,true,true,true,1,1,true)==Action::Keep);
    assert(act(true,true,true,false,2,2,false)==Action::Keep);
    assert(act(true,true,true,false,1,2,false)==Action::Native);      // native doze stays native
    assert(act(true,true,true,false,2,3,false)==Action::Native);      // unknown state
    assert(act(true,true,true,true,1,3,true)==Action::Native);
    assert(act(false,true,true,true,1,2,true)==Action::Unblank);      // the panel's own wake never leaves it dark
    assert(act(false,true,true,true,1,1,true)==Action::Native);

    // System sleep is the donor's, entering and leaving Off alike, whatever
    // the rest says; a blank in place is released for it.
    for(int bits=0;bits<8;++bits) {
        const bool eligible=bits&1,scanout=bits&2,running=bits&4;
        for(uint32_t current=0;current<=2;++current) {
            assert(act(eligible,scanout,running,false,current,0,false)==Action::Native);
            assert(act(eligible,scanout,running,false,0,current,false)==Action::Native);
            assert(act(eligible,scanout,running,true,current,0,true)==Action::Release);
            assert(why(eligible,scanout,running,true,current,0,true)==Reason::SystemSleep);
            assert(act(eligible,scanout,running,true,0,current,true)==Action::Release);
            assert(why(eligible,scanout,running,true,0,current,true)==Reason::SystemSleep);
        }
    }

    // Unblank, keep and blank need the scanout chain running. Otherwise a
    // blank in place is released and the donor's transition follows.
    assert(act(true,true,false,false,2,1,false)==Action::Native);
    assert(act(true,true,false,false,2,2,false)==Action::Native);
    for(uint32_t requested=1;requested<=3;++requested) {
        assert(act(true,true,false,true,1,requested,true)==Action::Release);
        assert(why(true,true,false,true,1,requested,true)==Reason::NotRunning);
        assert(act(true,false,true,true,1,requested,true)==Action::Release);
        assert(why(true,false,false,true,1,requested,true)==Reason::NotScannedOut);
    }

    // Another framebuffer never touches the blanked panel, whatever it asks.
    for(uint32_t current=0;current<=3;++current)
        for(uint32_t requested=0;requested<=3;++requested)
            for(int bits=0;bits<8;++bits)
                assert(act(bits&1,bits&2,bits&4,false,current,requested,true)==Action::Native);

    // The scanout chain: transcoder enabled and active, the panel DDI's buffer
    // enabled, the panel powered; a reset or unpowered display is not running.
    {
        auto io=live();uint32_t seen[3];
        assert(running(io,0,seen) && seen[0]==0xC0000000U && seen[1]==0x80000002U && seen[2]==0x80000008U);
        assert(!running(io,1,seen) && seen[1]==0U);
        assert(!running(io,9,seen) && seen[1]==UINT32_MAX);
        const uint32_t regs[]={TransConf,ddiBufCtl(0),PPStatus};
        for(auto reg:regs)
            for(uint32_t value:{0U,UINT32_MAX}) {
                auto off=live();off.r[reg]=value;
                assert(!running(off,0,seen));
                assert(off.writes.empty());
            }
        auto idle=live();idle.r[TransConf]=0x80000000U;   // enabled, not yet active
        assert(!running(idle,0,seen));
    }

    // Blank clears only the enables and latches each plane once.
    auto io=live();State s;
    blank(io,s);
    assert(s.blanked && s.blanks==1);
    assert(io.r[PlaneCtl]==0x04001008U && io.r[PlaneSurf]==0x00200000U);
    assert(io.r[CurCtl]==0x04000000U && io.r[CurBase]==0x01230000U);
    assert((io.writes==std::vector<uint32_t>{CurCtl,CurBase,PlaneCtl,PlaneSurf}));

    // Unblank restores the enables, latching after each control write.
    io.writes.clear();
    unblank(io,s);
    assert(!s.blanked && s.unblanks==1);
    assert(io.r[PlaneCtl]==0x84001008U && io.r[CurCtl]==0x04000027U);
    assert((io.writes==std::vector<uint32_t>{PlaneCtl,PlaneSurf,CurCtl,CurBase}));

    // A donor reprogramming while dark is kept; only the enable comes back.
    io=live();State t;blank(io,t);
    io.r[PlaneCtl]=0x04003008U; io.r[PlaneSurf]=0x00400000U;
    unblank(io,t);
    assert(io.r[PlaneCtl]==0x84003008U && io.r[PlaneSurf]==0x00400000U);

    // A cursor the donor already re-enabled is not rewritten.
    io=live();State u;blank(io,u);
    io.r[CurCtl]=0x04000022U;io.writes.clear();
    unblank(io,u);
    assert((io.writes==std::vector<uint32_t>{PlaneCtl,PlaneSurf}));
    assert(io.r[CurCtl]==0x04000022U);

    // Unblank and release drop the owner; release writes nothing and records why.
    io=live();State w;blank(io,w);w.fb=0x1234;unblank(io,w);
    assert(!w.blanked && !w.fb && w.unblanks==1 && !w.releases);
    blank(io,w);w.fb=0x1234;io.writes.clear();release(w,Reason::NotRunning);
    assert(!w.blanked && !w.fb && w.releases==1 && !w.sleepReleases && w.lastRelease==Reason::NotRunning);
    assert(w.unblanks==1 && io.writes.empty());
    assert(io.r[PlaneCtl]==0x04001008U && io.r[CurCtl]==0x04000000U);
    blank(io,w);w.fb=0x1234;release(w,Reason::SystemSleep);
    assert(w.releases==2 && w.sleepReleases==1 && w.lastRelease==Reason::SystemSleep && w.blanks==3);

    // A plane that was already off stays off.
    io=live();io.r[PlaneCtl]=0x04001008U;State v;blank(io,v);unblank(io,v);
    assert(io.r[PlaneCtl]==0x04001008U);
    puts("display13_edp_blank: ok");
    return 0;
}
