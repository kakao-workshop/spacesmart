# Arduino 펌웨어 v2.3

[전체 안내로 돌아가기](../README.md)

검증 기준은 **ESP32-S3 N16R8(Flash 16 MB / OPI PSRAM 8 MB)**와 이 저장소의 배선입니다. 메인 스케치는 [SpaceSmart/SpaceSmart.ino](SpaceSmart/SpaceSmart.ino)입니다. 다른 보드에서는 [Config.h](SpaceSmart/Config.h), 핀 배치와 전원 조건을 먼저 대조하세요.

## 부품과 배선

| 부품 | 용도 |
|---|---|
| ESP32-S3 N16R8 | 메인 컨트롤러 |
| ST7789 240×320 LCD + 노브·버튼 | 가로 320×240 화면, 회전·PUSH·K0 조작 |
| SCD40 | 온도·습도·CO₂ |
| VEML7700 | 조도 |
| LD2410C | 재실 감지, UART 256000 baud |
| 주소 지정 LED | 본체 8개 + 신호등 1개 |
| nRF24L01 + YL-105 | MJGJD01YL 조명용 2.4 GHz 무선 |

[신호 배선 CSV](wiring/signal-connections.csv) · [전원 배선 CSV](wiring/power-connections.csv) · [배선도](wiring/revk-power-direct-wiring.png)

| 연결 | ESP32 GPIO |
|---|---|
| I²C SDA / SCL | 4 / 5, SCD40·VEML7700 공유 |
| LCD SCK / MOSI / CS / DC / RST | 12 / 11 / 10 / 9 / 8 |
| 노브 A / B / PUSH, K0 | 21 / 40 / 41, 47 |
| nRF24 CE / CSN / SCK / MOSI / MISO | 17 / 16 / 14 / 13 / 15 |
| LD2410C TX → ESP RX / RX ← ESP TX | 7 / 18 |
| 본체 LED / 신호등 LED | 6 / 42, 레벨 시프터 경유 |

LCD에 SCL/SDA라고 표기되어 있어도 이 배선의 LCD는 **SPI**입니다. 센서 I²C와 구분하세요. GND는 공통입니다. YL-105의 레귤레이터, LCD 전압, 시프터 종류와 공급 전류는 사용하는 실물 모듈을 기준으로 확인합니다. GPIO39/BLK와 선택 터치 입력은 기본 구동하지 않습니다.

## 펌웨어 설치

