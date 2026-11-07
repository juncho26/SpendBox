// Verified against Waveshare's own ESP-IDF V2 example (02_Example/ESP-IDF/V2/*/main/user_config.h).
#pragma once
#define PIN_BTN_UP     0    // BOOT button  (software-readable, deep-sleep wake works). Strapping pin only matters at chip reset.
#define PIN_BTN_DOWN  18    // PWR button   (software-readable too: Waveshare's examples read it with gpio_get_level)
#define PIN_VBAT_PWR  17    // drive HIGH to keep battery power on; hold it through deep sleep or the board powers off
#define PIN_EPD_PWR    6    // active LOW
#define PIN_AUDIO_PWR 42    // active LOW (codec + mic)
#define PIN_EPD_DC    10
#define PIN_EPD_CS    11
#define PIN_EPD_SCK   12
#define PIN_EPD_MOSI  13
#define PIN_EPD_RST    9
#define PIN_EPD_BUSY   8
#define PIN_I2C_SDA   47
#define PIN_I2C_SCL   48
#define PIN_RTC_INT    5    // PCF85063 alarm; not used for waking (we only wake on buttons)
