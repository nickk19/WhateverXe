// -wex-edp-keep through the production wrappers: the donor's receiver-caps
// reads (wrapDisplay13ReadAUX) and its mode-set decision
// (wrapDisplay13RegsNeedUpdate) over the panel path the IdeaPad Slim 5
// 14IAH8's firmware lit, with the real boot readout, AUX bridge and owner
// lock, mock MMIO and a sink with the panel's measured DPCD.
struct Sink {uint8_t dpcd[0x2300] {};bool trained=false;unsigned statusReads=0;uint32_t failingRead=UINT32_MAX;} sink;
unsigned sinkRead(void *,uint32_t address,void *data,uint32_t length){
 assert(!locked);auto b=static_cast<uint8_t *>(data);
 if(address==sink.failingRead)return 0xE00002BCU;
 if(address<=0x202 && address+length>0x202)++sink.statusReads;
 uint8_t status[6] {};if(sink.trained){status[0]=0x77;status[2]=0x01;}
 for(uint32_t i=0;i<length;++i){const auto a=address+i;b[i]=a>=0x202 && a<0x208 ? status[a-0x202] : sink.dpcd[a];}
 return 0;
}
// Neither the caps view nor the decision writes to the sink; the link
// configuration scenarios let it, and can make one register fail.
struct SinkWrites {bool allowed=false;uint32_t failing=UINT32_MAX;std::vector<std::pair<uint32_t,std::vector<uint8_t>>> log;} sinkWrites;
unsigned sinkWrite(void *,uint32_t address,void *data,uint32_t length){
 assert(!locked && sinkWrites.allowed);auto b=static_cast<uint8_t *>(data);
 sinkWrites.log.push_back({address,std::vector<uint8_t>(b,b+length)});
 if(address==sinkWrites.failing)return 0xE00002BCU;
 for(uint32_t i=0;i<length;++i)sink.dpcd[address+i]=b[i];
 return 0;
}
// Samsung ATNA40CT01 (WEXEDPDPCD and the Linux i915 DPCD dump): receiver caps,
// SUPPORTED_LINK_RATES up to 5.4 Gbps, eDP 1.4b, and the link settings the
// firmware leaves: LINK_BW_SET 0x14 next to LINK_RATE_SET 3, two lanes with
// enhanced framing.
void sinkReset(){
 sink=Sink{};const uint8_t caps[16]={0x14,0x14,0xc2,0xc1,0,0,1,0xc0,2,0,0,0,0x3f,0x09,0x80,0};
 memcpy(sink.dpcd,caps,16);memcpy(sink.dpcd+0x2200,caps,16);
 const uint8_t rates[16]={0xa4,0x1f,0x30,0x2a,0x76,0x2f,0xbc,0x34,0x48,0x3f,0x60,0x54,0x78,0x69,0,0};
 memcpy(sink.dpcd+0x10,rates,16);sink.dpcd[0x700]=0x05;sink.dpcd[0x701]=0xfd;sink.dpcd[0x702]=0xff;sink.dpcd[0x703]=0x02;
 sink.dpcd[0x100]=0x14;sink.dpcd[0x101]=0x82;sink.dpcd[0x115]=3;sink.dpcd[0x10A]=0x80;sink.dpcd[0x600]=1;
 // The Intel TCON brightness interface as the firmware leaves it: Intel's
 // OUI, version 1, nits, SDR brightness over AUX, on; 30 nits.
 const uint8_t intel[5]={0x01,0x10,0x01,0x00,0x10},oui[3]={0x00,0xaa,0x01};
 memcpy(sink.dpcd+0x340,intel,5);memcpy(sink.dpcd+0x300,oui,3);sink.dpcd[0x354]=30;
}
void edpSetup(Fixture &f,Map &map,bool panel=true){
 setup(f,map,0,0,2,10,panel);auto &c=display13FBPower;state.display13FBAUXHooksReady=true;
 state.display13AUXControllerSlot=reinterpret_cast<uintptr_t>(&c.controller);put(f.ctl,0xC30,c.lock);
 for(unsigned n=0;n<6;++n)put(f.port,0x594+4*n,0x64010U+4*n);
 state.orgDisplay13ReadAUX=reinterpret_cast<uintptr_t>(sinkRead);state.orgDisplay13WriteAUX=reinterpret_cast<uintptr_t>(sinkWrite);
 f.port[0x11A]=1;f.port[0x118]=0;put(f.port,0x550,1U);
 for(auto reg:{0x60544U,0x70008U,0x164280U,0x46010U,0x46014U,0x164284U,0x164288U,0x16428CU,0x164290U,0xC7200U,0xC7204U})map.regs[reg]=0;
 map.regs[0x64000]=0x80;sinkReset();sinkWrites=SinkWrites{};display13EDPLink={};
 display13EDPOrder={};display13EDPOrderLast[0]=display13EDPOrderLast[1]=0;display13EDPOrderLast[2]=UINT32_MAX;
 testOnDelay()=nullptr;testDelayedUs()=0;
 display13EDPBacklight={};display13EDPBacklightRestores=0;display13EDPOUIWrites=display13EDPOUIReplaced=0;
 display13EDPFirmware={};
}
// getDPCDInfo and the link setup read the receiver caps, the extended caps,
// the rate table and the LINK_BW_SET they reuse. Under -wex-edp-keep every view
// is capped at HBR; otherwise the panel's values pass unchanged. ASSR is never
// offered, and TPS4 not next to TPS3: the panel equalizes with TPS3, as the
// firmware trains it (kern_display13_edp_link.hpp).
void capsScenarios(bool keep,bool hook=true){
 Fixture f(0,0,0);Map map;edpSetup(f,map);
 // The decision hook is installed (any non-null original); without it the caps stay native.
 state.orgDisplay13RegsNeedUpdate=hook ? 1 : 0;keep=keep && hook;
 uint8_t caps[16],ext[16],rates[16],bw[2];
 assert(!XE::wrapDisplay13ReadAUX(f.port,0,caps,16) && !XE::wrapDisplay13ReadAUX(f.port,0x2200,ext,16));
 assert(!XE::wrapDisplay13ReadAUX(f.port,0x10,rates,16) && !XE::wrapDisplay13ReadAUX(f.port,0x100,bw,2));
 assert(caps[1]==(keep?0x0A:0x14) && ext[1]==(keep?0x0A:0x14) && bw[0]==(keep?0x0A:0x14) && bw[1]==0x82);
 assert(caps[0xD]==0x08 && ext[0xD]==0x08 && display13EDPLink.assrHidden==2 && !display13EDPLink.intervalMasked);
 assert(caps[3]==0x41 && ext[3]==0x41 && display13EDPLink.tps4Hidden==2);
 for(unsigned i=0;i<16;++i)if(i!=1 && i!=3 && i!=0xD)assert(caps[i]==sink.dpcd[i] && ext[i]==sink.dpcd[0x2200+i]);
 for(unsigned i=0;i<16;++i)assert(rates[i]==(keep && i>=8 ? 0 : sink.dpcd[0x10+i]));
 // Only the donor's view changes: the sink keeps its own values.
 assert(sink.dpcd[0x1]==0x14 && sink.dpcd[0x100]==0x14 && sink.dpcd[0xD]==0x09 && sink.dpcd[3]==0xc1);
 assert(objectRefs.empty());
}
// The firmware's link format (kern_display13_edp_link.hpp). With its depth
// from the boot handoff (TRANS_DDI_FUNC_CTL 0x8A010102: 8 bpc on DDI A) and
// its rate from the panel's link settings at the donor's first read
// (LINK_RATE_SET 3: 2.7 Gbps), -wex-edp-keep caps every view of the rates at
// HBR; at the firmware's HBR2 the caps are the panel's; a rate or depth not
// known leaves -wex-edp-keep's HBR. Without -wex-edp-keep the donor sees the
// panel's own caps; the firmware's format is still read and published.
void firmwareScenarios(bool keep){
 const auto record=[](){auto &d=IORegistryEntry::getRegistryRoot()->data;auto i=d.find("WEXEDPFirmwareLink");
  std::vector<uint32_t> r;if(i!=d.end()){r.resize(i->second.size()/4);memcpy(r.data(),i->second.data(),r.size()*4);}return r;};
 const auto read=[](void *port,uint8_t *caps,uint8_t *rates,uint8_t *bw){
  return !XE::wrapDisplay13ReadAUX(port,0,caps,16) && !XE::wrapDisplay13ReadAUX(port,0x10,rates,16) &&
         !XE::wrapDisplay13ReadAUX(port,0x100,bw,2);};
 uint8_t caps[16],rates[16],bw[2];
 {Fixture f(0,0,0);Map map;edpSetup(f,map);state.orgDisplay13RegsNeedUpdate=1;display13EDPFirmware.func=0x8A010102;display13EDPFirmware.depth=8;
  assert(read(f.port,caps,rates,bw) && caps[1]==(keep?0x0A:0x14) && bw[0]==(keep?0x0A:0x14) && bw[1]==0x82);
  for(unsigned i=0;i<16;++i)assert(rates[i]==(keep && i>=8 ? 0 : sink.dpcd[0x10+i]));
  assert((record()==std::vector<uint32_t>{1,8,13500,0x0A,0x14,3,1,0x8A010102,keep?0x0Au:0u,keep?8u:0u}));
  // Read once: a later LINK_RATE_SET does not change the firmware's rate.
  sink.dpcd[0x115]=6;assert(read(f.port,caps,rates,bw) && caps[1]==(keep?0x0A:0x14) && record()[2]==13500);
  // The panel itself is not written.
  assert(sink.dpcd[1]==0x14 && sink.dpcd[0x100]==0x14 && sinkWrites.log.empty());}
 {Fixture f(0,0,0);Map map;edpSetup(f,map);state.orgDisplay13RegsNeedUpdate=1;sink.dpcd[0x115]=6;display13EDPFirmware.func=0x8A110102;display13EDPFirmware.depth=10;
  assert(read(f.port,caps,rates,bw) && caps[1]==0x14 && bw[0]==0x14 && rates[12]==0x78 && rates[13]==0x69);
  assert((record()==std::vector<uint32_t>{1,10,27000,0x14,0x14,6,1,0x8A110102,keep?0x14u:0u,keep?10u:0u}));}
 // A link setting the panel does not return, or no depth: no firmware view.
 {Fixture f(0,0,0);Map map;edpSetup(f,map);state.orgDisplay13RegsNeedUpdate=1;sink.failingRead=0x115;display13EDPFirmware.depth=8;
  uint8_t c[16];assert(!XE::wrapDisplay13ReadAUX(f.port,0,c,16) && c[1]==(keep?0x0A:0x14));
  const auto r=record();assert(r.size()==10 && r[1]==8 && !r[2] && !r[3] && r[6]==1 && r[8]==(keep?0x0Au:0u) && r[9]==(keep?8u:0u));}
 {Fixture f(0,0,0);Map map;edpSetup(f,map);state.orgDisplay13RegsNeedUpdate=1;
  assert(read(f.port,caps,rates,bw) && caps[1]==(keep?0x0A:0x14) && bw[0]==(keep?0x0A:0x14));
  const auto r=record();assert(r[2]==13500 && r[3]==0x0A && !r[1]);}
 // Only the panel DDI is read and capped.
 {Fixture f(0,0,0);Map map;edpSetup(f,map,false);state.orgDisplay13RegsNeedUpdate=1;display13EDPFirmware.depth=8;
  uint8_t c[16];assert(!XE::wrapDisplay13ReadAUX(f.port,0,c,16) && c[1]==0x14 && !display13EDPFirmware.read);}
 printf("PASS Display13 eDP firmware link: read once, rates capped at the firmware's only with keep, the panel untouched (%s)\n",keep?"keep":"no keep");
}
// The donor's training pattern writes in the firmware's and Linux's order:
// TRAINING_PATTERN_SET goes to the panel with the next TRAINING_LANEx_SET,
// after the source sends the pattern; for clock recovery once the DDI buffer
// the donor enabled is active.
using LinkWrite=std::pair<uint32_t,std::vector<uint8_t>>;
Map *orderMap=nullptr;unsigned orderActiveAfter=0;
void orderBufferActive(unsigned us){if(us>=orderActiveAfter)orderMap->regs[0x64000]&=~0x80U;}
void orderScenarios(unsigned &scenarios){
 const auto record=[](){auto &d=IORegistryEntry::getRegistryRoot()->data;auto i=d.find("WEXEDPLink");
  std::vector<uint32_t> r;if(i!=d.end()){r.resize(i->second.size()/4);memcpy(r.data(),i->second.data(),r.size()*4);}return r;};
 uint8_t cr[1]={0x21},eq[1]={0x23},off[1]={0},lanes[4]={0,0,0,0},raised[4]={1,1,0,0},status[6];
 // The donor's clock recovery and equalization over an active buffer.
 {Fixture f(0,0,0);Map map;edpSetup(f,map);sinkWrites.allowed=true;map.regs[0x64000]=0x80000002;
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x102,cr,1) && sinkWrites.log.empty() && display13EDPOrder.held);
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x103,lanes,4));
  assert((sinkWrites.log==std::vector<LinkWrite>{{0x102,{0x21,0,0,0,0}}}) && sink.dpcd[0x102]==0x21 && !testDelayedUs());
  // Later passes write the lanes alone, and the status reads go straight out.
  assert(!XE::wrapDisplay13ReadAUX(f.port,0x202,status,6) && !XE::wrapDisplay13WriteAUX(f.port,0x103,raised,4));
  assert(sinkWrites.log.size()==2 && sinkWrites.log[1]==LinkWrite(0x103,{1,1,0,0}));
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x102,eq,1) && sinkWrites.log.size()==2);
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x103,raised,4) && sinkWrites.log[2]==LinkWrite(0x102,{0x23,1,1,0,0}));
  // The end of training is not held.
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x102,off,1) && sinkWrites.log[3]==LinkWrite(0x102,{0}) && sinkWrites.log.size()==4);
  const auto r=record();
  assert(r.size()==15 && r[8]==2 && r[9]==2 && !r[10] && !r[11] && !r[12] && !r[13] && !r[14]);
  ++scenarios;}
 // Clock recovery waits for the buffer to leave idle, 10 us at a time; a
 // buffer still idle after 10 ms gets the write anyway.
 {Fixture f(0,0,0);Map map;edpSetup(f,map);sinkWrites.allowed=true;map.regs[0x64000]=0x80000082;
  orderMap=&map;orderActiveAfter=370;testOnDelay()=orderBufferActive;
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x102,cr,1) && !XE::wrapDisplay13WriteAUX(f.port,0x103,lanes,4));
  assert(testDelayedUs()==370 && sinkWrites.log.size()==1 && record()[12]==370 && !record()[13]);
  testOnDelay()=nullptr;testDelayedUs()=0;map.regs[0x64000]=0x80000082;
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x102,cr,1) && !XE::wrapDisplay13WriteAUX(f.port,0x103,lanes,4));
  assert(testDelayedUs()==10000 && sinkWrites.log.size()==2 && record()[12]==10000 && record()[13]==1);
  // Equalization does not wait; a buffer that is off is not waited for.
  testDelayedUs()=0;
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x102,eq,1) && !XE::wrapDisplay13WriteAUX(f.port,0x103,lanes,4) && !testDelayedUs());
  map.regs[0x64000]=0x80;
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x102,cr,1) && !XE::wrapDisplay13WriteAUX(f.port,0x103,lanes,4) && !testDelayedUs());
  assert(sinkWrites.log.size()==4 && sinkWrites.log[3]==LinkWrite(0x102,{0x21,0,0,0,0}));
  ++scenarios;}
 // A read before the lanes, a second pattern, the pattern cleared: the panel
 // never misses a pattern the donor set, and sees only the last one.
 {Fixture f(0,0,0);Map map;edpSetup(f,map);sinkWrites.allowed=true;map.regs[0x64000]=0x80000002;
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x102,cr,1) && !XE::wrapDisplay13ReadAUX(f.port,0x202,status,6));
  assert((sinkWrites.log==std::vector<LinkWrite>{{0x102,{0x21}}}) && sink.statusReads==1 && display13EDPOrder.delivered==1);
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x102,cr,1) && !XE::wrapDisplay13WriteAUX(f.port,0x102,eq,1));
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x103,lanes,2) && sinkWrites.log[1]==LinkWrite(0x102,{0x23,0,0}));
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x102,cr,1) && !XE::wrapDisplay13WriteAUX(f.port,0x102,off,1));
  assert(sinkWrites.log.size()==3 && sinkWrites.log[2]==LinkWrite(0x102,{0}) && display13EDPOrder.replaced==2);
  ++scenarios;}
 // A failed joined write fails the donor's TRAINING_LANEx_SET.
 {Fixture f(0,0,0);Map map;edpSetup(f,map);sinkWrites.allowed=true;map.regs[0x64000]=0x80000002;sinkWrites.failing=0x102;
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x102,cr,1) && XE::wrapDisplay13WriteAUX(f.port,0x103,lanes,4)==0xE00002BCU);
  assert(record()[14]==0xE00002BCU && !display13EDPOrder.held);
  ++scenarios;}
 sinkWrites=SinkWrites{};display13EDPOrder={};
}
// Brightness over AUX: the firmware's state read at the donor's first caps
// read; Intel's OUI kept on the panel; after the panel trains, the interface
// awaited and brightness over AUX back on when a power loss turned it off,
// with the last level read from the panel, or the firmware's.
void backlightScenarios(unsigned &scenarios){
 const auto record=[](){auto &d=IORegistryEntry::getRegistryRoot()->data;auto i=d.find("WEXEDPBacklight");
  std::vector<uint32_t> r;if(i!=d.end()){r.resize(i->second.size()/4);memcpy(r.data(),i->second.data(),r.size()*4);}return r;};
 const auto boot=[](Fixture &f){uint8_t caps[16];assert(!XE::wrapDisplay13ReadAUX(f.port,0,caps,16));};
 const auto powerLoss=[](){memset(sink.dpcd+0x300,0,3);sink.dpcd[0x344]=0;sink.dpcd[0x354]=sink.dpcd[0x355]=0;};
 const LinkWrite intel{0x300,{0x00,0xaa,0x01}};
 // The panel after a system sleep: Intel's OUI, then the level, then 0x344, as Linux.
 {Fixture f(0,0,0);Map map;edpSetup(f,map);boot(f);
  const auto &b=display13EDPBacklight;
  assert(b.known && b.firmwareAux && b.firmwareOUI && b.firmwareNits==30 && !b.lastNits);
  auto r=record();assert(r.size()==17 && r[0]==2 && r[1]==1 && r[2]==1 && r[3]==1 && r[4]==0x10 && r[5]==30 && !r[7]);
  uint8_t again[16];sink.dpcd[0x344]=0;assert(!XE::wrapDisplay13ReadAUX(f.port,0,again,16) && b.firmwareControl==0x10);
  sink.dpcd[0x344]=0x10;
  // AuxDim's check of the level it wrote, through the donor: remembered.
  uint8_t level[2];sink.dpcd[0x354]=29;assert(!XE::wrapDisplay13ReadAUX(f.port,0x354,level,2) && b.lastNits==29);
  powerLoss();uint8_t zero[2];assert(!XE::wrapDisplay13ReadAUX(f.port,0x354,zero,2) && b.lastNits==29);
  sinkWrites.allowed=true;const unsigned slept=testSleptMs();XE::display13EDPTrained(f.port);
  assert((sinkWrites.log==std::vector<LinkWrite>{intel,{0x354,{29,0,0,0}},{0x344,{0x10}}}) && sink.dpcd[0x344]==0x10);
  r=record();assert(r[7]==1 && !r[8] && !r[9] && r[10]==1 && !r[11] && r[12]==0x10 && !r[13] && r[14]==1 && !r[15]);
  assert(testSleptMs()==slept);
  // Trained again with the interface on: nothing to write.
  XE::display13EDPTrained(f.port);assert(sinkWrites.log.size()==3 && record()[7]==1);
  ++scenarios;}
 // The firmware's level when none was read since; a level that came back
 // stays; other bits of 0x344 stay.
 {Fixture f(0,0,0);Map map;edpSetup(f,map);boot(f);
  powerLoss();sinkWrites.allowed=true;XE::display13EDPTrained(f.port);
  assert((sinkWrites.log==std::vector<LinkWrite>{intel,{0x354,{30,0,0,0}},{0x344,{0x10}}}));
  powerLoss();sink.dpcd[0x344]=0x01;sink.dpcd[0x354]=0x90;sink.dpcd[0x355]=0x01;sinkWrites.log.clear();
  XE::display13EDPTrained(f.port);
  assert((sinkWrites.log==std::vector<LinkWrite>{intel,{0x344,{0x11}}}) && record()[9]==0x190 && !record()[10]);
  ++scenarios;}
 // The interface the panel does not offer within 200 ms, 1 ms apart: nothing
 // more written. A failed level write leaves 0x344 alone.
 {Fixture f(0,0,0);Map map;edpSetup(f,map);boot(f);
  powerLoss();sink.dpcd[0x341]=0;sinkWrites.allowed=true;const unsigned slept=testSleptMs();
  XE::display13EDPTrained(f.port);
  assert((sinkWrites.log==std::vector<LinkWrite>{intel}) && testSleptMs()==slept+200 && record()[13]==UINT32_MAX && !record()[7]);
  sink.dpcd[0x341]=0x10;sinkWrites.log.clear();sinkWrites.failing=0x354;XE::display13EDPTrained(f.port);
  assert((sinkWrites.log==std::vector<LinkWrite>{{0x354,{30,0,0,0}}}) && !sink.dpcd[0x344] && record()[11]==0xE00002BCU);
  ++scenarios;}
 // The donor's source identification (Apple's OUI and device id) stays off
 // the panel: Intel's OUI goes there only when the panel lost it. Intel's OUI
 // also goes before the link configuration.
 {Fixture f(0,0,0);Map map;edpSetup(f,map);boot(f);sinkWrites.allowed=true;
  uint8_t apple[12]={0x00,0x10,0xfa,'A','A','P','L',0,0,1,1,0},bw[2]={0x0a,0x82};
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x300,apple,12) && sinkWrites.log.empty());
  powerLoss();assert(!XE::wrapDisplay13WriteAUX(f.port,0x300,apple,12));
  assert((sinkWrites.log==std::vector<LinkWrite>{intel}) && !memcmp(sink.dpcd+0x300,"\x00\xaa\x01\x00",4));
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x302,apple,2) && sinkWrites.log.size()==1);  // overlaps the OUI
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x303,apple,2) && sinkWrites.log.size()==2);  // after it: the donor's
  powerLoss();sinkWrites.log.clear();assert(!XE::wrapDisplay13WriteAUX(f.port,0x100,bw,2));
  assert(sinkWrites.log.size()==4 && sinkWrites.log[0]==intel && sinkWrites.log[1].first==0x600 && sinkWrites.log[3].first==0x100);
  auto r=record();assert(r[14]==2 && !r[15] && r[16]==3);
  ++scenarios;}
 // Brightness the firmware did not put over AUX, under Intel's OUI, a panel
 // without the interface, an unreadable interface: never written, the
 // donor's source identification passes.
 for(unsigned c=0;c<4;++c) {
  Fixture f(0,0,0);Map map;edpSetup(f,map);
  if(c==0)sink.dpcd[0x344]=0;
  if(c==1)sink.dpcd[0x342]=0;
  if(c==2)sink.failingRead=0x340;
  if(c==3)sink.dpcd[0x301]=0x10;
  boot(f);assert(display13EDPBacklight.known && !display13EDPBacklight.firmwareAux);
  sink.failingRead=UINT32_MAX;powerLoss();sinkWrites.allowed=true;XE::display13EDPTrained(f.port);
  assert(sinkWrites.log.empty() && !record()[7]);
  uint8_t apple[3]={0x00,0x10,0xfa};assert(!XE::wrapDisplay13WriteAUX(f.port,0x300,apple,3) && sinkWrites.log.size()==1);
 }
 ++scenarios;
 // The ATNA40CT01's caps (0x341 = 0x77, SDP colorimetry): after the donor
 // trains the firmware-lit panel, 0x344 goes from the firmware's 0x10 to
 // Linux's 0x90, the level stays; after a power loss the level, then 0x90.
 {Fixture f(0,0,0);Map map;edpSetup(f,map);sink.dpcd[0x341]=0x77;sink.dpcd[0x354]=0x90;sink.dpcd[0x355]=0x01;boot(f);
  sinkWrites.allowed=true;XE::display13EDPTrained(f.port);
  assert((sinkWrites.log==std::vector<LinkWrite>{{0x344,{0x90}}}) && sink.dpcd[0x344]==0x90);
  auto r=record();assert(r[7]==1 && r[8]==0x10 && r[9]==0x190 && !r[10] && !r[11] && r[12]==0x90);
  XE::display13EDPTrained(f.port);assert(sinkWrites.log.size()==1);
  powerLoss();sinkWrites.log.clear();XE::display13EDPTrained(f.port);
  assert((sinkWrites.log==std::vector<LinkWrite>{intel,{0x354,{0x90,0x01,0,0}},{0x344,{0x90}}}));
  ++scenarios;}
 // Another port: neither read at boot nor written.
 {Fixture f(0,0,0);Map map;edpSetup(f,map,false);boot(f);
  assert(!display13EDPBacklight.known);powerLoss();sinkWrites.allowed=true;
  XE::display13EDPTrained(f.port);assert(sinkWrites.log.empty());
  ++scenarios;}
 sinkWrites=SinkWrites{};
}
// The panel's link training as the firmware and Linux run it
// (kern_display13_edp_link.hpp): the donor's reads of the receiver caps, its
// link configuration writes and the order of its training pattern writes.
void linkScenarios(){
 unsigned scenarios=0;
 const auto record=[](){auto &d=IORegistryEntry::getRegistryRoot()->data;auto i=d.find("WEXEDPLink");
  std::vector<uint32_t> r;if(i!=d.end()){r.resize(i->second.size()/4);memcpy(r.data(),i->second.data(),r.size()*4);}return r;};
 // getDPCDInfo: the 16-byte caps keep the extended caps flag and show TPS3
 // without TPS4; the EDP_CONFIGURATION_CAP / TRAINING_AUX_RD_INTERVAL pair
 // shows interval 0 and no ASSR; TRAINING_AUX_RD_INTERVAL alone shows 0 too.
 {Fixture f(0,0,0);Map map;edpSetup(f,map);state.orgDisplay13RegsNeedUpdate=1;
  uint8_t caps[16],ext[16],pair[2],interval[1];
  assert(!XE::wrapDisplay13ReadAUX(f.port,0,caps,16) && !XE::wrapDisplay13ReadAUX(f.port,0x2200,ext,16));
  assert(caps[0xE]==0x80 && ext[0xE]==0x80 && caps[0xD]==0x08 && ext[0xD]==0x08);
  assert(caps[2]==0xc2 && ext[2]==0xc2 && caps[3]==0x41 && ext[3]==0x41);
  assert(!XE::wrapDisplay13ReadAUX(f.port,0xD,pair,2) && pair[0]==0x08 && pair[1]==0x00);
  assert(!XE::wrapDisplay13ReadAUX(f.port,0xE,interval,1) && interval[0]==0x00);
  assert(display13EDPLink.assrHidden==3 && display13EDPLink.intervalMasked==2);
  assert(sink.dpcd[0xD]==0x09 && sink.dpcd[0xE]==0x80 && sink.dpcd[3]==0xc1);
  // Published as soon as the view changes, before any link configuration.
  auto r=record();assert(r.size()==15 && r[0]==2 && r[1]==3 && r[2]==2 && r[7]==2);
  ++scenarios;}
 // Without TPS4 the panel equalizes with unscrambled patterns and keeps ASSR;
 // the interval is still read as 0.
 {Fixture f(0,0,0);Map map;edpSetup(f,map);sink.dpcd[3]=sink.dpcd[0x2203]=0x41;
  uint8_t caps[16],pair[2];
  assert(!XE::wrapDisplay13ReadAUX(f.port,0,caps,16) && !XE::wrapDisplay13ReadAUX(f.port,0xD,pair,2));
  assert(caps[0xD]==0x09 && pair[0]==0x09 && pair[1]==0x00 && !display13EDPLink.assrHidden && display13EDPLink.intervalMasked==1);
  assert(caps[3]==0x41 && !display13EDPLink.tps4Hidden);
  ++scenarios;}
 // TPS4 in the extended caps alone counts for both copies.
 {Fixture f(0,0,0);Map map;edpSetup(f,map);sink.dpcd[3]=0x41;
  uint8_t ext[16],caps[16];
  assert(!XE::wrapDisplay13ReadAUX(f.port,0x2200,ext,16) && !XE::wrapDisplay13ReadAUX(f.port,0,caps,16));
  assert(ext[0xD]==0x08 && caps[0xD]==0x08 && display13EDPLink.assrHidden==2);
  assert(ext[3]==0x41 && caps[3]==0x41 && display13EDPLink.tps4Hidden==1);
  ++scenarios;}
 // TPS4 without TPS3 stays: the donor equalizes with TPS4, as the firmware would.
 {Fixture f(0,0,0);Map map;edpSetup(f,map);sink.dpcd[2]=sink.dpcd[0x2202]=0x82;
  uint8_t caps[16],ext[16];
  assert(!XE::wrapDisplay13ReadAUX(f.port,0,caps,16) && !XE::wrapDisplay13ReadAUX(f.port,0x2200,ext,16));
  assert(caps[3]==0xc1 && ext[3]==0xc1 && !display13EDPLink.tps4Hidden && caps[0xD]==0x08);
  ++scenarios;}
 // The link configuration: D0 and 8b/10b, then the donor's LINK_BW_SET.
 {Fixture f(0,0,0);Map map;edpSetup(f,map);sinkWrites.allowed=true;IORegistryEntry::getRegistryRoot()->data.erase("WEXEDPLink");
  std::vector<uint32_t> r;sink.dpcd[0x600]=2;sink.dpcd[0x108]=0;const unsigned before=display13EDPLinkConfigs,slept=testSleptMs();
  uint8_t link[2]={0x0A,0x82},pattern[1]={0x21};
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x100,link,2));
  assert((sinkWrites.log==std::vector<LinkWrite>{{0x600,{1}},{0x108,{1}},{0x100,{0x0A,0x82}}}));
  assert(sink.dpcd[0x600]==1 && sink.dpcd[0x108]==1 && display13EDPLinkConfigs==before+1 && testSleptMs()==slept);
  r=record();assert(r.size()==15 && r[0]==2 && r[3]==before+1 && !r[4] && !r[5] && !r[6]);
  // Other writes pass alone, including a block that stops short of 0x100; a
  // held training pattern goes out before it.
  uint8_t block[2]={0x00,0x00};
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x102,pattern,1) && sinkWrites.log.size()==3);
  assert(!XE::wrapDisplay13WriteAUX(f.port,0xFE,block,2));
  assert(sinkWrites.log.size()==5 && sinkWrites.log[3]==LinkWrite(0x102,{0x21}) && sinkWrites.log[4]==LinkWrite(0xFE,{0,0}));
  assert(display13EDPLinkConfigs==before+1 && display13EDPOrder.holds==1 && display13EDPOrder.delivered==1);
  ++scenarios;}
 // A receiver that refuses D0: three tries 1 ms apart, then the training goes on.
 {Fixture f(0,0,0);Map map;edpSetup(f,map);sinkWrites.allowed=true;sinkWrites.failing=0x600;
  const unsigned slept=testSleptMs();uint8_t link[2]={0x0A,0x82};
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x100,link,2));
  assert(sinkWrites.log.size()==5 && sinkWrites.log[3]==LinkWrite(0x108,{1}) && sinkWrites.log[4]==LinkWrite(0x100,{0x0A,0x82}));
  auto r=record();assert(r[4]==0xE00002BCU && r[5]==3 && !r[6] && testSleptMs()==slept+3);
  ++scenarios;}
 // Another port (an external DisplayPort sink on DDI A) keeps its caps and writes.
 {Fixture f(0,0,0);Map map;edpSetup(f,map,false);sinkWrites.allowed=true;
  uint8_t caps[16],pair[2],link[2]={0x0A,0x82},pattern[1]={0x21},lanes[4]={0,0,0,0};
  assert(!XE::wrapDisplay13ReadAUX(f.port,0,caps,16) && !XE::wrapDisplay13ReadAUX(f.port,0xD,pair,2));
  assert(caps[0xD]==0x09 && pair[0]==0x09 && pair[1]==0x80 && caps[3]==0xc1 && !display13EDPLink.tps4);
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x100,link,2) && sinkWrites.log.size()==1);
  assert(!XE::wrapDisplay13WriteAUX(f.port,0x102,pattern,1) && !XE::wrapDisplay13WriteAUX(f.port,0x103,lanes,4));
  assert((sinkWrites.log==std::vector<LinkWrite>{{0x100,{0x0A,0x82}},{0x102,{0x21}},{0x103,{0,0,0,0}}}) && !display13EDPOrder.holds);
  ++scenarios;}
 orderScenarios(scenarios);
 backlightScenarios(scenarios);
 sinkWrites=SinkWrites{};display13EDPLink={};
 printf("PASS Display13 eDP link: interval 0, no ASSR and TPS3 in the donor's caps view, D0 and 8b/10b before the link configuration, training patterns after the source, Intel's OUI on the panel and brightness over AUX back on after training (%u scenarios)\n",scenarios);
}
// The panel path the firmware lit: pipe A -> DDI A eDP SST x2, 8 bpc,
// 1920x1200 from the panel's EDID DTD (154.25 MHz, 1920+160 x 1200+36, sync
// 48/32, 8/8), and the donor's CRTCParams for the same mode.
struct KeepMode {uint32_t ha,hb,hso,hsw,va,vb,vso,vsw;};
const KeepMode keepPanel {1920,160,48,32,1200,36,8,8};
void keepTimings(const KeepMode &t,uint32_t out[6]) {
 const uint32_t htotal=t.ha+t.hb,hstart=t.ha+t.hso,hend=hstart+t.hsw,vtotal=t.va+t.vb;
 out[0]=out[1]=(((htotal-1)<<16)&0x3FFF0000U)|((t.ha-1)&0x3FFFU);
 out[2]=(((hend-1)<<16)&0x3FFF0000U)|((hstart-1)&0x3FFFU);
 out[3]=out[4]=(((vtotal-1)<<16)&0x1FFF0000U)|((t.va-1)&0x1FFFU);
 const uint32_t vstart=0x1FFFU+t.va+t.vso,vend=vstart+t.vsw;
 out[5]=((vend<<16)&0x1FFF0000U)|(vstart&0x1FFFU);
}
void firmwareLink(Map &map,const KeepMode &m) {
 uint32_t t[6];keepTimings(m,t);
 map.regs[0x45404]|=0x400; // PW_A state
 const std::pair<uint32_t,uint32_t> regs[]={{0x60400,0x8A010102},{0x60540,0x80040300},{0x64000,0x80000002},
  {0x46140,0x10000000},{0x60030,0x7E5D159E},{0x60034,0x800000},{0x60040,0x7C1CD},{0x60044,0x80000},
  {0x60000,t[0]},{0x60004,t[1]},{0x60008,t[2]},{0x6000C,t[3]},{0x60010,t[4]},{0x60014,t[5]},
  {0x6001C,0x077F04AF},{0x70008,0xC0000000}};
 for(const auto &r:regs)map.regs[r.first]=r.second;
}
void keepParams(uint8_t *p,const KeepMode &m,uint32_t lanes=2,uint32_t bpc=0) {
 memset(p,0,0x100);uint32_t timing[6];keepTimings(m,timing);
 const uint32_t func=0x8A010000U|(bpc<<20)|((lanes-1)<<1),source=((m.ha-1)<<16)|(m.va-1);
 memcpy(p+0x4,&func,4);memcpy(p+0x10,timing,sizeof(timing));memcpy(p+0x28,&source,4);
}
unsigned regsCalls=0;bool regsResult=true;
bool nativeRegs(void *,void *,void *,void *,const void *,void *){assert(!locked);++regsCalls;return regsResult;}
struct KeepResult {bool update=true,recorded=false;uint32_t reasons=0,kept=0,call=0,portLanes=0,portRate=0;unsigned statusReads=0;size_t writes=0;};
template<class Change> KeepResult keepCase(Change change) {
 Fixture f(0,0,0);Map map;edpSetup(f,map);auto &c=display13FBPower;
 state.orgDisplay13RegsNeedUpdate=reinterpret_cast<uintptr_t>(nativeRegs);regsResult=true;
 firmwareLink(map,keepPanel);sink.trained=true;
 // WhateverXe adopted this boot path from the firmware (N97FBBootHandoff).
 c.bootPanel=true;c.bridge.bootPort=0;c.bridge.bootPath=reinterpret_cast<uintptr_t>(f.path);c.bridge.bootAdopted=true;
 uint8_t params[0x100];keepParams(params,keepPanel);
 // No earlier decision on this path unless the case says so (a new fixture
 // may reuse the previous one's address).
 display13EDPKeepPath=0;
 change(f,map,params);
 auto &data=IORegistryEntry::getRegistryRoot()->data;data.erase("WEXEDPKeep");
 const size_t writes=map.writes.size();const unsigned reads=sink.statusReads,calls=regsCalls;
 KeepResult r;r.update=XE::wrapDisplay13RegsNeedUpdate(f.ctl,f.fb,f.path,params,nullptr,nullptr);
 assert(regsCalls==calls+1 && !locked);
 r.writes=map.writes.size()-writes;r.statusReads=sink.statusReads-reads;
 if(data.count("WEXEDPKeep")) {
  const auto &v=data["WEXEDPKeep"];assert(v.size()==sizeof(Display13EDPKeepRecord));
  Display13EDPKeepRecord record;memcpy(&record,v.data(),sizeof(record));
  r.recorded=true;r.reasons=record.reasons;r.kept=record.kept;r.call=record.call;r.portLanes=record.portLanes;r.portRate=record.portRate;
 }
 assert(objectRefs.empty());
 return r;
}
Display13EDPKeepRecord firstRecord() {
 auto &data=IORegistryEntry::getRegistryRoot()->data;
 assert(data.count("WEXEDPKeepFirst"));const auto &v=data["WEXEDPKeepFirst"];assert(v.size()==sizeof(Display13EDPKeepRecord));
 Display13EDPKeepRecord record;memcpy(&record,v.data(),sizeof(record));return record;
}
void keepScenarios() {
 namespace K=Display13EDPKeep;unsigned scenarios=0;
 const auto none=[](Fixture &,Map &,uint8_t *){};
 // 1. The firmware's link runs the requested mode: no complete mode set, no
 //    register write, one DPCD status read outside the owner lock. The donor's
 //    rate (HBR) and the firmware's LINK_BW_SET (0x14) differ, as on the
 //    IdeaPad, and do not matter: the link is not touched.
 {auto r=keepCase(none);assert(!r.update && r.recorded && !r.reasons && r.kept==1 && !r.writes && r.statusReads==1);
  assert(r.call==1 && r.portLanes==2 && r.portRate==0x0A);
  const auto f=firstRecord();assert(f.call==1 && f.kept==1 && !f.reasons && f.ddi==0 && !f.pipe);
  assert(testSyslog().size()==1 && strstr(testSyslog()[0].c_str(),"firmware link kept"));++scenarios;}
 // The panel's Combo port before setPortMode (native mode 0, the handoff's
 // terms in display13PanelBootPort) is the same panel; any other mode is not.
 {auto r=keepCase([](Fixture &f,Map &,uint8_t *){put(f.port,0x550,0U);});assert(!r.update && !r.reasons && r.kept==2);++scenarios;}
 {auto r=keepCase([](Fixture &f,Map &,uint8_t *){put(f.port,0x550,2U);});assert(r.update && r.reasons==K::NotPanel && !r.statusReads);++scenarios;}
 // 2. Recorded only: an untrained sink status, another refresh of the same image.
 {auto r=keepCase([](Fixture &,Map &,uint8_t *){sink.trained=false;});assert(!r.update && r.reasons==K::Sink && r.kept==3);++scenarios;}
 {auto r=keepCase([](Fixture &,Map &,uint8_t *p){KeepMode m=keepPanel;m.vb=60;keepParams(p,m);});
  assert(!r.update && r.reasons==K::Blanking);++scenarios;}
 // 3. Never kept: a stopped or retrained source, another image, a scaled GOP
 //    mode, another lane count or depth, a path WhateverXe did not adopt, a
 //    pipe that is off.
 {auto r=keepCase([](Fixture &,Map &map,uint8_t *){map.regs[0x64000]=0x82;});assert(r.update && (r.reasons&K::NoSource) && !r.writes);++scenarios;}
 {auto r=keepCase([](Fixture &,Map &map,uint8_t *){map.regs[0x60540]=0x80040000;});assert(r.update && (r.reasons&K::NoSource));++scenarios;}
 {auto r=keepCase([](Fixture &,Map &,uint8_t *p){KeepMode m=keepPanel;m.ha=1680;m.hb=400;keepParams(p,m);});assert(r.update && (r.reasons&K::Timing));++scenarios;}
 {auto r=keepCase([](Fixture &,Map &map,uint8_t *){map.regs[0x6001C]=0x03FF02FF;});assert(r.update && r.reasons==K::Timing);++scenarios;}
 // 6. A scaled mode of the native timing (WindowServer's 1600x1000 on the
 //    1920x1200 panel): kept, the donor's scaler path programs scaler and
 //    PIPESRC. After a kept decision the scaled source is the donor's own, so
 //    back to native and on to another scaled mode are kept too; a path whose
 //    latest decision was not kept still refuses a scaled source.
 {auto r=keepCase([](Fixture &,Map &,uint8_t *p){const uint32_t s=(1599U<<16)|999U;memcpy(p+0x28,&s,4);});
  assert(!r.update && r.reasons==K::Source && !r.writes);++scenarios;}
 {auto r=keepCase([](Fixture &f,Map &map,uint8_t *){map.regs[0x6001C]=(1599U<<16)|999U;display13EDPKeepPath=reinterpret_cast<uintptr_t>(f.path);});
  assert(!r.update && r.reasons==K::Source);++scenarios;}
 {auto r=keepCase([](Fixture &f,Map &map,uint8_t *p){map.regs[0x6001C]=(1599U<<16)|999U;display13EDPKeepPath=reinterpret_cast<uintptr_t>(f.path);
   const uint32_t s=(1679U<<16)|1049U;memcpy(p+0x28,&s,4);});
  assert(!r.update && r.reasons==K::Source);++scenarios;}
 {auto r=keepCase([](Fixture &f,Map &map,uint8_t *){map.regs[0x6001C]=(1599U<<16)|999U;display13EDPKeepPath=reinterpret_cast<uintptr_t>(f.path)+8;});
  assert(r.update && r.reasons==K::Timing);++scenarios;}
 {auto r=keepCase([](Fixture &,Map &,uint8_t *p){keepParams(p,keepPanel,1);});assert(r.update && r.reasons==K::Format);++scenarios;}
 {auto r=keepCase([](Fixture &,Map &,uint8_t *p){keepParams(p,keepPanel,2,1);});assert(r.update && r.reasons==K::Format);++scenarios;}
 {auto r=keepCase([](Fixture &f,Map &,uint8_t *){put(f.port,0x124,1U);});assert(r.update && r.reasons==K::Format);++scenarios;}
 {auto r=keepCase([](Fixture &,Map &,uint8_t *){display13FBPower.bridge.bootPath=1;});assert(r.update && r.reasons==K::NotBoot);++scenarios;}
 {auto r=keepCase([](Fixture &,Map &,uint8_t *){display13FBPower.bootPanel=false;});assert(r.update && r.reasons==K::NotBoot);++scenarios;}
 {auto r=keepCase([](Fixture &,Map &,uint8_t *){display13FBPower.bridge.bootAdopted=false;});assert(r.update && r.reasons==K::NotBoot);++scenarios;}
 {auto r=keepCase([](Fixture &,Map &map,uint8_t *){map.regs[0x70008]=0x80000000;});assert(r.update && (r.reasons&K::PipeOff));++scenarios;}
 // 4. Not the panel's pipe A: no readout, no AUX.
 {auto r=keepCase([](Fixture &f,Map &,uint8_t *){put(f.path,0x3640,1U);});assert(r.update && r.reasons==K::NotPanel && !r.statusReads);++scenarios;}
 // 5. The donor already found nothing to update: untouched, unrecorded.
 {auto r=keepCase([](Fixture &,Map &,uint8_t *){regsResult=false;});assert(!r.update && !r.recorded && !r.statusReads);++scenarios;}
 // The first decision stays published next to the last one, and only the
 // first kept link is logged.
 {const auto f=firstRecord();assert(f.call==1 && !f.reasons);assert(testSyslog().size()==1);}
 printf("PASS Display13 eDP keep: %u mode-set decisions over a firmware-lit panel through the production wrappers\n",scenarios);
}
// -wex-edp-blank through the production power hook (display13EDPBlankPower):
// the kept boot panel path blanks and unblanks the primary plane and cursor of
// pipe A in place of the donor's Doze/Wake transition; system sleep and
// everything else take the donor's own. The mock MMIO window is real memory
// (the hook reads and writes it through the mapping's virtual address, as the
// kernel does).
alignas(4096) uint8_t blankMMIO[0x1000000];
uintptr_t Map::getVirtualAddress(){return reinterpret_cast<uintptr_t>(blankMMIO);}
volatile uint32_t &mmio(uint32_t offset){return *reinterpret_cast<volatile uint32_t *>(blankMMIO+offset);}
namespace B=Display13EDPBlank;
const uint32_t livePlane=0x84001008U,liveCursor=0x04000027U;
// The firmware's scanout chain as the IdeaPad Slim 5 14IAH8 reads it: TRANSCONF_A,
// DDI_BUF_CTL_A and PP_STATUS.
const uint32_t liveTrans=0xC0000000U,liveBuf=0x80000002U,livePower=0x80000008U;
void scanoutChain(uint32_t trans,uint32_t buf,uint32_t power){mmio(B::TransConf)=trans;mmio(B::ddiBufCtl(0))=buf;mmio(B::PPStatus)=power;}
void blankLive() {
 mmio(B::PlaneCtl)=livePlane;mmio(B::PlaneSurf)=0x00200000U;mmio(B::CurCtl)=liveCursor;mmio(B::CurBase)=0x01230000U;
 scanoutChain(liveTrans,liveBuf,livePower);
}
std::vector<uint32_t> blankRecord() {
 auto &data=IORegistryEntry::getRegistryRoot()->data;assert(data.count("WEXEDPBlank"));
 const auto &v=data["WEXEDPBlank"];assert(v.size()==16*sizeof(uint32_t));
 std::vector<uint32_t> r(16);memcpy(r.data(),v.data(),v.size());assert(r[0]==3);return r;
}
uint32_t powerOf(Fixture &f){uint32_t v;memcpy(&v,f.fb+0x4284,sizeof(v));return v;}
// The adopted boot panel path, awake, attached on pipe A, whose latest decision kept the link.
void blankPrepare(Fixture &f,Map &map) {
 edpSetup(f,map);auto &c=display13FBPower;
 c.bootPanel=true;c.bridge.bootPort=0;c.bridge.bootPath=reinterpret_cast<uintptr_t>(f.path);c.bridge.bootAdopted=true;
 f.attach();put(f.fb,0x4284,2U);state.fbOwned=true;state.adlnOutputWiring.dsi=0;
 state.orgDisplay13RegsNeedUpdate=reinterpret_cast<uintptr_t>(nativeRegs);map.length=0x1000000;
 c.bridge.state.faulted=c.bridge.state.suspended=false;
 display13EDPKeepPath=reinterpret_cast<uintptr_t>(f.path);
 display13EDPBlankState=Display13EDPBlank::State{};IORegistryEntry::getRegistryRoot()->data.erase("WEXEDPBlank");blankLive();
}
void blankScenarios() {
 const bool requested=testKernelArguments().count("-wex-edp-blank")!=0;
 unsigned scenarios=0;uint32_t result=0xFFFFFFFFU;
 const auto power=[&](Fixture &f,uint32_t st){result=0xFFFFFFFFU;return XE::display13EDPBlankPower(reinterpret_cast<IOService *>(f.fb),st,result);};
 {Fixture f(0,0,0);Map map;blankPrepare(f,map);
  if(!requested) {
   // Without the switch the donor's transition is untouched.
   assert(!power(f,1) && result==0xFFFFFFFFU && powerOf(f)==2 && mmio(B::PlaneCtl)==livePlane && mmio(B::CurCtl)==liveCursor);
   assert(!IORegistryEntry::getRegistryRoot()->data.count("WEXEDPBlank"));++scenarios;
   printf("PASS Display13 eDP blank off: the donor's power transition unchanged (%u scenarios)\n",scenarios);return;
  }
  // Display sleep: Wake -> Doze only clears the enables and records the state.
  assert(power(f,1) && result==kIOReturnSuccess && powerOf(f)==1);
  assert(!(mmio(B::PlaneCtl)&B::PlaneEnable) && mmio(B::PlaneCtl)==(livePlane&~B::PlaneEnable));
  assert(!(mmio(B::CurCtl)&B::CursorMode) && mmio(B::PlaneSurf)==0x00200000U && mmio(B::CurBase)==0x01230000U);
  {const auto r=blankRecord();
   assert(r[1]==1 && r[2]==2 && r[3]==1 && r[4]==unsigned(B::Action::Blank) && r[5]==1 && r[6]==0 && r[7]==livePlane && r[8]==liveCursor);
   assert(r[9]==0 && r[10]==6 && r[11]==0 && r[12]==0 && r[13]==liveTrans && r[14]==liveBuf && r[15]==livePower);}
  assert(display13EDPBlankState.fb==reinterpret_cast<uintptr_t>(f.fb));
  // Doze -> Doze while dark: nothing to do.
  assert(power(f,1) && powerOf(f)==1 && display13EDPBlankState.blanks==1 && blankRecord()[4]==unsigned(B::Action::Keep));
  // Wake: the enables come back, no donor transition.
  assert(power(f,2) && result==kIOReturnSuccess && powerOf(f)==2);
  assert(mmio(B::PlaneCtl)==livePlane && mmio(B::CurCtl)==liveCursor && !display13EDPBlankState.blanked);
  {const auto r=blankRecord();assert(r[1]==2 && r[2]==1 && r[4]==unsigned(B::Action::Unblank) && r[6]==1 && r[10]==7 && !display13EDPBlankState.fb);}
  // Awake again: nothing to do, still not the donor's.
  assert(power(f,2) && powerOf(f)==2 && display13EDPBlankState.blanks==1 && display13EDPBlankState.unblanks==1);
  assert(blankRecord()[4]==unsigned(B::Action::Keep));
  assert(!locked && objectRefs.empty());++scenarios;}
 // A donor reprogramming of the plane while dark survives the wake.
 {Fixture f(0,0,0);Map map;blankPrepare(f,map);
  assert(power(f,1));mmio(B::PlaneCtl)=0x04003008U;mmio(B::PlaneSurf)=0x00400000U;
  assert(power(f,2) && mmio(B::PlaneCtl)==0x84003008U && mmio(B::PlaneSurf)==0x00400000U);++scenarios;}
 // Each missing condition keeps the donor's own transition and the hardware untouched.
 const std::pair<const char *,void (*)(Fixture &)> refusals[]={
  {"link not kept",[](Fixture &){display13EDPKeepPath=0;}},
  {"another path",[](Fixture &f){display13EDPKeepPath=reinterpret_cast<uintptr_t>(f.path)+8;}},
  {"path not adopted at the handoff",[](Fixture &){display13FBPower.bridge.bootAdopted=false;}},
  {"not the boot path",[](Fixture &){display13FBPower.bridge.bootPath=1;}},
  {"not the boot panel",[](Fixture &){display13FBPower.bootPanel=false;}},
  {"pipe B",[](Fixture &f){put(f.path,0x3640,1U);}},
  {"path detached",[](Fixture &f){put(f.path,0x3644,1U);}},
  {"not an owned framebuffer",[](Fixture &){state.fbOwned=false;}},
  {"bridge suspended",[](Fixture &){display13FBPower.bridge.state.suspended=true;}},
  {"bridge faulted",[](Fixture &){display13FBPower.bridge.state.faulted=true;}},
  {"DSI panel",[](Fixture &){state.adlnOutputWiring.dsi=1;}},
  {"unknown power state",[](Fixture &f){put(f.fb,0x4284,7U);}},
 };
 // The hardware must agree with the software state: transcoder enabled and
 // active, the panel DDI's buffer enabled, the panel powered.
 const std::pair<const char *,void (*)(Fixture &)> stopped[]={
  {"transcoder off",[](Fixture &){scanoutChain(0,liveBuf,livePower);}},
  {"transcoder enabled but not active",[](Fixture &){scanoutChain(0x80000000U,liveBuf,livePower);}},
  {"DDI buffer off",[](Fixture &){scanoutChain(liveTrans,0,livePower);}},
  {"panel power off",[](Fixture &){scanoutChain(liveTrans,liveBuf,0);}},
  {"display reset (all zero)",[](Fixture &){scanoutChain(0,0,0);}},
  {"display unreadable (all ones)",[](Fixture &){scanoutChain(UINT32_MAX,UINT32_MAX,UINT32_MAX);}},
  {"boot port not the panel DDI",[](Fixture &){display13FBPower.bridge.bootPort=1;}},
 };
 for(const auto &refusal:refusals) {
  Fixture f(0,0,0);Map map;blankPrepare(f,map);refusal.second(f);
  const auto before=powerOf(f);
  if(power(f,1)){printf("blanked despite: %s\n",refusal.first);assert(false);}
  assert(powerOf(f)==before && mmio(B::PlaneCtl)==livePlane && mmio(B::CurCtl)==liveCursor && !display13EDPBlankState.blanked);
  assert(!locked && objectRefs.empty());++scenarios;
 }
 for(const auto &refusal:stopped) {
  Fixture f(0,0,0);Map map;blankPrepare(f,map);refusal.second(f);
  // Neither a blank nor an "already awake" answer over a stopped chain.
  for(uint32_t st:{1U,2U}) {
   if(power(f,st)){printf("handled despite: %s\n",refusal.first);assert(false);}
   assert(powerOf(f)==2 && mmio(B::PlaneCtl)==livePlane && mmio(B::CurCtl)==liveCursor && !display13EDPBlankState.blanked);
   assert(blankRecord()[4]==unsigned(B::Action::Native) && !(blankRecord()[10]&4));
  }
  assert(!locked && objectRefs.empty());++scenarios;
 }
 // A native doze stays native, also when it wakes.
 {Fixture f(0,0,0);Map map;blankPrepare(f,map);put(f.fb,0x4284,1U);
  assert(!power(f,2) && powerOf(f)==1 && blankRecord()[4]==unsigned(B::Action::Native));++scenarios;}
 // A blanked panel is never left dark: when its link is no longer the kept one
 // the wake unblanks it and hands over to the donor.
 {Fixture f(0,0,0);Map map;blankPrepare(f,map);
  assert(power(f,1));display13EDPKeepPath=0;
  assert(!power(f,2) && mmio(B::PlaneCtl)==livePlane && mmio(B::CurCtl)==liveCursor && !display13EDPBlankState.blanked);
  assert(powerOf(f)==1 && blankRecord()[4]==unsigned(B::Action::Unblank));++scenarios;}
 // Another framebuffer (an external display on pipe B, an offline connector
 // without a path) never touches the blanked panel and never overwrites its
 // record: in clamshell mode the external display wakes while the panel stays
 // off. Only the panel's own wake brings it back.
 {Fixture f(0,0,0);Map map;blankPrepare(f,map);
  Fixture ext(1,1,1);ext.attach();put(ext.fb,0x4284,1U);
  Fixture offline(2,2,2);put(offline.fb,0x4284,1U);
  assert(power(f,1) && !(mmio(B::PlaneCtl)&B::PlaneEnable));
  const auto record=blankRecord();
  for(uint32_t st:{2U,1U,0U,2U}) {
   assert(!power(ext,st) && result==0xFFFFFFFFU && !power(offline,st) && result==0xFFFFFFFFU);
   assert(!(mmio(B::PlaneCtl)&B::PlaneEnable) && !(mmio(B::CurCtl)&B::CursorMode) && display13EDPBlankState.blanked);
   assert(blankRecord()==record);
  }
  assert(power(f,2) && mmio(B::PlaneCtl)==livePlane && mmio(B::CurCtl)==liveCursor && !display13EDPBlankState.blanked);
  // Awake, the others' callbacks still take the donor's path and leave pipe A alone.
  assert(!power(ext,2) && !power(offline,2) && mmio(B::PlaneCtl)==livePlane);
  assert(!locked && objectRefs.empty());++scenarios;}
 // The panel's next callback with pipe A no longer on the boot panel (the
 // bridge suspended or faulted, the path moved to pipe B) or with the scanout
 // chain stopped under the software state (a reset the callbacks did not
 // see) releases the blank without writing a register, and hands over to the
 // donor, which still sees its doze.
 for(int variant=0;variant<6;++variant)
  for(uint32_t st:{2U,1U}) {
   Fixture f(0,0,0);Map map;blankPrepare(f,map);
   assert(power(f,1));const auto dark=mmio(B::PlaneCtl);const auto darkCursor=mmio(B::CurCtl);
   if(variant==0)display13FBPower.bridge.state.suspended=true;
   else if(variant==1)display13FBPower.bridge.state.faulted=true;
   else if(variant==2)put(f.path,0x3640,1U);
   else if(variant==3)scanoutChain(0,0,0);
   else if(variant==4)scanoutChain(liveTrans,0,livePower);
   else scanoutChain(liveTrans,liveBuf,0);
   assert(!power(f,st) && mmio(B::PlaneCtl)==dark && mmio(B::CurCtl)==darkCursor && !(dark&B::PlaneEnable));
   assert(!display13EDPBlankState.blanked && !display13EDPBlankState.fb && powerOf(f)==1);
   assert(display13EDPBlankState.releases==1 && display13EDPBlankState.unblanks==0 && !display13EDPBlankState.sleepReleases);
   {const auto r=blankRecord();const auto reason=variant<3 ? B::Reason::NotScannedOut : B::Reason::NotRunning;
    assert(r[4]==unsigned(B::Action::Release) && r[9]==1 && r[11]==unsigned(reason) && r[12]==0 && (r[10]&1) && !(r[10]&8));}
   // Released: the next callbacks are the donor's, with the plane left to it.
   assert(!power(f,2) && mmio(B::PlaneCtl)==dark);
   assert(!locked && objectRefs.empty());++scenarios;
  }
 // System sleep stays the donor's: Off is entered through prepareToEnterSleep
 // and left through prepareToExitSleep, which own IOGraphics' sleep/wake
 // handshake. Sleeping from an awake panel: the donor's Wake -> Off, the
 // hardware untouched.
 {Fixture f(0,0,0);Map map;blankPrepare(f,map);
  assert(!power(f,0) && result==0xFFFFFFFFU && powerOf(f)==2 && mmio(B::PlaneCtl)==livePlane && mmio(B::CurCtl)==liveCursor);
  {const auto r=blankRecord();assert(r[1]==0 && r[2]==2 && r[4]==unsigned(B::Action::Native) && r[5]==0 && r[9]==0);}
  // The donor's Off, then its DarkWake doze and full wake over a reset display:
  // every callback is the donor's, none touches pipe A.
  put(f.fb,0x4284,0U);scanoutChain(0,0,0);mmio(B::PlaneCtl)=0;mmio(B::CurCtl)=0;
  assert(!power(f,1) && powerOf(f)==0 && blankRecord()[4]==unsigned(B::Action::Native));
  put(f.fb,0x4284,1U);
  assert(!power(f,2) && powerOf(f)==1 && blankRecord()[4]==unsigned(B::Action::Native));
  put(f.fb,0x4284,0U);
  assert(!power(f,2) && powerOf(f)==0 && mmio(B::PlaneCtl)==0 && mmio(B::CurCtl)==0);
  assert(display13EDPBlankState.blanks==0 && !display13EDPBlankState.blanked);
  // Back after the donor's own wake (here: the chain running again): display sleep blanks again.
  put(f.fb,0x4284,2U);blankLive();
  assert(power(f,1) && powerOf(f)==1 && display13EDPBlankState.blanks==1);
  assert(!locked && objectRefs.empty());++scenarios;}
 // Sleeping from display sleep: the blank is released without a register
 // write, and the donor gets Wake back over the still running pipe, so that its
 // Wake -> Off also runs prepareToExitWake before prepareToEnterSleep.
 {Fixture f(0,0,0);Map map;blankPrepare(f,map);
  assert(power(f,1) && powerOf(f)==1);const auto dark=mmio(B::PlaneCtl);
  assert(!power(f,0) && result==0xFFFFFFFFU && powerOf(f)==2 && mmio(B::PlaneCtl)==dark && !(dark&B::PlaneEnable));
  assert(!(mmio(B::CurCtl)&B::CursorMode) && !display13EDPBlankState.blanked && !display13EDPBlankState.fb);
  assert(display13EDPBlankState.releases==1 && display13EDPBlankState.sleepReleases==1 && display13EDPBlankState.unblanks==0);
  {const auto r=blankRecord();
   assert(r[1]==0 && r[2]==1 && r[4]==unsigned(B::Action::Release) && r[9]==1 && r[10]==15 && r[11]==unsigned(B::Reason::SystemSleep) && r[12]==1);}
  assert(!locked && objectRefs.empty());++scenarios;}
 // The pipe already stopped by then: the donor keeps the doze it was told.
 {Fixture f(0,0,0);Map map;blankPrepare(f,map);
  assert(power(f,1));scanoutChain(0,0,0);
  assert(!power(f,0) && powerOf(f)==1 && display13EDPBlankState.sleepReleases==1);
  {const auto r=blankRecord();assert(r[4]==unsigned(B::Action::Release) && r[10]==3 && r[11]==unsigned(B::Reason::SystemSleep));}
  assert(!locked && objectRefs.empty());++scenarios;}
 // A blank still in place when the donor reports Off (a sleep entered by the
 // donor without this callback) is released on the way out as well.
 for(uint32_t st:{1U,2U}) {
  Fixture f(0,0,0);Map map;blankPrepare(f,map);
  assert(power(f,1));put(f.fb,0x4284,0U);scanoutChain(0,0,0);
  assert(!power(f,st) && powerOf(f)==0 && !display13EDPBlankState.blanked && display13EDPBlankState.sleepReleases==1);
  {const auto r=blankRecord();assert(r[2]==0 && r[4]==unsigned(B::Action::Release) && r[11]==unsigned(B::Reason::SystemSleep) && !(r[10]&8));}
  assert(!locked && objectRefs.empty());++scenarios;
 }
 // The hardware window must be the whole 16 MB BAR0.
 {Fixture f(0,0,0);Map map;blankPrepare(f,map);map.length=0x800000;
  assert(!power(f,1) && powerOf(f)==2 && mmio(B::PlaneCtl)==livePlane && !display13EDPBlankState.blanked);++scenarios;}
 // Ready needs the keep decision hook: without it no link is ever kept.
 {Fixture f(0,0,0);Map map;blankPrepare(f,map);
  assert(XE::display13EDPBlankReady());
  state.orgDisplay13RegsNeedUpdate=0;assert(!XE::display13EDPBlankReady());
  state.orgDisplay13RegsNeedUpdate=reinterpret_cast<uintptr_t>(nativeRegs);state.adlnOutputWiring.panel=0;assert(!XE::display13EDPBlankReady());
  ++scenarios;}
 printf("PASS Display13 eDP blank: plane/cursor blank and unblank in place of the donor's Doze/Wake, eligibility, never left dark, system sleep and a stopped scanout chain left to the donor, other framebuffers leave pipe A alone (%u scenarios)\n",scenarios);
}
// Without -wex-edp-keep the decision is the donor's, unrecorded.
void offScenario() {
 Fixture f(0,0,0);Map map;edpSetup(f,map);
 state.orgDisplay13RegsNeedUpdate=reinterpret_cast<uintptr_t>(nativeRegs);regsResult=true;firmwareLink(map,keepPanel);sink.trained=true;
 auto &c=display13FBPower;c.bootPanel=true;c.bridge.bootPort=0;c.bridge.bootPath=reinterpret_cast<uintptr_t>(f.path);c.bridge.bootAdopted=true;
 uint8_t params[0x100];keepParams(params,keepPanel);const unsigned reads=sink.statusReads;
 assert(XE::wrapDisplay13RegsNeedUpdate(f.ctl,f.fb,f.path,params,nullptr,nullptr) && sink.statusReads==reads);
 assert(!IORegistryEntry::getRegistryRoot()->data.count("WEXEDPKeep") && testSyslog().empty());
 {Fixture g(0,0,0);Map m;blankPrepare(g,m);uint32_t r=0xFFFFFFFFU;assert(!XE::display13EDPBlankPower(reinterpret_cast<IOService *>(g.fb),1,r) && mmio(B::PlaneCtl)==livePlane);
  assert(!XE::display13EDPBlankReady());}
 printf("PASS Display13 eDP keep off: the donor's caps and mode-set decision unchanged\n");
}
int main(int argc,char **argv){
 for(int i=1;i<argc;++i)testKernelArguments().insert(argv[i]);
 const bool keep=testKernelArguments().count("-wex-edp-keep")!=0;
 capsScenarios(keep);
 // A donor build the decision hook does not match keeps nothing: its caps stay native.
 capsScenarios(keep,false);
 firmwareScenarios(keep);
 linkScenarios();
 if(keep){keepScenarios();blankScenarios();}else offScenario();
 return 0;
}