1. [Arduino IDE](https://www.arduino.cc/en/software)를 설치합니다.
2. 환경설정의 추가 보드 매니저 URL에 다음 주소를 추가합니다.

   ```text
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

3. 보드 매니저에서 **esp32 by Espressif Systems 3.3.7**을 설치합니다.
4. 라이브러리 매니저에서 아래 버전을 설치합니다. 의존성 설치 요청도 함께 적용합니다.

   | 라이브러리 | 검증 버전 |
   |---|---|
   | Adafruit GFX Library | 1.12.6 |
   | Adafruit ST7735 and ST7789 Library | 1.11.0 |
   | Adafruit NeoPixel | 1.15.5 |
   | Adafruit VEML7700 Library | 2.1.6 |
   | Adafruit BusIO | 1.17.4 |
   | Sensirion I2C SCD4x | 1.1.0 |
   | Sensirion Core | 0.7.3 |
   | RF24 by TMRh20 | 1.6.2 |

5. `Arduino/SpaceSmart/SpaceSmart.ino`를 엽니다. 같은 폴더의 `.h`·`.cpp` 파일을 모두 유지합니다.
6. 다음 보드 설정을 적용합니다.

   | 항목 | 값 |
   |---|---|
   | Board | ESP32S3 Dev Module |
   | Flash Size | 16 MB |
   | Flash Mode / Frequency | QIO / 80 MHz |
   | Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) |
   | PSRAM | OPI PSRAM |
   | USB Mode | Hardware CDC and JTAG |
   | USB CDC On Boot | Enabled |
   | Upload Mode | UART0 / Hardware CDC |
   | Upload Speed | 921600, 불안정하면 115200 |

7. **ESP32 개발보드의 USB 포트**를 데이터 케이블로 연결하고, 업로드 대상 포트를 직접 확인한 뒤 업로드합니다. 케이스의 전원 분배용 USB-C 단자는 업로드 포트가 아닙니다.
8. 부팅 후 화면이 먼저 나오고 LED·센서·레이더·nRF24·Bluetooth가 순서대로 켜집니다. 약 10초 기다린 뒤 Mac에서 연결합니다. SCD40 첫 측정값은 추가 대기가 필요할 수 있습니다.

CLI가 설치되어 있다면 저장소 루트에서 다음 명령으로 **컴파일만** 할 수 있습니다. 자동 업로드는 하지 않습니다.

```sh
arduino-cli compile \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default' \
  --build-path build/arduino Arduino/SpaceSmart
```

## 디바이스 사용법

| 조작 | 동작 |
|---|---|
| 노브 회전 | 화면 또는 선택 항목 이동 |
| PUSH 짧게 | 선택·실행·편집 확정 |
| PUSH 0.8초 | 설정 열기 |
| K0 짧게 / 길게 | 뒤로 / 홈 |
| 그래프 화면 | 노브로 센서 선택, PUSH로 30분·6시간·24시간 변경 |
| 설정 → 테마 | 민트·블루·앰버·바이올렛 변경 |
| 설정 → LED 시험 | 패턴 선택 후 PUSH, 15초 뒤 복원, K0로 즉시 종료 |

기본 화면 순서는 **실내 환경 → 환경 그래프 → 컴퓨터 → 커스텀 → AI 작업 → 집중 타이머 → 시계 → 기기 진단 → 설정 → 조명**입니다. 집중 타이머 선택 중 K0를 길게 누르면 초기화합니다. 테마·방향·LED 밝기·집중 시간은 마지막 변경 약 2초 뒤 저장합니다. 그래프 이력과 실행 중 타이머는 재부팅 시 초기화됩니다.

시리얼 모니터는 **115200 baud / Newline**입니다. `status`로 진단합니다. 화면이 안 나오면 배선·보드 설정·PSRAM부터 확인하세요. 검증된 LCD 우선 초기화 순서는 유지하는 편이 좋습니다.

## Mac·AI·조명

- [macOS 설치·사용법](../macos/README.md): PC 상태와 시간은 AI 연동 없이 사용할 수 있습니다.
- [커스텀 BLE 형식](docs/CUSTOM_PROTOCOL.txt): 그래프형은 제목·사용률, 텍스트형은 제목·좌·우 텍스트를 전달합니다.
- [MJGJD01YL 설치·학습 안내](docs/LIGHTBAR_KO.txt): 원래 리모컨 등록, 전원·밝기·색온도, 위치별 조도 기록.
- [조명 BLE 프로토콜](docs/LIGHTBAR_PROTOCOL.txt): 상태·명령·학습 이벤트 형식.

nRF24 조명 기능은 프로토콜과 로직 검사를 마쳤지만 실제 조명 RF와 현장 학습은 검증 전입니다. MJGJD02YL/1S는 지원하지 않습니다. 자동 조절은 충분한 수동 기록과 실물 확인 후 Mac에서 직접 켭니다.

## 개발 및 테스트

저장소 루트에서 Python 3과 C++17 컴파일러로 실행합니다. 보드가 필요 없으며 업로드하지 않습니다.

```sh
python3 Arduino/tests/check_wiring.py
sh Arduino/tests/run.sh
```

[글꼴 생성기](tools/generate_dashboard_fonts.py)는 Python Pillow와 번들 Nanum Gothic을 사용합니다. 생성된 `DashboardFonts.cpp`가 이미 포함되어 있어 일반 Arduino 빌드에서는 재생성이 필요 없습니다.

`SpaceSmartRadarTest/`는 LD2410C 개별 진단용입니다. 통합 대시보드가 필요하면 메인 `SpaceSmart/` 스케치를 사용하세요.
