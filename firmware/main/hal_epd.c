// SSD1681-style 200x200 e-paper (Waveshare 1.54" V2). Register sequence and waveform tables are taken from Waveshare's
// ESP-IDF V2 example (epaper_driver_bsp) and re-written in C. The panel is powered only during a refresh, so the
// controller loses its RAM between refreshes: a partial refresh first re-loads the previous picture as the base.
#include <string.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "hal.h"
#include "display.h"
#include "pins.h"

#define EPD_PARTIAL_OK 1      // set to 0 to always do a full refresh (slower, flashes, but the most conservative)
#define W 200
#define H 200
#define BYTES (W * H / 8)
static const char *TAG = "epd";
static spi_device_handle_t spi;
static bool bus_ready;
static uint8_t inv[BYTES];

static const uint8_t LUT_FULL[159] = {
    0x80, 0x48, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x48, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x48, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x48, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x01, 0x00, 0x08, 0x01,
    0x00, 0x02, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00, 0x00, 0x22, 0x17, 0x41,
    0x00, 0x32, 0x20 };
static const uint8_t LUT_PART[159] = {
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00, 0x00, 0x02, 0x17, 0x41,
    0xB0, 0x32, 0x28 };

static void tx(const void *p, int n) {
    spi_transaction_t t = { .length = (size_t)n * 8, .tx_buffer = p };
    spi_device_polling_transmit(spi, &t);
}
static void cmd(uint8_t c)  { gpio_set_level(PIN_EPD_DC, 0); gpio_set_level(PIN_EPD_CS, 0); tx(&c, 1); gpio_set_level(PIN_EPD_CS, 1); }
static void dat(uint8_t d)  { gpio_set_level(PIN_EPD_DC, 1); gpio_set_level(PIN_EPD_CS, 0); tx(&d, 1); gpio_set_level(PIN_EPD_CS, 1); }
static void datn(const uint8_t *p, int n) { gpio_set_level(PIN_EPD_DC, 1); gpio_set_level(PIN_EPD_CS, 0); tx(p, n); gpio_set_level(PIN_EPD_CS, 1); }
static void busy(void) {                       // HIGH = busy. Tick rate is 100 Hz, so poll with real 10 ms ticks and measure real time.
    int lvl = gpio_get_level(PIN_EPD_BUSY);
    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(PIN_EPD_BUSY) == 1 && esp_timer_get_time() - t0 < 6000000) vTaskDelay(1);
    ESP_LOGI(TAG, "busy: level on entry %d, waited %d ms", lvl, (int)((esp_timer_get_time() - t0) / 1000));
}

// Waveshare's own V2 examples switch BOTH the panel rail (GPIO6) and the audio/peripheral rail (GPIO42) on before touching the panel.
static bool rail_on, panel_ready;       // rail_on: panel rail is powered; panel_ready: panel is initialised for quick partial updates

static void deep_sleep_panel(void);
void hal_epd_power(bool on) {
    if (on == rail_on) return;                   // a wake that redraws several times keeps the panel powered: later updates skip the whole start-up
    if (!on && panel_ready) deep_sleep_panel();
    gpio_config_t o = { .pin_bit_mask = (1ULL << PIN_EPD_PWR) | (1ULL << PIN_AUDIO_PWR), .mode = GPIO_MODE_OUTPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&o);
    gpio_set_level(PIN_EPD_PWR, on ? 0 : 1);
    gpio_set_level(PIN_AUDIO_PWR, on ? 0 : 1);
    rail_on = on;
    if (on) vTaskDelay(pdMS_TO_TICKS(150));
    else panel_ready = false;
}

