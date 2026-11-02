#include "SpaceSmartApp.h"

#include <Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_VEML7700.h>
#include <Arduino.h>
#include <Preferences.h>
#include <RF24.h>
#include <SPI.h>
#include <SensirionI2cScd4x.h>
#include <Wire.h>
#include <ctype.h>
#include <esp_heap_caps.h>
#include <math.h>

#include "AppLogic.h"
#include "Config.h"
#include "DashboardController.h"
#include "DashboardLink.h"
#include "DashboardRenderer.h"
#include "RadarFrame.h"
#include "LightbarService.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Select esp32 by Espressif Systems > ESP32S3 Dev Module."
#endif

namespace {
using Logic::Ai;
using Logic::Traffic;

// Two independent hardware SPI controllers, matching the actual wiring.
SPIClass lcdSpi(FSPI);
SPIClass radioSpi(HSPI);
Adafruit_ST7789 display(&lcdSpi, Config::LCD_CS, Config::LCD_DC, Config::LCD_RST);
RF24 radio(Config::RADIO_CE, Config::RADIO_CSN, 1000000);
HardwareSerial radarUart(1);
RadarFrame radar;
uint32_t radarBytes = 0;
SensirionI2cScd4x scd;
Adafruit_VEML7700 lightSensor;
Adafruit_NeoPixel aiPixels(Config::AI_LED_COUNT, Config::AI_LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel trafficPixels(Config::TRAFFIC_LED_COUNT, Config::TRAFFIC_LED_PIN,
                                NEO_GRB + NEO_KHZ800);
Dashboard::Button pushButton, k0Button;
Logic::DebouncedButton leftTouch, rightTouch;
Logic::LineBuffer commandLine;
Dashboard::State ui;
Dashboard::History history;
Preferences preferences;
bool preferencesOpen = false, preferencesDirty = false;
uint32_t preferenceChangedAt = 0;
uint16_t *frame = nullptr, *shownFrame = nullptr;
int flushRow = 0, activeRotation = -1;
bool flushing = false, fullRefresh = true;
uint16_t transferStrip[320 * 8];
uint32_t renderedFrames = 0, displayStrips = 0;
uint32_t lcdTestUntil = 0;
uint8_t bootStage = 0;
uint32_t bootStageAt = 0;
bool bootScheduled = false;

struct Measurements {
  uint16_t co2 = 0;
  float temperature = 0, humidity = 0, lux = 0;
  bool co2Valid = false, luxValid = false, radarSeen = false, present = false;
  bool moving = false, stationary = false, luxSaturated = false;
  uint16_t movingCm = 0, stationaryCm = 0;
  uint32_t co2At = 0, luxAt = 0, radarAt = 0;
} values;

bool scdStarted = false, luxStarted = false, radioConnected = false;
bool ledsActive = false, sensorsActive = false, radioActive = false, bleActive = false,
     radarActive = false;
bool trafficAuto = true, pageChanged = true;
uint8_t brightness = Config::INITIAL_BRIGHTNESS;
Ai aiState = Ai::Idle;
Traffic autoTraffic = Traffic::Unknown, manualTraffic = Traffic::Off;
uint32_t lastSensors = 0, lastDisplay = 0, lastLeds = 0, lastReport = 0;
uint32_t lastRadioCheck = 0, lastScdErrorLog = 0;
bool ledTest = false;
uint32_t ledTestAt = 0;

void preferenceChanged(uint32_t now) {
  brightness = ui.brightness;
  preferencesDirty = true;
  ui.prefsSaved = false;
  preferenceChangedAt = now;
  pageChanged = true;
}

void servicePreferences(uint32_t now) {
  if (!preferencesDirty || uint32_t(now - preferenceChangedAt) < 2000) return;
  preferencesDirty = false;
  // Save one small versioned record, only after editing has settled.
  const uint8_t record[] = {1, ui.theme, ui.rotation, ui.brightness, ui.focus.minutes};
  ui.prefsSaved =
      preferencesOpen && preferences.putBytes("ui", record, sizeof(record)) == sizeof(record);
  if (!ui.prefsSaved) Serial.println("WARN settings could not be saved");
}

void loadPreferences() {
  ui.rotation = Config::LCD_ROTATION;
  ui.brightness = Config::INITIAL_BRIGHTNESS;
  preferencesOpen = preferences.begin("space-smart", false);
  uint8_t record[5] = {};
  if (preferencesOpen && preferences.getBytesLength("ui") == sizeof(record) &&
      preferences.getBytes("ui", record, sizeof(record)) == sizeof(record) && record[0] == 1 &&
      record[1] < Dashboard::THEME_COUNT && (record[2] == 0 || record[2] == 3) &&
      record[3] <= Config::MAX_BRIGHTNESS && record[4] >= 5 && record[4] <= 120) {
    ui.theme = record[1];
    ui.rotation = record[2];
    ui.brightness = record[3];
    ui.focus.setMinutes(record[4]);
  }
  brightness = ui.brightness;
  ui.prefsSaved = preferencesOpen;
  ui.co2Yellow = Config::CO2_YELLOW_PPM;
  ui.co2Red = Config::CO2_RED_PPM;
}

portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;
volatile int32_t encoderEdges = 0;
volatile uint8_t encoderPrevious = 0;
// DRAM placement keeps ISR access valid while flash is unavailable.
DRAM_ATTR const int8_t encoderTransitions[16] = {0,  -1, 1, 0, 1, 0, 0,  -1,
                                                 -1, 0,  0, 1, 0, 1, -1, 0};

void ARDUINO_ISR_ATTR encoderInterrupt() {
  portENTER_CRITICAL_ISR(&encoderMux);
  const uint8_t state = (digitalRead(Config::ENCODER_A) << 1) | digitalRead(Config::ENCODER_B);
  encoderEdges += encoderTransitions[(encoderPrevious << 2) | state];
  encoderPrevious = state;
  portEXIT_CRITICAL_ISR(&encoderMux);
}

bool co2Fresh(uint32_t now) {
  return Logic::fresh(values.co2Valid, values.co2At, now, Config::CO2_STALE_MS);
}
bool luxFresh(uint32_t now) {
  return Logic::fresh(values.luxValid, values.luxAt, now, Config::LUX_STALE_MS);
}
bool radarFresh(uint32_t now) {
  return Logic::fresh(values.radarSeen, values.radarAt, now, Config::RADAR_STALE_MS);
}
const char* aiName() {
  switch (aiState) {
    case Ai::Thinking:
      return "THINKING";
    case Ai::Done:
      return "DONE";
    case Ai::Error:
      return "ERROR";
    default:
      return "IDLE";
  }
}
const char* trafficName(Traffic state) {
  switch (state) {
    case Traffic::Green:
      return "GREEN";
    case Traffic::Yellow:
      return "YELLOW";
    case Traffic::Red:
      return "RED";
    case Traffic::Off:
      return "OFF";
    default:
      return "NO DATA";
  }
}
Traffic currentTraffic() { return trafficAuto ? autoTraffic : manualTraffic; }

bool i2cPresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void scanI2c() {
  Serial.println("I2C scan (SDA=4 SCL=5): expected SCD40=0x62, VEML7700=0x10");
  for (uint8_t address = 1; address < 127; ++address) {
    if (i2cPresent(address)) Serial.printf("  found 0x%02X\n", address);
  }
}

void beginLeds() {
  if (ledsActive) return;
  ledsActive = true;
  aiPixels.begin();
  trafficPixels.begin();
  aiPixels.clear();
  trafficPixels.clear();
  aiPixels.show();
  trafficPixels.show();
}
void beginSensors() {
  if (sensorsActive) return;
  sensorsActive = true;
  Wire.begin(Config::I2C_SDA, Config::I2C_SCL, 100000);
  Wire.setTimeOut(30);
  scd.begin(Wire, SCD40_I2C_ADDR_62);
  if (i2cPresent(0x62)) {
    // SCD40 has no wakeUp command. Stop handles MCU-only reset while the
    // sensor is still measuring; the official driver includes its 500 ms wait.
    const int16_t stopped = scd.stopPeriodicMeasurement();
    scdStarted = stopped == 0 && scd.startPeriodicMeasurement() == 0;
  }
  Serial.printf("SCD40: %s\n",
                scdStarted ? "measuring, first sample ~5 seconds" : "not started; check wiring");
  // Probe before begin; do not repeatedly allocate library objects on failures.
  luxStarted = i2cPresent(0x10) && lightSensor.begin(&Wire);
  if (luxStarted) {
    lightSensor.setGain(VEML7700_GAIN_1_8);
    lightSensor.setIntegrationTime(VEML7700_IT_100MS, false);
  }
  Serial.printf("VEML7700: %s\n", luxStarted ? "initialized" : "not found");
}
void beginRadio() {
  if (radioActive) return;
  radioActive = true;
  radioSpi.begin(Config::RADIO_SCK, Config::RADIO_MISO, Config::RADIO_MOSI, Config::RADIO_CSN);
  radioConnected = radio.begin(&radioSpi) && radio.isChipConnected();
  LightbarService::begin(radio,radioConnected,millis());
  Serial.printf("nRF24: %s\n", radioConnected ? "SPI OK; MJGJD01YL listening; no boot transmission" : "not found");
}
void beginBle() {
  if (bleActive) return;
  ui.bleEnabled = DashboardLink::begin();
  Serial.printf("Dashboard BLE: %s\n",
                ui.bleEnabled ? "SpaceSmart advertising" : "initialization failed");

  bleActive = ui.bleEnabled;
}
void beginRadar() {
  if (radarActive) return;
  radarActive = true;
  // Start radar last so blocking LCD/sensor/BLE initialization does not fill UART.
  radarUart.setRxBufferSize(2048);
  radarUart.begin(Config::RADAR_BAUD, SERIAL_8N1, Config::RADAR_RX, Config::RADAR_TX);
}

void serviceBoot(uint32_t now) {
  if (!bootScheduled || flushing || uint32_t(now - bootStageAt) < 1500) return;
  // Verified on the assembled board: complete the LCD's first frame, then
  // initialize peripherals in this order, with a settling interval in between.
  switch (bootStage++) {
    case 0:
      beginLeds();
      break;
    case 1:
      beginSensors();
      break;
    case 2:
      beginRadar();
      break;
    case 3:
      beginRadio();
      break;
    case 4:
      beginBle();
      bootScheduled = false;
      break;
    default:
      bootScheduled = false;
      break;
  }
  bootStageAt = millis();
  pageChanged = true;
}

void printHelp() {
  Serial.println("\nSpaceSmart integrated v2.3 — serial commands, 115200 baud, newline:");
  Serial.println("  status | help | i2c | test | page | radarraw");
  Serial.println("  brightness 0..64");
  Serial.println("  mode env|graph|pc|custom|ai|focus|clock|diagnostics|settings|light|ledtest|lightsetup");
  Serial.println("  theme mint|blue|amber|violet | orientation landscape|portrait");
  Serial.println("  pc CPU_percent RAM_percent RX_KiBs TX_KiBs | clock HH MM SS");
  Serial.println("  agent claude|codex STATUS ELAPSED_SECONDS |task title");
  Serial.println("  ai idle | ai thinking | ai done | ai error");
  Serial.println("  traffic auto | traffic green | traffic yellow | traffic red | traffic off");
  Serial.println(
      "Knob=screen/item, PUSH=select, hold PUSH=settings, K0=back, hold K0=home/reset timer.");
  Serial.println("lamp TOKEN enable 0|1 / learn / id HEX6 / toggle / bri -15..15 / temp -15..15 / reset / pair / mark 0|1 / verify 0|1");
}

void printStatus(uint32_t now) {
  // One JSON object per line. Unknown readings are null, never fake zero values.
  Serial.print("{\"co2_ppm\":");
  if (co2Fresh(now))
    Serial.print(values.co2);
  else
    Serial.print("null");
  Serial.print(",\"temperature_c\":");
  if (co2Fresh(now))
    Serial.print(values.temperature, 1);
  else
    Serial.print("null");
  Serial.print(",\"humidity_pct\":");
  if (co2Fresh(now))
    Serial.print(values.humidity, 1);
  else
    Serial.print("null");
  Serial.print(",\"lux\":");
  if (luxFresh(now))
    Serial.print(values.lux, 1);
  else
    Serial.print("null");
  Serial.print(",\"presence\":");
  if (radarFresh(now))
    Serial.print(values.present ? "true" : "false");
  else
    Serial.print("null");
  Serial.print(",\"moving_cm\":");
  if (radarFresh(now) && values.moving)
    Serial.print(values.movingCm);
  else
    Serial.print("null");
  Serial.print(",\"stationary_cm\":");
  if (radarFresh(now) && values.stationary)
    Serial.print(values.stationaryCm);
  else
    Serial.print("null");
  Serial.printf(
      ",\"radio_spi\":%s,\"ai\":\"%s\",\"traffic\":\"%s\","
      "\"traffic_mode\":\"%s\",\"brightness\":%u,\"uptime_s\":%lu,",
      radioConnected ? "true" : "false", aiName(), trafficName(currentTraffic()),
      trafficAuto ? "auto" : "manual", brightness, static_cast<unsigned long>(now / 1000));
  Serial.printf(
      "\"screen\":%u,\"theme\":\"%s\",\"rotation\":%u,\"ble\":%s,"
      "\"settings_saved\":%s,\"display_buffer\":%s,",
      unsigned(ui.page), Dashboard::THEMES[ui.theme].id, ui.rotation,
      DashboardLink::connected() ? "true" : "false", ui.prefsSaved ? "true" : "false",
      ui.displayReady ? "true" : "false");
  unsigned customMask = 0;
  for (unsigned i = 0; i < 3; ++i) if (ui.custom[i].valid(now)) customMask |= 1u << i;
  Serial.printf("\"custom_mask\":%u,\"led_test\":%s,", customMask, ui.ledTest.active ? "true" : "false");
  const auto& lamp=LightbarService::view();
  Serial.printf("\"lamp_enabled\":%s,\"lamp_id\":%lu,\"lamp_verified\":%s,\"lamp_tx\":%lu,",lamp.enabled?"true":"false",(unsigned long)lamp.id,lamp.verified?"true":"false",(unsigned long)lamp.txCount);
  Serial.printf(
      "\"version\":\"2.3\",\"width\":%d,\"height\":%d,\"radar_frames\":%lu,"
      "\"radar_invalid\":%lu,\"radar_bytes\":%lu,\"pc_fresh\":%s,"
      "\"claude_state\":%u,\"codex_state\":%u,\"heap\":%lu,\"psram\":%lu}\n",
      display.width(), display.height(), static_cast<unsigned long>(radar.frames),
      static_cast<unsigned long>(radar.invalid), static_cast<unsigned long>(radarBytes),
      Dashboard::fresh(ui.computer.received, ui.computer.at, now, Dashboard::PC_TTL) ? "true"
                                                                                     : "false",
      unsigned(ui.agents[0].visibleStatus(now)), unsigned(ui.agents[1].visibleStatus(now)),
      static_cast<unsigned long>(ESP.getFreeHeap()),
      static_cast<unsigned long>(ESP.getFreePsram()));
}

void changePage() {
  if (ui.page == Dashboard::Page::LightSetup) ui.page=Dashboard::Page::Light;
  if (ui.page == Dashboard::Page::LedTest) { ui.ledTest.stop(); ui.page = Dashboard::Page::Settings; }
  ui.detail = ui.editing = false;
  Dashboard::rotate(ui, 1);
  pageChanged = true;
}

void inspectRadarBytes() {
  // Read-only diagnostic: consumes UART input for 500 ms without sending any
  // commands or altering the LD2410C's settings. Normal parsing resumes after it.
  uint8_t sample[64];
  size_t kept = 0;
  uint32_t count = 0;
  const uint32_t start = millis();
  while (static_cast<uint32_t>(millis() - start) < 500) {
    while (radarUart.available()) {
      const int byte = radarUart.read();
      if (byte < 0) break;
      ++count;
      if (kept < sizeof(sample)) sample[kept++] = static_cast<uint8_t>(byte);
    }
    delay(1);
  }
  Serial.printf("RADAR RAW: RX=%u TX=%u baud=%lu bytes/500ms=%lu RX_level=%d\n", Config::RADAR_RX,
                Config::RADAR_TX, static_cast<unsigned long>(Config::RADAR_BAUD),
                static_cast<unsigned long>(count), digitalRead(Config::RADAR_RX));
  Serial.print("HEX:");
  for (size_t i = 0; i < kept; ++i) Serial.printf(" %02X", sample[i]);
  Serial.println();
}

void command(char* line, uint32_t now) {
  while (isspace(static_cast<unsigned char>(*line))) ++line;
  size_t length = strlen(line);
  while (length && isspace(static_cast<unsigned char>(line[length - 1]))) line[--length] = '\0';
  // Preserve UTF-8 task names and capitalization in incoming telemetry.
  if (!strncmp(line, "pc ", 3) || !strncmp(line, "clock ", 6) || !strncmp(line, "agent ", 6) || !strncmp(line, "custom ", 7)) {
    Serial.println(Dashboard::telemetry(line, ui, now) ? "OK" : "ERR telemetry");
    return;
  }
  if (!strncmp(line,"lamp ",5)) { Serial.println(LightbarService::command(line,now,0)?"OK lamp":"ERR lamp"); return; }
  for (size_t i = 0; i < length; ++i)
    line[i] = static_cast<char>(tolower(static_cast<unsigned char>(line[i])));
  if (!length) return;
  if (!strcmp(line, "help"))
    printHelp();
  else if (!strncmp(line, "hardware ", 9)) {
    const char* part = line + 9;
    if (!strcmp(part, "leds") || !strcmp(part, "all")) beginLeds();
    if (!strcmp(part, "sensors") || !strcmp(part, "all")) beginSensors();
    if (!strcmp(part, "radio") || !strcmp(part, "all")) beginRadio();
    if (!strcmp(part, "ble") || !strcmp(part, "all")) beginBle();
    if (!strcmp(part, "radar") || !strcmp(part, "all")) beginRadar();
    fullRefresh = true;
    pageChanged = true;
    Serial.printf("HARDWARE leds=%u sensors=%u radio=%u ble=%u radar=%u\n", ledsActive,
                  sensorsActive, radioActive, bleActive, radarActive);
  } else if (!strcmp(line, "lcdtest")) {
    lcdTestUntil = now + 600000;
    flushing = false;
    fullRefresh = true;
    pageChanged = true;
    display.fillScreen(ST77XX_BLACK);
    const uint16_t colors[] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE, ST77XX_WHITE};
    for (int i = 0; i < 4; ++i) display.fillRect(i * 80, 40, 80, 160, colors[i]);
    Serial.printf("LCD TEST (screen to exit), frames=%lu strips=%lu first_pixel=%04X\n",
                  static_cast<unsigned long>(renderedFrames),
                  static_cast<unsigned long>(displayStrips), frame ? frame[0] : 0);
  } else if (!strcmp(line, "screen")) {
    lcdSpi.end();
    lcdSpi.begin(Config::LCD_SCK, -1, Config::LCD_MOSI, Config::LCD_CS);
    display.init(Config::LCD_WIDTH, Config::LCD_HEIGHT);
    display.setSPISpeed(Config::LCD_SPI_HZ);
    display.setRotation(ui.rotation);
    display.invertDisplay(Config::LCD_INVERT);
    flushing = false;
    fullRefresh = true;
    pageChanged = true;
    lcdTestUntil = 0;
  } else if (!strcmp(line, "status"))
    printStatus(now);
  else if (!strcmp(line, "i2c"))
    scanI2c();
  else if (!strcmp(line, "radarraw"))
    inspectRadarBytes();
  else if (!strcmp(line, "page"))
    changePage();
  else if (!strcmp(line, "test")) {
    ledTest = true;
    ledTestAt = now;
    Serial.println("LED test: RED -> GREEN -> BLUE, then restore (3 seconds).");
  } else if (!strncmp(line, "brightness ", 11)) {
    uint8_t requested = 0;
    if (!Logic::parseBrightness(line + 11, Config::MAX_BRIGHTNESS, requested)) {
      Serial.println("ERR brightness: use an integer 0..64");
      return;
    }
    ui.brightness = requested;
    preferenceChanged(now);
  } else if (!strncmp(line, "theme ", 6)) {
    bool found = false;
    for (uint8_t i = 0; i < Dashboard::THEME_COUNT; ++i)
      if (!strcmp(line + 6, Dashboard::THEMES[i].id)) {
        ui.theme = i;
        found = true;
      }
    if (!found) {
      Serial.println("ERR theme: mint/blue/amber/violet");
      return;
    }
    preferenceChanged(now);
  } else if (!strncmp(line, "orientation ", 12)) {
    if (!strcmp(line + 12, "landscape"))
      ui.rotation = 3;
    else if (!strcmp(line + 12, "portrait"))
      ui.rotation = 0;
    else {
      Serial.println("ERR orientation: landscape/portrait");
      return;
    }
    preferenceChanged(now);
  } else if (!strncmp(line, "mode ", 5)) {
    const char* names[] = {"env", "graph", "pc", "custom", "ai", "focus", "clock", "diagnostics", "settings", "light"};
    bool found = false;
    for (uint8_t i = 0; i < uint8_t(Dashboard::Page::Count); ++i)
      if (!strcmp(line + 5, names[i])) {
        ui.page = Dashboard::Page(i);
        found = true;
      }
    if (!strcmp(line + 5, "ledtest")) { ui.page = Dashboard::Page::LedTest; found = true; }
    if (!strcmp(line + 5, "lightsetup")) { ui.page=Dashboard::Page::LightSetup; found=true; }
    if (!found) {
      Serial.println("ERR mode; type help");
      return;
    }
    ui.ledTest.stop();
    ui.detail = ui.page == Dashboard::Page::LedTest || ui.page==Dashboard::Page::LightSetup; ui.editing = false;
    pageChanged = true;
  } else if (!strcmp(line, "ai idle"))
    aiState = Ai::Idle;
  else if (!strcmp(line, "ai thinking"))
    aiState = Ai::Thinking;
  else if (!strcmp(line, "ai done"))
    aiState = Ai::Done;
  else if (!strcmp(line, "ai error"))
    aiState = Ai::Error;
  else if (!strcmp(line, "traffic auto"))
    trafficAuto = true;
  else if (!strncmp(line, "traffic ", 8)) {
    Traffic requested;
    if (!strcmp(line + 8, "green"))
      requested = Traffic::Green;
    else if (!strcmp(line + 8, "yellow"))
      requested = Traffic::Yellow;
    else if (!strcmp(line + 8, "red"))
      requested = Traffic::Red;
    else if (!strcmp(line + 8, "off"))
      requested = Traffic::Off;
    else {
      Serial.println("ERR traffic: auto/green/yellow/red/off");
      return;
    }
    trafficAuto = false;
    manualTraffic = requested;
  } else {
    Serial.println("ERR unknown command; type help");
    return;
  }
  Serial.println("OK");
}

void serviceInputs(uint32_t now) {
  int32_t clicks;
  portENTER_CRITICAL(&encoderMux);
  clicks = encoderEdges / Config::ENCODER_EDGES_PER_CLICK;
  encoderEdges %= Config::ENCODER_EDGES_PER_CLICK;
  portEXIT_CRITICAL(&encoderMux);
  if (clicks) {
    clicks = constrain(clicks, -32, 32) * Config::ENCODER_DIRECTION;
    if (Dashboard::rotate(ui, int(clicks))) preferenceChanged(now);
    pageChanged = true;
  }
  const auto push = pushButton.update(digitalRead(Config::ENCODER_PUSH) == LOW, now);
  const auto back = k0Button.update(digitalRead(Config::BUTTON_K0) == LOW, now);
  if (push == Dashboard::Button::Short) Dashboard::push(ui, now);
  if (push == Dashboard::Button::Long) Dashboard::openSettings(ui);
  if (back != Dashboard::Button::None) Dashboard::back(ui, back == Dashboard::Button::Long);
  if (push != Dashboard::Button::None || back != Dashboard::Button::None) pageChanged = true;
  if(ui.lightRequest>=0) { LightbarService::localAction(ui.lightRequest,ui.lightDelta,now); ui.lightRequest=-1; }
  if (Config::ENABLE_TOUCH) {
    if (leftTouch.pressed(digitalRead(Config::TOUCH_LEFT) == HIGH, now)) changePage();
    if (rightTouch.pressed(digitalRead(Config::TOUCH_RIGHT) == HIGH, now)) {
      Dashboard::push(ui, now);
      pageChanged = true;
    }
  }
  // Limit work per loop so a stream of serial input cannot starve the radar.
  for (uint8_t count = 0; count < 64 && Serial.available(); ++count) {
    const auto result = commandLine.push(static_cast<char>(Serial.read()));
    if (result == Logic::LineBuffer::Result::Ready)
      command(commandLine.data(), now);
    else if (result == Logic::LineBuffer::Result::Overflow)
      Serial.println("ERR command too long; discarded");
  }
}

void serviceRadar(uint32_t now) {
  if (!radarActive) return;
  // Same receive-only parser that passed the isolated LD2410C hardware test.
  for (unsigned count = 0; count < 1024 && radarUart.available(); ++count) {
    const int byte = radarUart.read();
    if (byte < 0) break;
    ++radarBytes;
    if (!radar.push(static_cast<uint8_t>(byte))) continue;
    const auto& sample = radar.sample;
    values.radarSeen = true;
    values.radarAt = now;
    values.present = sample.state != 0;
    values.moving = sample.state & 1;
    values.stationary = sample.state & 2;
    values.movingCm = sample.movingCm;
    values.stationaryCm = sample.stationaryCm;
  }
}

bool readLuxChecked(float& lux, bool& saturated) {
  // Adafruit's convenience getter cannot report an I2C read error. Read the
  // same ALS register with explicit transaction checks, at fixed gain 1/8,
  // integration 100 ms: 0.4608 lux/count (Adafruit VEML7700 2.1.6 resolution).
  Wire.beginTransmission(VEML7700_I2CADDR_DEFAULT);
  Wire.write(VEML7700_ALS_DATA);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(static_cast<uint8_t>(VEML7700_I2CADDR_DEFAULT), static_cast<uint8_t>(2)) !=
      2)
    return false;
  const uint16_t low = Wire.read();
  const uint16_t raw = low | (static_cast<uint16_t>(Wire.read()) << 8);
  saturated = raw == UINT16_MAX;
  lux = raw * 0.4608f;
  return !saturated;
}

