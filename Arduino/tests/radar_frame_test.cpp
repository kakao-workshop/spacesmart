#include "../SpaceSmartRadarTest/RadarFrame.h"
#include <cassert>
#include <vector>
#include <iostream>

std::vector<uint8_t> frame(uint8_t state, bool engineering=false) {
  const unsigned body=engineering ? 35 : 13;
  std::vector<uint8_t> f(body+10,0);
  const uint8_t header[]={0xF4,0xF3,0xF2,0xF1};
  memcpy(f.data(),header,4); f[4]=body; f[6]=engineering ? 1 : 2;
  f[7]=0xAA; f[8]=state; f[9]=0xF4; f[10]=1; f[11]=80;
  f[12]=100; f[14]=45; f[15]=90; f[body+4]=0x55;
  const uint8_t footer[]={0xF8,0xF7,0xF6,0xF5};
  memcpy(f.data()+body+6,footer,4); return f;
}
void feed(RadarFrame& p,const std::vector<uint8_t>& f) { for (auto b:f) p.push(b); }
int main() {
  RadarFrame p;
  for (unsigned s=0;s<4;++s) {
    auto f=frame(s); feed(p,f);
    assert(p.sample.state==s && p.states[s]==1);
    assert(p.sample.movingCm==500 && p.sample.stationaryCm==100);
    assert(p.sample.detectionCm==90 && p.sample.movingEnergy==80);
  }
  feed(p,frame(2,true)); assert(p.frames==5 && p.sample.type==1 && p.lastSize==45);
  auto corrupt=frame(1); corrupt.back()=0; feed(p,corrupt); assert(p.frames==5);
  corrupt=frame(1); corrupt[8]=4; feed(p,corrupt); assert(p.frames==5);
  feed(p,{0xF4,0xF3,0xF2,0xF1,0xFF,0xFF}); assert(p.frames==5);
  feed(p,{0xF4,0xF4,0xF3,0xF4,0xF3});
  feed(p,frame(0)); assert(p.frames==6 && p.sample.state==0 && p.invalid>=3);
  p.reset(); auto f=frame(3);
  for(size_t n=0;n+1<f.size();++n) { assert(!p.push(f[n])); assert(p.frames==0); }
  assert(p.push(f.back()) && p.frames==1);
  p.reset(); assert(!p.frames && !p.invalid && !p.lastSize);
  std::cout << "PASS: basic/engineering frames, all states, malformed frames, resync, truncation\n";
}