static void bus_init(void) {
    if (bus_ready) return;
    gpio_config_t o = { .pin_bit_mask = (1ULL << PIN_EPD_DC) | (1ULL << PIN_EPD_CS) | (1ULL << PIN_EPD_RST), .mode = GPIO_MODE_OUTPUT };
    gpio_config(&o);
    gpio_config_t i = { .pin_bit_mask = 1ULL << PIN_EPD_BUSY, .mode = GPIO_MODE_INPUT };
    gpio_config(&i);
    gpio_set_level(PIN_EPD_CS, 1);
    spi_bus_config_t b = { .mosi_io_num = PIN_EPD_MOSI, .miso_io_num = -1, .sclk_io_num = PIN_EPD_SCK,
                           .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = BYTES + 8 };
    spi_bus_initialize(SPI2_HOST, &b, SPI_DMA_CH_AUTO);
    spi_device_interface_config_t d = { .clock_speed_hz = 4 * 1000 * 1000, .mode = 0, .spics_io_num = -1, .queue_size = 1 };
    spi_bus_add_device(SPI2_HOST, &d, &spi);
    bus_ready = true;
}

static void hw_reset(void) {
    gpio_set_level(PIN_EPD_RST, 1); vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_EPD_RST, 0); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_EPD_RST, 1); vTaskDelay(pdMS_TO_TICKS(50));
    busy();
}
static void set_lut(const uint8_t *l) {
    cmd(0x32); datn(l, 153); busy();
    cmd(0x3F); dat(l[153]);
    cmd(0x03); dat(l[154]);
    cmd(0x04); dat(l[155]); dat(l[156]); dat(l[157]);
    cmd(0x2C); dat(l[158]);
}
static void cursor(void) { cmd(0x4E); dat(0); cmd(0x4F); dat((H - 1) & 0xFF); dat(((H - 1) >> 8) & 0xFF); }
static void window(void) {
    cmd(0x44); dat(0); dat((W - 1) >> 3);
    cmd(0x45); dat((H - 1) & 0xFF); dat(((H - 1) >> 8) & 0xFF); dat(0); dat(0);
}
static void common_init(const uint8_t *lut) {
    hw_reset();
    cmd(0x12); busy();                           // software reset
    cmd(0x01); dat(0xC7); dat(0x00); dat(0x01);  // driver output control
    cmd(0x11); dat(0x01);                        // data entry mode
    window();
    cmd(0x3C); dat(0x01);                        // border waveform
    cmd(0x18); dat(0x80);                        // internal temperature sensor
    cmd(0x22); dat(0xB1); cmd(0x20);             // load temperature + waveform
    cursor();
    busy();
    set_lut(lut);
}
static void write_ram(uint8_t reg, const uint8_t *fb) {   // fb: pixel 1 = black; panel RAM: 1 = white
    for (int i = 0; i < BYTES; i++) inv[i] = (uint8_t)~fb[i];
    cursor();
    cmd(reg);
    datn(inv, BYTES);
}
static void deep_sleep_panel(void) { cmd(0x10); dat(0x01); panel_ready = false; }

void hal_epd_flush(const uint8_t *fb, const uint8_t *prev, int stride, Rect win, bool full) {
    (void)stride; (void)win;                     // the whole 5 KB goes over SPI in ~5 ms; the waveform decides what visibly changes
    bus_init();
    if (full || !prev || !EPD_PARTIAL_OK) {
        common_init(LUT_FULL);
        write_ram(0x24, fb);
        write_ram(0x26, fb);
        cmd(0x22); dat(0xC7); cmd(0x20); busy();
        deep_sleep_panel();
    } else if (panel_ready) {                    // fast path: panel already awake with the old picture in its RAM
        write_ram(0x24, fb);
        cmd(0x22); dat(0xCF); cmd(0x20); busy();
    } else {
        common_init(LUT_PART);
        write_ram(0x24, prev);                   // controller RAM was lost with the power: re-load the picture that is on the glass
        write_ram(0x26, prev);
        cmd(0x37); dat(0); dat(0); dat(0); dat(0); dat(0); dat(0x40); dat(0); dat(0); dat(0); dat(0);
        cmd(0x3C); dat(0x80);
        cmd(0x22); dat(0xC0); cmd(0x20); busy();
        write_ram(0x24, fb);
        cmd(0x22); dat(0xCF); cmd(0x20); busy();
        panel_ready = true;
    }
    ESP_LOGI(TAG, "refresh %s", (full || !prev || !EPD_PARTIAL_OK) ? "full" : "partial");
}