void serviceSensors(uint32_t now) {
  if (static_cast<uint32_t>(now - lastSensors) < 1000) return;
  lastSensors = now;
  if (scdStarted) {
    bool ready = false;
    int16_t error = scd.getDataReadyStatus(ready);
    if (!error && ready) {
      uint16_t ppm = 0;
      float temperature = 0, humidity = 0;
      error = scd.readMeasurement(ppm, temperature, humidity);
      if (!error && ppm > 0 && isfinite(temperature) && isfinite(humidity) && humidity >= 0 &&
          humidity <= 100) {
        values.co2 = ppm;
        values.temperature = temperature;
        values.humidity = humidity;
        values.co2At = millis();
        values.co2Valid = true;
      } else
        values.co2Valid = false;
    }
    if (error) {
      values.co2Valid = false;
      if (static_cast<uint32_t>(now - lastScdErrorLog) >= 5000) {
        char errorText[128];
        errorToString(error, errorText, sizeof(errorText));
        Serial.printf("SCD40 error: %s\n", errorText);
        lastScdErrorLog = now;
      }
    }
  }
  if (luxStarted) {
    values.luxValid = readLuxChecked(values.lux, values.luxSaturated);
    if (values.luxValid) values.luxAt = millis();
    LightbarService::sample(values.lux,values.luxValid,millis());
  }
}

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) { return Adafruit_NeoPixel::Color(r, g, b); }

