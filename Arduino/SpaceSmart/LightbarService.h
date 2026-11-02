#pragma once
#include "LightbarModel.h"
class RF24;
namespace LightbarService {
void begin(RF24& radio,bool present,uint32_t now);
void tick(uint32_t now,float lux,bool luxValid,bool present);
void sample(float lux,bool valid,uint32_t now);
const Lightbar::View& view();
// Strict allowlist; source: 0 device, 1 Mac, 2 original remote, 3 automation.
bool command(const char* text,uint32_t now,uint8_t source=1);
void localAction(uint8_t item,int delta,uint32_t now);
// One newline-delimited status/event at a time. Events replay until explicit ACK.
bool report(char* out,size_t capacity,uint32_t now);
}
