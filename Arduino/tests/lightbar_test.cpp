#include <cassert>
#include <cstdio>
#include <RF24.h>
#include "../SpaceSmart/LightbarModel.h"
#include "../SpaceSmart/LightbarService.h"
#include "../SpaceSmart/DashboardController.h"
int main(){
  // Captured on-air vectors from lamperez's protocol documentation, independent of our encoder.
  const uint8_t known[]={0x53,0x39,0x14,0xdd,0x1c,0x49,0x34,0x12,0x01,0xb9,0x60,0xff,0x79,0x01,0x00,0x38,0x70};
  Lightbar::Packet p; assert(Lightbar::decode(known,17,p)&&p.id==0x01b960&&p.seq==0x79);
  uint8_t built[17];Lightbar::encode(p,built);assert(!memcmp(built,known,17));
  for(int bit=0;bit<136;bit++){uint8_t corrupt[17];memcpy(corrupt,known,17);corrupt[bit/8]^=1<<(bit%8);assert(!Lightbar::decode(corrupt,17,p));}
  uint8_t raw[17]={};for(int i=0;i<17;i++)raw[i]=uint8_t(known[i]<<5)|(i<16?known[i+1]>>3:0);assert(Lightbar::decodeRaw(raw,p));
  Lightbar::Estimate e{1,7,5};e.apply(5,0xff);assert(e.brightness==6);e.apply(3,0xfe);assert(e.temperature==3);e.apply(4,15);assert(e.brightness==15);e.apply(1,0);assert(e.power==0);e.apply(6,0);assert(e.brightness==7&&e.temperature==0&&e.power==0);
  Lightbar::LuxWindow h;for(int i=0;i<5;i++)h.add(100+i*.1,true,1000+i*1000);
  Lightbar::View v;v.id=1;v.state={1,4,0};v.verified=true;
  Lightbar::Capture capture;capture.action(5001,0,v,h);v.state.brightness=6;
  for(int i=0;i<13;i++)capture.sample(120,true,6000+i*1000);
  Lightbar::Event event;assert(capture.finish(18000,v,event)&&event.flags==0&&event.b0==4&&event.b1==6&&event.after==120);
  capture.action(19000,3,v,h);for(int i=0;i<13;i++)capture.sample(130,true,20000+i*1000);assert(capture.finish(33000,v,event)&&(event.flags&Lightbar::Automatic)&&(event.flags&Lightbar::BadBefore));
  v.enabled=v.radio=true;v.state={1,7,0};v.id=1;v.manualSeen=true;v.lastManualAt=UINT32_MAX-100;assert(!v.autoAllowed(500));assert(v.autoAllowed(600000));
  Dashboard::State ui;ui.page=Dashboard::Page::Light;Dashboard::push(ui,0);Dashboard::rotate(ui,1);Dashboard::push(ui,0);Dashboard::rotate(ui,1);assert(ui.lightRequest==1&&ui.lightDelta==1);Dashboard::back(ui);assert(!ui.editing);
  RF24 radio;LightbarService::begin(radio,true,0);assert(radio.transmitted.empty()&&!LightbarService::view().enabled);
  assert(!LightbarService::command("lamp 1 toggle",0));
  assert(LightbarService::command("lamp 4000000000 id ABCDEF",0));assert(LightbarService::view().id==0xabcdef);
  assert(!LightbarService::command("lamp 18446744073709551616 toggle",0));
  assert(LightbarService::command("lamp 2 enable 1",0));assert(LightbarService::command("lamp 3 mark 1",0));assert(LightbarService::command("lamp 4 verify 1",0));
  assert(LightbarService::command("lamp 5 reset",0));assert(!LightbarService::command("lamp 5 reset",0));
  for(uint32_t t=0;t<500;t++)LightbarService::tick(t,100,true,true);
  assert(radio.transmitted.size()==20);for(auto& b:radio.transmitted)assert(b==radio.transmitted[0]);
  assert(LightbarService::view().state.brightness==7);
  assert(!LightbarService::command("lamp 6 auto 999 1",601000));
  for(uint32_t t=1000;t<602000;t+=1000){LightbarService::sample(100,true,t);LightbarService::tick(t,100,true,true);}
  auto revision=LightbarService::view().revision;char command[96];snprintf(command,sizeof(command),"lamp 7 auto %u 1",revision);
  assert(LightbarService::command(command,602000));
  for(uint32_t t=602000;t<603000;t++)LightbarService::tick(t,100,true,true);
  assert(LightbarService::view().state.brightness==8&&radio.transmitted.size()==40);
  snprintf(command,sizeof(command),"lamp 8 auto %u 1",LightbarService::view().revision);assert(!LightbarService::command(command,603000));
  assert(LightbarService::command("lamp 9 enable 0",603000));assert(!LightbarService::command("lamp 10 toggle",603000));
  puts("PASS lightbar: captured CRC, corruption, RX alignment, signed steps, captures, UI, disabled boot, nonce bounds/idempotency, nonblocking bursts, stale/cooldown automation guards");
}
