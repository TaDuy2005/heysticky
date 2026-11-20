// HeySticky pin map (reTerminal Sticky, ESP32-S3). Source: PLAN.md §1.
#pragma once

// Buttons
constexpr int PIN_BTN_TOP = 4;   // OK / AI
constexpr int PIN_BTN_UP = 5;
constexpr int PIN_BTN_DOWN = 6;

constexpr int PIN_BUZZER = 48;

// PDM microphone (PLAN_AI.md §2): clock, data, power enable (HIGH = on)
constexpr int PIN_MIC_CLK = 19, PIN_MIC_DATA = 20, PIN_MIC_PWR = 38;

// Touch (GT911) on Wire
constexpr int PIN_TP_SCL = 2, PIN_TP_SDA = 3, PIN_TP_INT = 21, PIN_TP_RST = 41, PIN_TP_EN = 42;

// Sensors (SHT40 0x44, BQ27220 0x55) on Wire1
constexpr int PIN_SENS_SCL = 0, PIN_SENS_SDA = 1;

// Power
constexpr int PIN_POWER_HOLD = 45;
constexpr int PIN_CHG_EN = 39;       // BQ25616 EN_BAT_CHGn, active low: LOW = charging allowed (Seeed pin_config.h)
constexpr int PIN_USB_PRESENT = 9;   // external power, HIGH when present

// microSD (shares the panel SPI; driven by Seeed_GFX2)
constexpr int PIN_SD_CS = 8, PIN_SD_EN = 10, PIN_SD_DETECT = 11;