void serviceLeds(uint32_t now) {
  if (!ledsActive) return;
  if (static_cast<uint32_t>(now - lastLeds) < 33) return;
  lastLeds = now;
  uint32_t aiColor = 0, trafficColor = 0;
  Ai visibleAi = aiState;
  bool needsAction = false;
  if (ui.agents[0].received || ui.agents[1].received) {
    visibleAi = Ai::Idle;
    bool busy = false, done = false, failed = false;
    for (const auto& agent : ui.agents) {
      const auto state = agent.visibleStatus(now);
      busy |= state == Dashboard::AgentStatus::Busy;
      done |= state == Dashboard::AgentStatus::Complete;
      failed |= state == Dashboard::AgentStatus::Error;
      needsAction |= state == Dashboard::AgentStatus::Waiting;
    }
    visibleAi = failed ? Ai::Error : busy ? Ai::Thinking : done ? Ai::Done : Ai::Idle;
  }
  if (visibleAi == Ai::Thinking) {
    const uint16_t phase = (now % 2000) / 4;
    const uint8_t pulse = 24 + ((phase < 250 ? phase : 500 - phase) * 200 / 250);
    aiColor = rgb(0, pulse / 2, pulse);
  } else if (visibleAi == Ai::Done)
    aiColor = rgb(0, 255, 80);
  else if (visibleAi == Ai::Error)
    aiColor = (now / 400) % 2 ? rgb(255, 0, 0) : 0;
  if (needsAction && visibleAi != Ai::Error) aiColor = rgb(255, 150, 30);

  switch (currentTraffic()) {
    case Traffic::Green:
      trafficColor = rgb(0, 255, 0);
      break;
    case Traffic::Yellow:
      trafficColor = rgb(255, 120, 0);
      break;
    case Traffic::Red:
      trafficColor = rgb(255, 0, 0);
      break;
    case Traffic::Off:
      break;
    case Traffic::Unknown:
      trafficColor = (now / 700) % 2 ? rgb(0, 0, 80) : 0;
      break;
  }
  if (ledTest) {
    const uint32_t elapsed = now - ledTestAt;
    if (elapsed >= 3000)
      ledTest = false;
    else {
      aiColor = elapsed < 1000 ? rgb(255, 0, 0) : elapsed < 2000 ? rgb(0, 255, 0) : rgb(0, 0, 255);
      trafficColor = aiColor;
    }
  }
  ui.ledTest.tick(now);
  if (ui.ledTest.active) {
    const uint32_t colors[] = {rgb(255, 0, 0), rgb(0, 255, 0), rgb(0, 0, 255), rgb(255, 255, 255)};
    const uint32_t elapsed = now - ui.ledTest.startedAt;
    aiPixels.setBrightness(20); trafficPixels.setBrightness(20);
    if (ui.ledTest.runningMode == 5) {
      aiPixels.clear(); trafficPixels.clear();
      const uint16_t index = (elapsed / 450) % (Config::AI_LED_COUNT + Config::TRAFFIC_LED_COUNT);
      if (index < Config::AI_LED_COUNT) aiPixels.setPixelColor(index, colors[3]);
      else trafficPixels.setPixelColor(index - Config::AI_LED_COUNT, colors[3]);
    } else {
      const uint8_t color = ui.ledTest.runningMode == 0 ? (elapsed / 1000) % 4 : ui.ledTest.runningMode - 1;
      aiPixels.fill(colors[color]); trafficPixels.fill(colors[color]);
    }
    aiPixels.show(); trafficPixels.show();
    return;
  }
  // Refill after brightness changes to avoid NeoPixel's lossy rescaling.
  aiPixels.setBrightness(brightness);
  trafficPixels.setBrightness(brightness);
  aiPixels.fill(aiColor);
  trafficPixels.fill(trafficColor);
  aiPixels.show();
  trafficPixels.show();
}

