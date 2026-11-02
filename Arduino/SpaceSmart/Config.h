#pragma once
#include <stdint.h>

// 기존 배선표 그대로 사용합니다. SCL/SDA라고 적힌 LCD 핀은 SPI입니다.
namespace Config {
constexpr uint8_t I2C_SDA = 4;
constexpr uint8_t I2C_SCL = 5;
constexpr uint8_t LCD_SCK = 12;
constexpr uint8_t LCD_MOSI = 11;
constexpr uint8_t LCD_RST = 8;
constexpr uint8_t LCD_DC = 9;
constexpr uint8_t LCD_CS = 10;
constexpr uint8_t ENCODER_A = 21;
constexpr uint8_t ENCODER_B = 40;
constexpr uint8_t ENCODER_PUSH = 41;
constexpr uint8_t BUTTON_K0 = 47;
constexpr uint8_t RADIO_CE = 17;
constexpr uint8_t RADIO_CSN = 16;
constexpr uint8_t RADIO_SCK = 14;
constexpr uint8_t RADIO_MOSI = 13;
constexpr uint8_t RADIO_MISO = 15;
constexpr uint8_t RADAR_RX = 7;   // LD2410C TX -> GPIO7
constexpr uint8_t RADAR_TX = 18;  // GPIO18 -> LD2410C RX
constexpr uint32_t RADAR_BAUD = 256000; // LD2410C 기본 통신 속도
constexpr uint8_t AI_LED_PIN = 6;       // 시프터 DAT -> D5 -> AI LED DIN
constexpr uint8_t TRAFFIC_LED_PIN = 42; // 시프터 CLK -> C5 -> 신호등 DIN

// 2026-10-26: LCD ST7789 240x320, 신호등 1개 사용자 확인.
constexpr uint16_t LCD_WIDTH = 240;
constexpr uint16_t LCD_HEIGHT = 320;
constexpr uint8_t LCD_ROTATION = 3; // 2026-10-30 실물 방향에 맞춰 180도 회전, 가로 320x240
constexpr bool LCD_INVERT = false; // 2026-10-30 흰색이 검정으로 보이는 색상 반전 해제
constexpr uint32_t LCD_SPI_HZ = 16000000;
constexpr uint16_t AI_LED_COUNT = 8; // 최신 케이스의 134mm 스트립
constexpr uint16_t TRAFFIC_LED_COUNT = 1;
constexpr uint8_t INITIAL_BRIGHTNESS = 20; // 0~64, 부팅 시 낮은 밝기
constexpr uint8_t MAX_BRIGHTNESS = 64;
constexpr uint8_t BRIGHTNESS_STEP = 2;
constexpr int8_t ENCODER_DIRECTION = 1; // 방향이 반대면 -1
constexpr uint8_t ENCODER_EDGES_PER_CLICK = 4; // 두 칸마다 반응하면 2

// 기본 신호등 동작: CO2에 따라 초록/노랑/빨강. 사용자 지정 UI 기준값.
constexpr uint16_t CO2_YELLOW_PPM = 1000;
constexpr uint16_t CO2_RED_PPM = 1500;
constexpr uint16_t CO2_HYSTERESIS_PPM = 50;
constexpr uint32_t CO2_STALE_MS = 15000;
constexpr uint32_t LUX_STALE_MS = 3000;
constexpr uint32_t RADAR_STALE_MS = 2000;

// 실물 미연결 선택 부품은 기본 비활성화. BLK / GPIO39는 구동하지 않습니다.
constexpr bool ENABLE_TOUCH = false;
constexpr uint8_t TOUCH_LEFT = 1;
constexpr uint8_t TOUCH_RIGHT = 2;
} // namespace Config

static_assert(Config::AI_LED_COUNT > 0 && Config::TRAFFIC_LED_COUNT > 0,
              "At least one pixel is required per LED output");
static_assert(Config::INITIAL_BRIGHTNESS <= Config::MAX_BRIGHTNESS,
              "Initial brightness exceeds limit");
static_assert(Config::CO2_HYSTERESIS_PPM < Config::CO2_YELLOW_PPM &&
              Config::CO2_YELLOW_PPM < Config::CO2_RED_PPM,
              "Invalid CO2 thresholds");
static_assert(Config::ENCODER_DIRECTION == 1 || Config::ENCODER_DIRECTION == -1,
              "Encoder direction must be 1 or -1");
static_assert(Config::ENCODER_EDGES_PER_CLICK > 0, "Invalid encoder resolution");
