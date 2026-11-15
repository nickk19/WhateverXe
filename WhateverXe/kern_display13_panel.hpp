// Xe_LPD PPS0 / PCH PWM. Register facts: Linux v6.12 intel_pps.c and
// intel_backlight.c. Native code remains the owner of panel delays/sequence.
#ifndef KERN_DISPLAY13_PANEL_HPP
#define KERN_DISPLAY13_PANEL_HPP
#include "kern_xelp_panel_power.hpp"
#include "kern_xelp_panel_pwm.hpp"
namespace Display13Panel {
// Exact c004fc1a LightUpEDP: reject failed power/train before enabling scanout.
// This adapter admits only the standard native panel (controller+D0 is null),
// so the donor PSR-only branches are skipped without inventing PSR ownership.
struct LightPatch {uint32_t offset;uint8_t size,before[16],after[16];};
static constexpr LightPatch lightPatches[]={
 {0x50669,16,{0x48,0x8b,0x83,0x40,0x0d,0x00,0x00,0x83,0xb8,0x90,0x01,0x00,0x00,0x02,0x75,0x5b},
              {0x41,0x89,0xc6,0x85,0xc0,0x0f,0x85,0xcd,0x01,0x00,0x00,0xe9,0x5b,0x00,0x00,0x00}},
 {0x506fd,6,{0x0f,0x84,0x50,0x01,0x00,0x00},{0x0f,0x85,0x3e,0x01,0x00,0x00}}
};
struct State {
 XELP::PanelPWM pwm;
 uint32_t bios[3] {};
 bool captured {};
 template<class IO> bool capture(IO &io,const XELP::VBT::Ports::Panel &panel) {
  if(captured)return pwm.valid();
  const auto on=io.read(0xC7208),off=io.read(0xC720C),control=io.read(0xC7204);
  const auto period=io.read(0xC8254),polarity=io.read(0xC8250);
  const auto selected=XELP::PanelPWM::select(period,polarity,period?0:io.read(0xC2014),panel.pwmHz,panel.activeLowPWM);
  if(on==~0U || off==~0U || control==~0U || !selected.valid())return false;
  bios[0]=on;bios[1]=off;bios[2]=control;pwm=selected;captured=true;return true;
 }
};
// WA 22019252566 (Display13/14): block PCH DPLS clock gating over panel on.
// Keep a pre-existing firmware/peer request and unrelated bits. Call finish
// after every native exit, including timeout. The caller owns PG1 and serializes PPS.
struct OnGate {
 static constexpr uint32_t Reg=0xC2020,Bit=1U<<29;
 bool changed {};
 template<class IO> bool begin(IO &io) {
  const auto before=io.read(Reg);if(before==~0U)return false;
  if(before&Bit)return true;
  changed=true;io.write(Reg,before|Bit);return io.read(Reg)==(before|Bit);
 }
 template<class IO> bool finish(IO &io) {
  if(!changed)return true;
  const auto value=io.read(Reg);if(value==~0U)return false;
  io.write(Reg,value&~Bit);const bool ok=io.read(Reg)==(value&~Bit);
  if(ok)changed=false;return ok;
 }
};
// A panel the firmware lit is still on at the donor's first panel-on request,
// and hwSetPanelPower(2) returns without a PPS write when PP_STATUS reads on:
// the donor's complete mode set then trains a panel that went through no power
// cycle since the firmware's link. Samsung ATNA OLED eDP panels stay dark that
// way (training completes, brightness over AUX is ignored) until a display
// sleep powers them off and its wake on again. Linux's full mode set powers
// the panel off on disable and on again on enable (intel_pps_off/on, with the
// power cycle delay T12 between them). Do the same once for such a panel,
// through the donor: off (0), then on (2). Not with -wex-edp-keep, whose kept
// link runs on the firmware's panel power.
struct Cycle {
 static constexpr uint32_t Off=0,On=2,PPStatus=0xC7200,PPOn=0x80000000U;
 bool donorOn {}; // The panel is on by the donor's own latest request.
 uint32_t requests {},cycles {};
 // The latest on request: -wex-edp-keep, donorOn, PP_STATUS before, the off
 // result, PP_STATUS after off, the on result, PP_STATUS after on, then the
 // microseconds off and on took (UINT32_MAX / 0 when nothing was cycled).
 uint32_t last[9] {};
 bool wanted(uint32_t state,uint32_t status,bool keep) const {
  return state==On && !keep && !donorOn && status!=~0U && (status&PPOn);
 }
 void begin(uint32_t status,bool keep) {
  ++requests;const uint32_t start[9]={keep,donorOn,status,~0U,~0U,~0U,~0U,0,0};
  for(unsigned i=0;i<9;++i)last[i]=start[i];
 }
 // Every request the wrapper took, failed ones too: only a completed on
 // leaves the panel on by the donor.
 void finish(uint32_t state,bool ok) {donorOn=ok && state==On;}
};
}
#endif