void serviceLink(uint32_t now) {
  char packet[96];
  for (int count = 0; count < 4 && DashboardLink::receive(packet, sizeof(packet)); ++count) {
    if(!strncmp(packet,"lamp ",5)) LightbarService::command(packet,now,1);
    else Dashboard::telemetry(packet, ui, now);
  }
  DashboardLink::tick(now);
  if(DashboardLink::canSend()) { char report[240]; if(LightbarService::report(report,sizeof(report),now)) DashboardLink::send(report); }
  ui.bleConnected = DashboardLink::connected();
}

void updateDashboard(uint32_t now) {
  ui.now = now;
  ui.light=LightbarService::view();
  ui.focus.tick(now);
  ui.sensors.value[0] = values.temperature;
  ui.sensors.value[1] = values.humidity;
  ui.sensors.value[2] = values.co2;
  ui.sensors.value[3] = values.lux;
  for (int m = 0; m < 3; ++m) ui.sensors.valid[m] = co2Fresh(now);
  ui.sensors.valid[3] = luxFresh(now);
  ui.sensors.radarValid = radarFresh(now);
  ui.sensors.present = values.present;
  ui.sensors.moving = values.moving;
  ui.sensors.radio = radioConnected;
  ui.sensors.scd = scdStarted;
  ui.sensors.light = luxStarted;
  ui.sensors.saturated = values.luxSaturated;
  ui.co2Level = autoTraffic == Traffic::Red ? 2 : autoTraffic == Traffic::Yellow ? 1 : 0;
  ui.freeHeap = ESP.getFreeHeap();
  history.tick(ui.sensors, now);
}

