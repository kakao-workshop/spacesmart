/*
 * LD2410C isolated test, ESP32S3 Dev Module, USB Serial 115200 / New Line.
 * LD2410C VCC->5V, GND->common GND, TX->GPIO7, RX->GPIO18.
 * Commands: status | raw | pins | baud 256000 | help
 * Radar configuration is preserved. This sketch only receives UART data.
 */
#include <Arduino.h>
#include "RadarFrame.h"
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Select ESP32S3 Dev Module"
#endif

constexpr uint8_t RADAR_RX = 7, RADAR_TX = 18;
HardwareSerial radar(1);
RadarFrame parser;
uint32_t baudRate = 256000, bytes = 0, lastFrameAt = 0, lastReport = 0;
uint32_t previousFrames = 0, previousBytes = 0;
uint8_t firstBytes[64] = {};
size_t firstSize = 0;
char command[40];
size_t commandSize = 0;
bool overlong = false;

const char* stateName(uint8_t state) {
  const char* names[] = {"EMPTY", "MOVING", "STATIONARY", "MOVING+STATIONARY"};
  return state < 4 ? names[state] : "UNKNOWN";
}
bool fresh() { return parser.frames && uint32_t(millis() - lastFrameAt) < 2000; }

void startRadar(uint32_t baud) {
  radar.end();
  radar.setRxBufferSize(2048);
  radar.begin(baud, SERIAL_8N1, RADAR_RX, RADAR_TX);
  baudRate = baud; parser.reset(); bytes = 0; firstSize = 0;
  previousFrames = previousBytes = 0; lastReport = millis(); lastFrameAt = 0;
  Serial.printf("[UART] RX=%u TX=%u baud=%lu receive_only\n", RADAR_RX, RADAR_TX,
                static_cast<unsigned long>(baudRate));
}

void showStatus() {
  Serial.printf("[STATUS] uptime_s=%lu baud=%lu bytes=%lu valid_frames=%lu invalid=%lu fresh=%s state=%s\n",
    static_cast<unsigned long>(millis()/1000), static_cast<unsigned long>(baudRate),
    static_cast<unsigned long>(bytes), static_cast<unsigned long>(parser.frames),
    static_cast<unsigned long>(parser.invalid), fresh() ? "yes" : "no",
    fresh() ? stateName(parser.sample.state) : "UNKNOWN");
  Serial.printf("[STATES] empty=%lu moving=%lu stationary=%lu both=%lu\n",
    static_cast<unsigned long>(parser.states[0]), static_cast<unsigned long>(parser.states[1]),
    static_cast<unsigned long>(parser.states[2]), static_cast<unsigned long>(parser.states[3]));
}
void dump(const char* label, const uint8_t* data, size_t size) {
  Serial.printf("[%s] count=%u", label, unsigned(size));
  for (size_t n = 0; n < size; ++n) Serial.printf(" %02X", data[n]);
  Serial.println();
}
void showHelp() {
  Serial.println("SpaceSmartRadarTest v1 | Serial 115200 / New Line");
  Serial.println("Commands: status | raw | pins | baud <rate> | help");
  Serial.println("Rates: 9600 19200 38400 57600 115200 230400 256000 460800");
  Serial.println("TX from LD2410C -> GPIO7; RX -> GPIO18; VCC=5V; common GND.");
}
void inspectPins() {
  radar.end();
  pinMode(RADAR_TX, INPUT); // Release ESP TX while inspecting.
  pinMode(RADAR_RX, INPUT);
  unsigned high = 0;
  for (unsigned n=0; n<100; ++n) { high += digitalRead(RADAR_RX) == HIGH; delay(1); }
  Serial.printf("[PIN] RX7 input_no_pullup high=%u/100\n", high);
  pinMode(RADAR_RX, INPUT_PULLUP);
  delay(5); high = 0;
  for (unsigned n=0; n<100; ++n) { high += digitalRead(RADAR_RX) == HIGH; delay(1); }
  Serial.printf("[PIN] RX7 input_pullup high=%u/100\n", high);
  startRadar(baudRate);
}
void execute() {
  command[commandSize] = 0;
  if (!strcmp(command,"status")) showStatus();
  else if (!strcmp(command,"raw")) {
    showStatus(); dump("FIRST_BYTES",firstBytes,firstSize);
    dump("LAST_VALID_FRAME",parser.lastFrame,parser.lastSize);
  } else if (!strcmp(command,"help")) showHelp();
  else if (!strcmp(command,"pins")) inspectPins();
  else {
    const uint32_t rates[] = {9600,19200,38400,57600,115200,230400,256000,460800};
    for (uint32_t rate : rates) {
      char expected[24]; snprintf(expected,sizeof(expected),"baud %lu",static_cast<unsigned long>(rate));
      if (!strcmp(command,expected)) { startRadar(rate); return; }
    }
    Serial.println("[COMMAND ERROR] use help");
  }
}
void setup() {
  Serial.begin(115200);
  const uint32_t start = millis();
  while (!Serial && millis()-start < 2500) delay(10);
  showHelp(); startRadar(256000);
}
void loop() {
  for (unsigned n=0; n<1024 && radar.available(); ++n) {
    const uint8_t value = radar.read(); ++bytes;
    if (firstSize < sizeof(firstBytes)) firstBytes[firstSize++] = value;
    if (parser.push(value)) lastFrameAt = millis();
  }
  for (unsigned n=0; n<80 && Serial.available(); ++n) {
    const char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (overlong) Serial.println("[COMMAND ERROR] line too long");
      else if (commandSize) execute();
      commandSize = 0; overlong = false;
    } else if (!overlong) {
      if (commandSize < sizeof(command)-1) command[commandSize++] = c;
      else overlong = true;
    }
  }
  if (uint32_t(millis()-lastReport) >= 1000) {
    lastReport = millis();
    if (fresh()) {
      const auto& s = parser.sample;
      Serial.printf("[RADAR OK] state=%s moving_cm=%u energy=%u stationary_cm=%u energy=%u detection_cm=%u frames_s=%lu bytes_s=%lu\n",
        stateName(s.state),s.movingCm,s.movingEnergy,s.stationaryCm,s.stationaryEnergy,s.detectionCm,
        static_cast<unsigned long>(parser.frames-previousFrames),static_cast<unsigned long>(bytes-previousBytes));
    } else Serial.printf("[RADAR %s] baud=%lu bytes_s=%lu valid_frames=%lu state=UNKNOWN\n",
        parser.frames ? "STALE" : "NO DATA",static_cast<unsigned long>(baudRate),
        static_cast<unsigned long>(bytes-previousBytes),static_cast<unsigned long>(parser.frames));
    previousFrames=parser.frames; previousBytes=bytes;
  }
  delay(1);
}
