"""Verify required firmware pins against the existing RevK wiring artifact."""
import csv
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
config = (root / "SpaceSmart/Config.h").read_text()
pins = {name: int(value) for name, value in re.findall(
    r"constexpr uint8_t (\w+) = (\d+);", config)}
rows = list(csv.DictReader((root / "wiring/signal-connections.csv")
                          .open(encoding="utf-8-sig")))
expected = {
    "I2C_SDA": "SDA", "I2C_SCL": "SCL",
    "LCD_SCK": "SCL / SPI CLK", "LCD_MOSI": "SDA / SPI MOSI",
    "LCD_RST": "RES", "LCD_DC": "DC", "LCD_CS": "CS",
    "ENCODER_A": "A", "ENCODER_B": "B", "ENCODER_PUSH": "PUSH", "BUTTON_K0": "K0",
    "RADIO_CE": "CE", "RADIO_CSN": "CSN / CS", "RADIO_SCK": "SCK",
    "RADIO_MOSI": "MOSI", "RADIO_MISO": "MISO",
    "RADAR_RX": "TX → ESP RX", "RADAR_TX": "RX ← ESP TX",
    "AI_LED_PIN": "DAT → D5", "TRAFFIC_LED_PIN": "CLK → C5",
}
assert len(expected) == 20
assert len({pins[key] for key in expected}) == 20, "GPIO collision"
for key, label in expected.items():
    matches = [row for row in rows if row["pin"] == label]
    assert matches, (key, "missing from wiring CSV")
    assert all(pins[key] == int(row["GPIO"]) for row in matches), (key, matches)
assert not {pins[key] for key in expected} & {0, 3, 19, 20, 35, 36, 37, 39, 43, 44, 45, 46, 48}
assert "AI_LED_COUNT = 8;" in config
assert "TRAFFIC_LED_COUNT = 1;" in config
assert "LCD_WIDTH = 240;" in config and "LCD_HEIGHT = 320;" in config
print("PASS: all 20 required GPIOs match RevK CSV; no collisions/reserved pins; LCD 240x320, AI 8 / traffic 1")
