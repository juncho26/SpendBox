#include <string.h>
#include <stdlib.h>
#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "hal.h"
#include "pins.h"

static const char *TAG = "misc";
#define RTC_ADDR 0x51

static uint8_t bcd(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 15)); }
static uint8_t tobcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static bool rtc_open(i2c_master_bus_handle_t *bus, i2c_master_dev_handle_t *dev) {
    i2c_master_bus_config_t bc = { .i2c_port = I2C_NUM_0, .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
                                   .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true };
    if (i2c_new_master_bus(&bc, bus) != ESP_OK) return false;
    i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = RTC_ADDR, .scl_speed_hz = 100000 };
    if (i2c_master_bus_add_device(*bus, &dc, dev) != ESP_OK) { i2c_del_master_bus(*bus); return false; }
    return true;
}
static void rtc_close(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t dev) { i2c_master_bus_rm_device(dev); i2c_del_master_bus(bus); }

bool hal_rtc_set(int y, int mo, int d, int h, int mi, int s) {
    i2c_master_bus_handle_t bus; i2c_master_dev_handle_t dev;
    if (!rtc_open(&bus, &dev)) return false;
    Date dt = { (int16_t)y, (uint8_t)mo, (uint8_t)d };
    uint8_t buf[8] = { 0x04, tobcd(s), tobcd(mi), tobcd(h), tobcd(d), (uint8_t)date_weekday(dt), tobcd(mo), tobcd(y % 100) };
    bool ok = i2c_master_transmit(dev, buf, sizeof buf, 100) == ESP_OK;
    rtc_close(bus, dev);
    return ok;
}

static bool build_time(int *y, int *mo, int *d, int *h, int *mi, int *s) {
    static const char *m = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = { __DATE__[0], __DATE__[1], __DATE__[2], 0 };
    const char *p = strstr(m, mon);
    if (!p) return false;
    *mo = (int)(p - m) / 3 + 1; *d = atoi(__DATE__ + 4); *y = atoi(__DATE__ + 7);
    *h = atoi(__TIME__); *mi = atoi(__TIME__ + 3); *s = atoi(__TIME__ + 6);
    return true;
}

Date hal_rtc_today(void) {
    Date out = { 2026, 1, 1 };
    i2c_master_bus_handle_t bus; i2c_master_dev_handle_t dev;
    if (!rtc_open(&bus, &dev)) return out;
    uint8_t reg = 0x04, r[7] = { 0 };
    bool ok = i2c_master_transmit_receive(dev, &reg, 1, r, 7, 100) == ESP_OK;
    rtc_close(bus, dev);
    if (!ok) { ESP_LOGW(TAG, "RTC not answering"); return out; }
    bool lost = (r[0] & 0x80) != 0;                       // oscillator-stop flag: the clock never ran or lost power
    int y = 2000 + bcd(r[6]), mo = bcd(r[5] & 0x1F), d = bcd(r[3] & 0x3F);
    if (lost || mo < 1 || mo > 12 || d < 1 || d > 31 || y < 2026) {
        int by, bmo, bd, bh, bmi, bs;
        if (build_time(&by, &bmo, &bd, &bh, &bmi, &bs)) {
            ESP_LOGW(TAG, "RTC invalid, setting it to the build time %d-%02d-%02d", by, bmo, bd);
            hal_clock_lost_set(true);                     // remembered, so the screen can ask for the clock to be set from the phone
            hal_rtc_set(by, bmo, bd, bh, bmi, bs);
            out = (Date){ (int16_t)by, (uint8_t)bmo, (uint8_t)bd };
        }
        return out;
    }
    return (Date){ (int16_t)y, (uint8_t)mo, (uint8_t)d };
}

static int last_mv;
int hal_battery_mv(void) { return last_mv; }
int hal_battery_pct(void) {
    adc_oneshot_unit_handle_t u; adc_cali_handle_t cal = NULL;
    adc_oneshot_unit_init_cfg_t ic = { .unit_id = ADC_UNIT_1 };
    if (adc_oneshot_new_unit(&ic, &u) != ESP_OK) return 100;
    adc_oneshot_chan_cfg_t cc = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    adc_oneshot_config_channel(u, ADC_CHANNEL_3, &cc);   // GPIO4, battery through a 1:2 divider
    adc_cali_curve_fitting_config_t cf = { .unit_id = ADC_UNIT_1, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    adc_cali_create_scheme_curve_fitting(&cf, &cal);
    int raw = 0, sum = 0, mv = 0;
    for (int i = 0; i < 8; i++) { adc_oneshot_read(u, ADC_CHANNEL_3, &raw); sum += raw; }
    raw = sum / 8;
    if (cal) adc_cali_raw_to_voltage(cal, raw, &mv); else mv = raw * 3100 / 4095;
    if (cal) adc_cali_delete_scheme_curve_fitting(cal);
    adc_oneshot_del_unit(u);
    int vbat = mv * 2;                                    // millivolts
    last_mv = vbat < 2500 ? 0 : vbat;
    ESP_LOGI(TAG, "battery %d mV", vbat);
    if (vbat < 2500) return 100;                          // no battery fitted: running from USB
    int pct = (vbat - 3300) * 100 / (4150 - 3300);
    return pct < 0 ? 0 : pct > 100 ? 100 : pct;
}