void serviceDisplay(uint32_t now) {
  if (!ui.displayReady) return;
  if (lcdTestUntil && int32_t(lcdTestUntil - now) > 0) return;
  lcdTestUntil = 0;
  // Finish a frame before rendering another one. Transfer at most 8 rows per
  // loop (~2.6 ms at 16 MHz), leaving time to drain the 256 kbaud radar UART.
  if (!flushing) {
    if (!pageChanged && uint32_t(now - lastDisplay) < 500) return;
    lastDisplay = now;
    pageChanged = false;
    if (activeRotation != ui.rotation) {
      display.setRotation(ui.rotation);
      activeRotation = ui.rotation;
      fullRefresh = true;
    }
    Dashboard::Canvas canvas{frame, display.width(), display.height()};
    Dashboard::render(canvas, ui, history);
    ++renderedFrames;
    flushRow = 0;
    flushing = true;
  }
  const int width = display.width();
  const int rows = std::min(8, int(display.height()) - flushRow);
  const size_t offset = size_t(flushRow) * width;
  const size_t bytes = size_t(rows) * width * sizeof(uint16_t);
  if (fullRefresh || memcmp(frame + offset, shownFrame + offset, bytes)) {
    memcpy(transferStrip, frame + offset, bytes);
    display.drawRGBBitmap(0, flushRow, transferStrip, width, rows);
    ++displayStrips;
    memcpy(shownFrame + offset, frame + offset, bytes);
  }
  flushRow += rows;
  if (flushRow >= display.height()) {
    flushing = false;
    fullRefresh = false;
  }
}
}  // namespace

