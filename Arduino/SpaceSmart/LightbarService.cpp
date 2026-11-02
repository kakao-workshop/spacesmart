#include "LightbarService.h"
#include <Arduino.h>
#include <Preferences.h>
#include <RF24.h>
#include <esp_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
namespace LightbarService {
namespace {
RF24* rf=nullptr; Preferences prefs; bool storeOpen=false;
Lightbar::View v; Lightbar::LuxWindow history; Lightbar::Capture capture;
uint32_t bootID=0,lastCheck=0,lastReport=0,lastAuto=0,learnAt=0,candidate=0;
bool autoSent=false; uint8_t candidateSeq=0; bool haveCandidate=false;
uint8_t txSeq=0,lastRX=0; bool rxSeen=false; uint32_t rxAt=0;
float currentLux=0; bool luxOK=false,occupied=false;
struct Saved { uint32_t version=1,id=0; uint8_t enabled=0,verified=0,pad[2]={}; } saved;
struct EventStore { uint32_t version=1,next=1,dropped=0; uint8_t count=0,pad[3]={}; Lightbar::Event events[32]; } events;
struct Tx { uint8_t cmd,opt; } queue[8]; uint8_t count=0,copies=0; bool onAir=false;
uint32_t copyAt=0,airAt=0; uint8_t bytes[17];
char response[96]={}; uint32_t recent[8]={}; uint8_t recentAt=0;
void message(const char* s) { snprintf(v.message,sizeof(v.message),"%s",s); }
void saveConfig() {
  saved.id=v.id; saved.enabled=v.enabled; saved.verified=v.verified;
  if(storeOpen) prefs.putBytes("config",&saved,sizeof(saved));
}
void saveEvents() { if(storeOpen) prefs.putBytes("events",&events,sizeof(events)); v.dropped=events.dropped; }
void listen() { if(rf&&v.radio) rf->startListening(); }
void cancel() {
  count=copies=0; onAir=false; v.busy=false;
  if(rf&&v.radio) { rf->stopListening(); rf->flush_tx(); listen(); }
}
void invalidate() { v.state={}; v.revision++; if(capture.active) capture.event.flags|=Lightbar::Interrupted; }
void configure() {
  rf->stopListening(); rf->setAutoAck(false); rf->setRetries(0,0); rf->disableCRC();
  rf->disableDynamicPayloads(); rf->setAddressWidth(5); rf->setPayloadSize(17);
  rf->setChannel(68); rf->setDataRate(RF24_2MBPS); rf->setPALevel(RF24_PA_LOW);
  rf->openWritingPipe(0x5555555555ULL); rf->openReadingPipe(0,0xAAAAAAAAAAULL);
  rf->flush_rx(); rf->flush_tx(); listen();
}
void rememberManual(uint32_t now,uint8_t source) { if(source!=3) { v.lastManualAt=now; v.manualSeen=true; } }
bool enqueue(uint8_t cmd,uint8_t options,uint32_t now,uint8_t source) {
  if(count>=8) { message("전송 중 · 잠시 뒤 시도"); return false; }
  if(source!=3 && capture.active && capture.event.source==3 && v.busy) { cancel(); invalidate(); }
  capture.action(now,source,v,history); rememberManual(now,source);
  queue[count++]={cmd,options}; v.state.apply(cmd,options); ++v.revision; v.busy=true;
  message("전송 요청 · 상태는 추정"); return true;
}
bool absolute(bool temperature,int value,uint32_t now,uint8_t source) {
  if(value<0||value>15||count>6) return false;
  int old=temperature?v.state.temperature:v.state.brightness;
  if(old==value) return true;
  if(old<0) {
    // Saturating -16 is deferred by the bar until the following opposite command.
    if(!enqueue(temperature?3:5,uint8_t(-16),now,source)) return false;
    enqueue(temperature?2:4,uint8_t(value),now,source);
  } else enqueue(temperature?(value>old?2:3):(value>old?4:5),uint8_t(int8_t(value-old)),now,source);
  if(temperature) v.state.temperature=value; else v.state.brightness=value;
  return true;
}
bool number(const char* s,int64_t& value,int base=10) {
  if(!s||!*s) return false;
  char* end; errno=0; value=strtoll(s,&end,base); return *end==0 && errno!=ERANGE;
}
void result(uint32_t token,bool ok,const char* reason) {
  snprintf(response,sizeof(response),"la %lu %u %s %lu\n",(unsigned long)token,ok,reason,(unsigned long)v.revision);
  if(!ok) {
    const char* text=!strcmp(reason,"setup_required")?"조명 설정에서 등록하세요":!strcmp(reason,"busy")?"전송 중 · 잠시 뒤 시도":!strcmp(reason,"auto_paused")?"자동 조절 조건 대기":!strcmp(reason,"duplicate")?"이미 처리한 요청":!strcmp(reason,"limit")?"조절 범위 끝":"명령을 적용하지 못함";
    message(text);
  }
}
void takeRemote(const Lightbar::Packet& p,uint32_t now) {
  if(v.learning) {
    if(!haveCandidate || p.id!=candidate) { candidate=p.id; candidateSeq=p.seq; haveCandidate=true; }
    else if(p.seq!=candidateSeq) {
      v.id=p.id; v.learning=false; v.enabled=true; v.verified=false; rxSeen=false; invalidate(); saveConfig();
      message("리모컨 ID 저장 · 상태 확인");
    }
  }
  if(!v.enabled||p.id!=v.id) return;
  if(rxSeen&&p.seq==lastRX&&uint32_t(now-rxAt)<5000) return;
  // The radio has no acknowledgement. Missing remote actions make all estimates uncertain.
  if(rxSeen&&uint8_t(p.seq-lastRX)!=1) invalidate();
  lastRX=p.seq; rxSeen=true; rxAt=now; txSeq=p.seq;
  if(count) { cancel(); invalidate(); }
  capture.action(now,2,v,history); rememberManual(now,2);
  v.state.apply(p.command,p.options); ++v.revision; ++v.rxCount;
  message("리모컨 조작 · 조도 기록 중");
}
}
const Lightbar::View& view() { return v; }
void begin(RF24& radio,bool present,uint32_t now) {
  rf=&radio; v.radio=present; bootID=esp_random(); if(!bootID) bootID=1; txSeq=uint8_t(esp_random());
  storeOpen=prefs.begin("ss-light",false);
  Saved loaded;
  if(storeOpen&&prefs.getBytesLength("config")==sizeof(loaded)&&prefs.getBytes("config",&loaded,sizeof(loaded))==sizeof(loaded)&&loaded.version==1&&loaded.id<=0xffffff&&loaded.enabled<=1&&loaded.verified<=1) {
    v.id=loaded.id; v.enabled=loaded.enabled; v.verified=loaded.verified;
  }
  if(storeOpen&&prefs.getBytesLength("events")==sizeof(events)) {
    prefs.getBytes("events",&events,sizeof(events));
    if(events.version!=1||events.count>32) events={};
  }
  v.dropped=events.dropped;
  if(present) configure();
  lastCheck=now; message(v.enabled?"전원 상태 확인 필요":"조명 설정에서 시작");
}
void sample(float lux,bool valid,uint32_t now) { history.add(lux,valid,now); capture.sample(lux,valid,now); }
void tick(uint32_t now,float lux,bool valid,bool present) {
  currentLux=lux; luxOK=valid; occupied=present;
  if(!rf) return;
  if(v.learning&&uint32_t(now-learnAt)>60000) { v.learning=false; message("리모컨 등록 시간 초과"); }
  if(onAir) {
    bool sent=false,failed=false,received=false; rf->whatHappened(sent,failed,received);
    if(sent||failed||uint32_t(now-airAt)>=5) {
      onAir=false; rf->stopListening();
      if(!sent) { cancel(); invalidate(); message("무선 모듈 전송 오류"); }
      else { listen(); copyAt=now; if(++copies==20) { for(int i=1;i<count;i++)queue[i-1]=queue[i]; --count; copies=0; ++v.txCount; } }
    }
  } else if(v.radio) {
    for(int n=0;n<4&&rf->available();n++) { uint8_t raw[17]; rf->read(raw,sizeof(raw)); Lightbar::Packet p; if(Lightbar::decodeRaw(raw,p))takeRemote(p,now); }
    if(count&&uint32_t(now-copyAt)>=10) {
      if(!copies) Lightbar::encode({v.id,++txSeq,queue[0].cmd,queue[0].opt},bytes);
      rf->stopListening(); rf->startWrite(bytes,sizeof(bytes),false); onAir=true; airAt=now;
    }
  }
  v.busy=count||onAir;
  if(uint32_t(now-lastCheck)>=10000 && !v.busy) {
    lastCheck=now; bool ok=rf->isChipConnected();
    if(!ok&&v.radio) { v.radio=false; cancel(); invalidate(); message("nRF24 연결 확인"); }
    else if(ok&&(!v.radio||rf->getChannel()!=68)) { v.radio=true; invalidate(); configure(); }
  }
  Lightbar::Event e;
  if(capture.finish(now,v,e)) {
    e.boot=bootID; e.number=events.next++; if(!events.next)events.next=1;
    if(events.count==32) { for(int i=1;i<32;i++)events.events[i-1]=events.events[i]; --events.count; ++events.dropped; }
    events.events[events.count++]=e; saveEvents();
  }
}
bool command(const char* text,uint32_t now,uint8_t source) {
  char line[96]; if(strlen(text)>=sizeof(line))return false; strcpy(line,text);
  char* fields[8]; size_t n=0; char* save=nullptr;
  for(char* p=strtok_r(line," ",&save);p;p=strtok_r(nullptr," ",&save)) { if(n==8)return false; fields[n++]=p; }
  int64_t nonce=0,a=0,b=0;
  if(n<3||strcmp(fields[0],"lamp")||!number(fields[1],nonce)||nonce<0||uint64_t(nonce)>UINT32_MAX)return false;
  const uint32_t token=uint32_t(nonce); const char* op=fields[2];
  if(token) { for(auto old:recent)if(old==token){result(token,false,"duplicate");return false;} recent[recentAt++%8]=token; }
  auto done=[&](bool ok,const char* reason){result(token,ok,reason);return ok;};
  auto queued=[&](bool ok){return done(ok,ok?"queued":"busy");};
  if(!strcmp(op,"ack")&&n==5&&number(fields[3],a)&&number(fields[4],b)&&a>=0&&uint64_t(a)<=UINT32_MAX&&b>=0&&uint64_t(b)<=UINT32_MAX) {
    if(events.count&&events.events[0].boot==uint32_t(a)&&events.events[0].number==uint32_t(b)) {
      for(int i=1;i<events.count;i++)events.events[i-1]=events.events[i]; --events.count; saveEvents();
    } return true;
  }
  if(!strcmp(op,"enable")&&n==4&&number(fields[3],a)&&a>=0&&a<=1) {
    cancel(); v.enabled=a; invalidate(); capture.active=false; saveConfig(); return done(true,"configured");
  }
  if(!strcmp(op,"id")&&n==4&&strlen(fields[3])==6&&number(fields[3],a,16)&&a>0&&a<=0xffffff) {
    cancel(); v.id=a; v.verified=false; invalidate(); rxSeen=false; capture.active=false; saveConfig(); return done(true,"id_saved");
  }
  if(!strcmp(op,"learn")&&n==3&&v.radio) {
    cancel(); v.learning=true; learnAt=now; haveCandidate=false; message("60초 내 리모컨 두 번 조작"); return done(true,"listening");
  }
  if(!strcmp(op,"verify")&&n==4&&number(fields[3],a)&&a>=0&&a<=1&&v.id) {
    v.verified=a; saveConfig(); return done(true,"verification_saved");
  }
  if(!strcmp(op,"mark")&&n==4&&number(fields[3],a)&&a>=0&&a<=1) {
    cancel(); capture.active=false; v.state.power=a; ++v.revision; rememberManual(now,source); return done(true,"state_marked");
  }
  if(!strcmp(op,"unknown")&&n==3) { cancel(); invalidate(); capture.active=false; return done(true,"unknown"); }
  if(!v.enabled||!v.id||!v.radio) return done(false,"setup_required");
  if(!strcmp(op,"auto")&&n==5&&number(fields[3],a)&&number(fields[4],b)&&a>=0&&uint64_t(a)<=UINT32_MAX&&(b==1||b==-1)) {
    if(uint32_t(a)!=v.revision||!v.autoAllowed(now)||!luxOK||!occupied||capture.active||(autoSent&&uint32_t(now-lastAuto)<60000))return done(false,"auto_paused");
    int target=v.state.brightness+int(b); if(target<0||target>15)return done(false,"limit");
    bool ok=absolute(false,target,now,3); if(ok){lastAuto=now;autoSent=true;} return done(ok,ok?"auto_queued":"busy");
  }
  if(!strcmp(op,"toggle")&&n==3) return queued(enqueue(1,0,now,source));
  if((!strcmp(op,"reset")||!strcmp(op,"pair"))&&n==3) {
    if(!strcmp(op,"pair")) { v.verified=false; saveConfig(); }
    return queued(enqueue(6,0,now,source));
  }
  if((!strcmp(op,"brightness")||!strcmp(op,"temperature"))&&n==4&&number(fields[3],a)&&a>=0&&a<=15) return queued(absolute(!strcmp(op,"temperature"),a,now,source));
  if((!strcmp(op,"bri")||!strcmp(op,"temp"))&&n==4&&number(fields[3],a)&&a!=0&&a>=-15&&a<=15) {
    uint8_t cmd=!strcmp(op,"bri")?(a>0?4:5):(a>0?2:3);
    return queued(enqueue(cmd,uint8_t(int8_t(a)),now,source));
  }
  return done(false,"invalid_command");
}
void localAction(uint8_t item,int delta,uint32_t now) {
  char text[96]; const char* fixed=nullptr;
  if(item==0)fixed="toggle";
  else if(item==1||item==2) { snprintf(text,sizeof(text),"lamp 0 %s %d",item==1?"bri":"temp",std::max(-15,std::min(15,delta))); command(text,now,0);return; }
  else if(item==3)fixed="reset";
  else if(item==10)fixed=v.enabled?"enable 0":"enable 1";
  else if(item==11)fixed="learn";
  else if(item==12) {
    if(!v.id) { v.id=(esp_random()&0xffffff); if(!v.id)v.id=1; saveConfig(); }
    v.enabled=true; saveConfig(); fixed="pair";
  }
  else if(item==13)fixed="mark 1";
  else if(item==14)fixed="mark 0";
  else if(item==15)fixed=v.verified?"verify 0":"verify 1";
  if(fixed) { snprintf(text,sizeof(text),"lamp 0 %s",fixed);command(text,now,0); }
}
bool report(char* out,size_t cap,uint32_t now) {
  if(response[0]) { snprintf(out,cap,"%s",response); response[0]=0; return true; }
  if(uint32_t(now-lastReport)<2000)return false; lastReport=now;
  static bool eventTurn=false; eventTurn=!eventTurn;
  if(eventTurn&&events.count) {
    const auto& e=events.events[0];
    snprintf(out,cap,"le %lu %lu %06lX %lu %u %u %.2f %.2f %.2f %.2f %d %d %d %d %d %d %lu\n",(unsigned long)e.boot,(unsigned long)e.number,(unsigned long)e.id,(unsigned long)(e.boot==bootID?uint32_t(now-e.at):0),e.source,e.flags|(e.boot!=bootID?512:0),e.before,e.after,e.spreadBefore,e.spreadAfter,e.b0,e.b1,e.t0,e.t1,e.p0,e.p1,(unsigned long)e.duration);
  } else {
    const unsigned hold=v.manualSeen?std::min(uint32_t(Lightbar::MANUAL_HOLD_MS),uint32_t(now-v.lastManualAt)):Lightbar::MANUAL_HOLD_MS;
    snprintf(out,cap,"ls %lu %lu %06lX %u %u %u %d %d %d %lu %u %.2f %u %u %lu %lu %lu %u\n",(unsigned long)bootID,(unsigned long)now,(unsigned long)v.id,v.enabled,v.radio,v.verified,v.state.power,v.state.brightness,v.state.temperature,(unsigned long)v.revision,v.busy,luxOK?currentLux:-1,occupied,(Lightbar::MANUAL_HOLD_MS-hold)/1000,(unsigned long)v.txCount,(unsigned long)v.rxCount,(unsigned long)v.dropped,v.learning);
  } return true;
}
} // namespace LightbarService
