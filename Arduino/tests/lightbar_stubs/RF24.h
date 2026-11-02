#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <array>
#include <cstring>
constexpr int RF24_2MBPS=2,RF24_PA_LOW=1;
class RF24 {
public:
 bool connected=true,sent=false,fail=false; uint8_t channel=68;
 std::vector<std::array<uint8_t,17>> transmitted;
 void stopListening(){} void startListening(){} void setAutoAck(bool){} void setRetries(int,int){} void disableCRC(){} void disableDynamicPayloads(){} void setAddressWidth(int){} void setPayloadSize(int){}
 void setChannel(int c){channel=c;} uint8_t getChannel(){return channel;} void setDataRate(int){} void setPALevel(int){} void openWritingPipe(uint64_t){} void openReadingPipe(int,uint64_t){}
 void flush_rx(){} void flush_tx(){} bool isChipConnected(){return connected;} bool available(){return false;} void read(void*,size_t){}
 void startWrite(const void* bytes,size_t n,bool){std::array<uint8_t,17> b{};memcpy(b.data(),bytes,n);transmitted.push_back(b);sent=!fail;}
 void whatHappened(bool& tx,bool& failed,bool& rx){tx=sent;failed=fail;rx=false;sent=false;}
};
