#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <algorithm>

// Wire format / realignment adapted from the MIT projects credited in
// firmware/LIGHTBAR_THIRD_PARTY.txt. No RF24/Arduino dependency in this model.
namespace Lightbar {
constexpr uint8_t PREAMBLE[] = {0x53,0x39,0x14,0xDD,0x1C,0x49,0x34,0x12};
constexpr uint32_t MANUAL_HOLD_MS = 600000;
inline int level(int n) { return std::max(0, std::min(15,n)); }
inline uint16_t crc16(const uint8_t* bytes, size_t n) {
  uint16_t c=0xfffe;
  while(n--) { c^=uint16_t(*bytes++)<<8; for(int b=0;b<8;b++) c=(c&0x8000)?uint16_t((c<<1)^0x1021):uint16_t(c<<1); }
  return c;
}
struct Packet { uint32_t id=0; uint8_t seq=0,command=0,options=0; };
inline void encode(const Packet& p,uint8_t out[17]) {
  memcpy(out,PREAMBLE,8); out[8]=p.id>>16; out[9]=p.id>>8; out[10]=p.id;
  out[11]=0xff; out[12]=p.seq; out[13]=p.command; out[14]=p.options;
  uint16_t c=crc16(out,15); out[15]=c>>8; out[16]=c;
}
inline bool decode(const uint8_t* b,size_t n,Packet& p) {
  if(n!=17 || memcmp(b,PREAMBLE,8) || b[11]!=0xff || crc16(b,15)!=(uint16_t(b[15])<<8|b[16]) || b[13]<1 || b[13]>6) return false;
  p={uint32_t(b[8])<<16|uint32_t(b[9])<<8|b[10],b[12],b[13],b[14]}; return p.id>0;
}
inline bool decodeRaw(const uint8_t raw[17],Packet& p) {
  uint8_t b[17]; b[0]=0x50|(raw[0]>>5);
  for(int i=1;i<17;i++) b[i]=uint8_t(raw[i-1]<<3)|(raw[i]>>5);
  return decode(b,17,p);
}
struct Estimate {
  int8_t power=-1, brightness=-1, temperature=-1;
  // Positive options increase both scales; negative options decrease them,
  // regardless of which of the two wheel command IDs was used by the remote.
  void apply(uint8_t command,uint8_t options) {
    const int delta=int8_t(options);
    if(command==1) { if(power>=0) power=!power; }
    else if(command==6) { brightness=7; temperature=0; }
    else if(command==2||command==3) { if(temperature>=0) temperature=level(temperature+delta); }
    else if(command==4||command==5) { if(brightness>=0) brightness=level(brightness+delta); }
  }
};
struct View {
  bool enabled=false,radio=false,verified=false,learning=false,busy=false;
  uint32_t id=0,revision=0,lastManualAt=0,txCount=0,rxCount=0,dropped=0;
  bool manualSeen=false;
  Estimate state;
  uint8_t selected=0; // 0 power, 1 brightness, 2 temperature, 3 reset, 4 setup
  char message[64]="조명 설정에서 시작";
  bool autoAllowed(uint32_t now) const {
    return enabled&&radio&&verified&&id&&state.power==1&&state.brightness>=0&&state.temperature>=0&&!busy &&
      (!manualSeen || uint32_t(now-lastManualAt)>=MANUAL_HOLD_MS);
  }
};
struct LuxWindow {
  float samples[5]={}; uint8_t count=0,head=0; uint32_t at=0;
  void clear() { count=head=0; }
  void add(float value,bool valid,uint32_t now) {
    if(!valid || !isfinite(value) || value<0) { clear(); return; }
    if(count && uint32_t(now-at)>1800) clear();
    samples[head]=value; head=(head+1)%5; if(count<5) ++count; at=now;
  }
  bool stats(uint32_t now,float& median,float& spread) const {
    if(count!=5 || uint32_t(now-at)>1800) return false;
    float sorted[5]; memcpy(sorted,samples,sizeof(sorted)); std::sort(sorted,sorted+5);
    median=sorted[2]; spread=sorted[4]-sorted[0]; return true;
  }
};
// Stable binary event record, retained in a bounded NVS-backed queue until Mac ACK.
struct Event {
  uint32_t boot=0,number=0,id=0,at=0,duration=0;
  float before=-1,after=-1,spreadBefore=0,spreadAfter=0;
  int8_t b0=-1,b1=-1,t0=-1,t1=-1,p0=-1,p1=-1;
  uint16_t flags=0; uint8_t source=0,reserved=0;
};
enum Flags : uint16_t { BadBefore=1,BadAfter=2,Unknown=4,ColorChanged=8,PowerChanged=16,Unstable=32,Automatic=64,Unverified=128,Interrupted=256 };
struct Capture {
  bool active=false; uint32_t lastAt=0; Event event; LuxWindow after;
  void action(uint32_t now,uint8_t source,const View& view,const LuxWindow& history) {
    if(!active) {
      active=true; event={}; event.at=now; event.source=source; event.id=view.id;
      event.b0=view.state.brightness; event.t0=view.state.temperature; event.p0=view.state.power;
      if(!history.stats(now,event.before,event.spreadBefore)) event.flags|=BadBefore;
      if(!view.verified) event.flags|=Unverified;
    } else if(event.source!=source) event.flags|=Interrupted;
    if(source==3) event.flags|=Automatic;
    lastAt=now; after.clear();
  }
  void sample(float lux,bool valid,uint32_t now) { if(active && uint32_t(now-lastAt)>=4000) after.add(lux,valid,now); }
  bool finish(uint32_t now,const View& view,Event& out) {
    if(!active || uint32_t(now-lastAt)<12000 || view.busy) return false;
    event.duration=now-event.at; event.b1=view.state.brightness; event.t1=view.state.temperature; event.p1=view.state.power;
    if(!after.stats(now,event.after,event.spreadAfter)) event.flags|=BadAfter;
    if(event.b0<0||event.b1<0||event.t0<0||event.t1<0) event.flags|=Unknown;
    if(event.t0!=event.t1) event.flags|=ColorChanged;
    if(event.p0!=1||event.p1!=1) event.flags|=PowerChanged;
    if(event.spreadBefore>std::max(3.f,event.before*.1f)||event.spreadAfter>std::max(3.f,event.after*.1f)) event.flags|=Unstable;
    if(!view.verified) event.flags|=Unverified;
    active=false; out=event; return true;
  }
};
} // namespace Lightbar