void spaceSmartBegin() {
  Serial.begin(115200);  // Never wait for Serial: standalone power must work.
  delay(150);
  loadPreferences();
  ui.bleEnabled = bleActive;

  // Deselect both SPI peripherals before either controller starts.
  pinMode(Config::LCD_CS, OUTPUT);
  digitalWrite(Config::LCD_CS, HIGH);
  pinMode(Config::RADIO_CSN, OUTPUT);
  digitalWrite(Config::RADIO_CSN, HIGH);
  pinMode(Config::RADIO_CE, OUTPUT);
  digitalWrite(Config::RADIO_CE, LOW);
  lcdSpi.begin(Config::LCD_SCK, -1, Config::LCD_MOSI, Config::LCD_CS);
  display.init(Config::LCD_WIDTH, Config::LCD_HEIGHT);
  display.setSPISpeed(Config::LCD_SPI_HZ);
  display.setRotation(ui.rotation);
  display.invertDisplay(Config::LCD_INVERT);
  display.setTextWrap(false);
  constexpr size_t bufferBytes = size_t(Config::LCD_WIDTH) * Config::LCD_HEIGHT * 2;
  // This board has 8 MB OPI PSRAM. Preserve internal RAM for BLE and sensors.
  frame =
      static_cast<uint16_t*>(heap_caps_malloc(bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  shownFrame =
      static_cast<uint16_t*>(heap_caps_malloc(bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  ui.displayReady = frame && shownFrame;
  if (!ui.displayReady) {
    display.fillScreen(ST77XX_BLACK);
    display.setCursor(12, 20);
    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(2);
    display.println("Enable OPI PSRAM");
    Serial.println("ERROR dashboard needs OPI PSRAM; sensors remain active");
  }
  serviceDisplay(millis());

  pinMode(Config::ENCODER_A, INPUT_PULLUP);
  pinMode(Config::ENCODER_B, INPUT_PULLUP);
  pinMode(Config::ENCODER_PUSH, INPUT_PULLUP);
  pinMode(Config::BUTTON_K0, INPUT_PULLUP);
  encoderPrevious = (digitalRead(Config::ENCODER_A) << 1) | digitalRead(Config::ENCODER_B);
  attachInterrupt(digitalPinToInterrupt(Config::ENCODER_A), encoderInterrupt, CHANGE);
  attachInterrupt(digitalPinToInterrupt(Config::ENCODER_B), encoderInterrupt, CHANGE);
  pushButton.begin(digitalRead(Config::ENCODER_PUSH) == LOW, millis());
  k0Button.begin(digitalRead(Config::BUTTON_K0) == LOW, millis());
  if (Config::ENABLE_TOUCH) {
    pinMode(Config::TOUCH_LEFT, INPUT_PULLDOWN);
    pinMode(Config::TOUCH_RIGHT, INPUT_PULLDOWN);
    leftTouch.begin(digitalRead(Config::TOUCH_LEFT) == HIGH, millis());
    rightTouch.begin(digitalRead(Config::TOUCH_RIGHT) == HIGH, millis());
  }

#if !defined(SPACESMART_BRINGUP)
  bootScheduled = true;
  bootStageAt = millis();
#else
  Serial.println("BRINGUP: LCD/input only. hardware leds|sensors|radio|ble|radar|all");
#endif
  printHelp();
  lastSensors = millis();
  pageChanged = true;
}

void spaceSmartLoop() {
  uint32_t now = millis();
  serviceBoot(now);
  now = millis();
  serviceRadar(now);
  serviceInputs(now);
  serviceLink(now);
  serviceSensors(now);
  // Refresh time after I2C calls before evaluating timestamps taken inside them.
  now = millis();
  serviceRadar(now);
  autoTraffic = Logic::co2Traffic(values.co2, co2Fresh(now), autoTraffic, Config::CO2_YELLOW_PPM,
                                  Config::CO2_RED_PPM, Config::CO2_HYSTERESIS_PPM);
  LightbarService::tick(now,values.lux,luxFresh(now),radarFresh(now)&&values.present);
  updateDashboard(now);
  serviceLeds(now);
  serviceDisplay(now);
  servicePreferences(now);
  if (radioActive && static_cast<uint32_t>(now - lastRadioCheck) >= 10000) {
    lastRadioCheck = now;
    radioConnected = radio.isChipConnected();
  }
  if (static_cast<uint32_t>(now - lastReport) >= 5000) {
    lastReport = now;
    printStatus(now);
  }
  delay(1);
}
