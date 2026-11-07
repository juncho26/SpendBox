// ESP-IDF 5.4 (V2 board). Compiles; the hardware layer (hal_*.c) is ported from Waveshare's V2 examples and still needs a first run on the board.
//
// Power/latency rules this file follows:
//  1. A typical wake touches NO flash, I2C-except-RTC, Wi-Fi, mic, or e-paper. Today's total, today's limit and
//     days-left live in RTC memory, so a button press only has to update numbers and redraw.
//  2. E-paper keeps its image with no power. We never redraw on wake unless something changed, and the panel
//     is powered only while refreshing.
//  3. Redraws are coalesced: rapid presses update the number in memory and the panel refreshes once input pauses.
//  4. Hold-to-speak: the mic records from the moment a chord starts and stops on release, so it is on only as
//     long as you hold. Released before 1 s = discarded.
//  5. Short awake window, then deep sleep with every rail held off except the battery rail.
#include <string.h>
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "pins.h"
#include "hal.h"
#include "ui.h"
#include "button.h"
#include "budget.h"
#include "display.h"

// Bring-up mode: stay awake after the first power-on so the USB console and the screen can be watched, and log what happens.
// Set to 0 for the final low-power behaviour (sleep 2.5 s after the last press).
#define BRINGUP 0
static const char *TAG = "spend";
#if BRINGUP
#define AWAKE_AFTER_INPUT_MS 120000
#else
#define AWAKE_AFTER_INPUT_MS   2500
#endif   // quiet time before sleeping (a few presses in a row stay on one wake)
#define AWAKE_FLOW_MS          6000   // picker (after a failed recognition) waits this long for you; the mic is NOT on during this
#define REPEAT_MULT               5   // while a button is held, each repeat adds this many steps
#define REDRAW_QUIET_MS          60   // refresh the panel once presses pause this long
#define REDRAW_MAX_WAIT_MS      900   // ...but never keep the number stale longer than this while holding
#define FULL_REFRESH_EVERY       30   // after this many partial refreshes, one clean full refresh once you stop pressing (ghosting); also on each new day
#define SYNC_MAX_MS          600000   // sync mode closes after 10 minutes
#define MAX_AWAKE_MS         120000   // safety: never stay awake longer than this outside sync (stuck button, pocket press, hung panel)
#define VOICE_MAX_MS           5000   // safety cap on one hold-to-speak (stuck button, pocket squeeze)
// Hold-to-speak timing, applied inside the hal_voice_* implementation:
//   skip the first 150 ms of audio (button click), keep a 300 ms tail after release so the word's end isn't clipped.

// Everything below survives deep sleep (RTC slow memory, a few microamps).
RTC_DATA_ATTR static Ui      g_ui;
RTC_DATA_ATTR static bool    g_init;
RTC_DATA_ATTR static Date    g_day;            // the day the cached values belong to
RTC_DATA_ATTR static int32_t g_limit_today;    // fixed on the day's first wake
RTC_DATA_ATTR static int32_t g_spent_today;    // committed entries only (draft is separate)
RTC_DATA_ATTR static int     g_days_left;
RTC_DATA_ATTR static int32_t g_min_food;
RTC_DATA_ATTR static int32_t g_card_spent_today;           // part of today's entries that went on the card
RTC_DATA_ATTR static int32_t g_credit_today;               // card credit at the start of today
RTC_DATA_ATTR static bool    g_food_ok, g_on_card;     // from the plan: cash alone, or cash plus the credit card, covers the minimum food
RTC_DATA_ATTR static CatTable g_cats;       // ~560 bytes; edited on the website, so the device never touches flash for it
RTC_DATA_ATTR static Shown   g_shown;       // what the glass shows now (~100 bytes); re-rendered next wake to diff against
RTC_DATA_ATTR static int     g_batt;
RTC_DATA_ATTR static bool    g_clock_lost;     // the clock chip lost power at some point and has not been set from the phone yet
RTC_DATA_ATTR static uint8_t g_partials;


static bool s_low_batt;                        // this wake: sync was refused because the battery is too low
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static bool pressed(int pin) { return gpio_get_level((gpio_num_t)pin) == 0; }

static void rails_wake(void) {                       // undo the holds set before sleep; battery rail stays on
    gpio_hold_dis((gpio_num_t)PIN_VBAT_PWR);
    gpio_hold_dis((gpio_num_t)PIN_EPD_PWR);
    gpio_hold_dis((gpio_num_t)PIN_AUDIO_PWR);
    gpio_deep_sleep_hold_dis();
    gpio_set_direction((gpio_num_t)PIN_VBAT_PWR, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PIN_VBAT_PWR, 1);
    gpio_config_t c = { .pin_bit_mask = (1ULL << PIN_BTN_UP) | (1ULL << PIN_BTN_DOWN), .mode = GPIO_MODE_INPUT,
                        .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&c);
}

static void sleep_until_button(void) {
    hal_epd_power(false);                            // panel and codec rails off (both active low => drive high)
    gpio_set_direction((gpio_num_t)PIN_AUDIO_PWR, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PIN_AUDIO_PWR, 1);
    gpio_set_level((gpio_num_t)PIN_EPD_PWR, 1);
    gpio_hold_en((gpio_num_t)PIN_EPD_PWR);
    gpio_hold_en((gpio_num_t)PIN_AUDIO_PWR);
    gpio_deep_sleep_hold_en();
    esp_sleep_pd_config(ESP_PD_DOMAIN_MAX, ESP_PD_OPTION_AUTO);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (pressed(PIN_BTN_UP) || pressed(PIN_BTN_DOWN)) esp_sleep_enable_timer_wakeup(30ULL * 1000000ULL);   // a button is stuck down: waking on it would loop, so look again in 30 s
    else esp_sleep_enable_ext1_wakeup_io((1ULL << PIN_BTN_UP) | (1ULL << PIN_BTN_DOWN), ESP_EXT1_WAKEUP_ANY_LOW);
    rtc_gpio_hold_en((gpio_num_t)PIN_VBAT_PWR);      // without this the board powers off in deep sleep
    esp_deep_sleep_start();
}

// The panel is refreshed by its own task so the main loop keeps reading the buttons (a refresh takes ~0.5 s;
// presses during it used to be lost). Main builds a snapshot, the task draws it; only one refresh runs at a time.
typedef struct { bool full; Shown now; } RedrawJob;
static RedrawJob s_job;
static volatile bool s_busy;
static SemaphoreHandle_t s_go;

static void redraw_task(void *arg) {
    for (;;) {
        xSemaphoreTake(s_go, portMAX_DELAY);
        RedrawJob *j = &s_job;
        ESP_LOGI(TAG, "redraw start (full=%d, screen=%d, draft=%ld)", (int)j->full, (int)j->now.screen, (long)j->now.draft);
        bool full = j->full;
        static bool measured; if (!measured) { measured = true; g_batt = hal_battery_pct(); }   // once per wake, before the panel draws current
        j->now.home.battery_pct = g_batt;
        hal_epd_power(true);
        bool refreshed = display_show(&g_shown, &j->now, &g_cats, full);   // partial window unless forced / first / categories changed
        if (refreshed) { g_partials = full ? 0 : g_partials + 1; }
        ESP_LOGI(TAG, "redraw done (refreshed=%d)", (int)refreshed);
        s_busy = false;
    }
}

// Returns false (and does nothing) if the previous refresh is still running; the caller just asks again later.
static bool redraw(bool force_full) {
    if (s_busy) return false;
    if (!s_go) { s_go = xSemaphoreCreateBinary(); xTaskCreate(redraw_task, "redraw", 10240, NULL, 5, NULL); }
    g_ui.credit_ok = g_credit_today - g_card_spent_today > 0;
    Shown *now = &s_job.now;
    *now = (Shown){ .valid = true, .screen = g_ui.screen, .draft = g_ui.draft, .cursor = g_ui.cursor };
    date_label(g_day, now->date, sizeof now->date);
    strncpy(now->pass, hal_sync_password(), sizeof now->pass - 1);
    int32_t spent = g_spent_today + g_ui.draft;
    now->home = (HomeData){ now->date, g_batt, spent, g_ui.draft, g_limit_today, g_limit_today - spent, g_days_left,
                            g_min_food, !g_food_ok, g_spent_today, g_on_card, g_credit_today, g_card_spent_today,
                            (uint8_t)(g_ui.pay ? (g_ui.credit_ok ? 1 : 2) : 0), g_clock_lost, s_low_batt };
    s_job.full = force_full;
    s_busy = true;
    xSemaphoreGive(s_go);
    return true;
}

// Reads settings, bills and categories from flash and fixes the plan. Runs on a cold start, on a new day, and when sync ends.
static void reload_all(Date today, bool cold) {
    Settings s; Bill bills[32]; int nb = 0; int32_t step = 100;
    hal_load_settings_and_bills(&s, bills, &nb, &step, today);
    if (!hal_load_categories(&g_cats)) cats_default(&g_cats);
    hal_voice_set_commands(&g_cats);          // MultiNet takes new words at runtime: no retraining
    Plan p = budget_plan(&s, bills, nb, today);
    if (cold) { ui_init(&g_ui, step); g_spent_today = hal_recover_spent_today(today); g_card_spent_today = hal_card_spent_on(today); g_init = true; }
    else { g_ui.step = step; if (date_cmp(today, g_day) != 0) g_spent_today = 0, g_card_spent_today = 0; }
    g_limit_today = p.limit; g_days_left = p.days_left; g_min_food = s.min_food; g_food_ok = p.food_ok; g_on_card = p.on_card; g_credit_today = s.credit; g_day = today; g_clock_lost = hal_clock_lost();
}

void app_main(void) {
    ESP_LOGI(TAG, "boot: reset reason %d, wake cause %d", (int)esp_reset_reason(), (int)esp_sleep_get_wakeup_cause());
    esp_pm_config_t pm = { .max_freq_mhz = 80, .min_freq_mhz = 40, .light_sleep_enable = !BRINGUP };   // light sleep switches the USB port off
    esp_pm_configure(&pm);                           // vTaskDelay() below now idles the CPU in light sleep
    rails_wake();
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {                // the 30 s re-check while a button was stuck
        if (pressed(PIN_BTN_UP) || pressed(PIN_BTN_DOWN)) sleep_until_button();    // still stuck: straight back to sleep, nothing else powered
    }

    // Read buttons first: the press that woke us must not be lost while we do anything else.
    unsigned mask = 0;
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
        uint64_t w = esp_sleep_get_ext1_wakeup_status();
        if (w & (1ULL << PIN_BTN_UP))   mask |= 1;
        if (w & (1ULL << PIN_BTN_DOWN)) mask |= 2;
    }
    Btn btn; Event ev;
    bool have = button_start_from_wake(&btn, now_ms(), pressed(PIN_BTN_UP), pressed(PIN_BTN_DOWN), mask, &ev);

    Date today = hal_rtc_today();                    // the only I2C on a normal wake
    ESP_LOGI(TAG, "date %04d-%02d-%02d, init=%d", today.y, today.m, today.d, (int)g_init);
    bool need_full = false, new_day = date_cmp(today, g_day) != 0;
    if (!g_init || new_day) { reload_all(today, !g_init); need_full = true; }

    uint32_t last_input = now_ms(), first_dirty = 0, last_dirty = 0;
    bool dirty = need_full, recording = false;
    uint32_t listen_start = 0, sync_start = 0;
    for (;;) {
        uint32_t t = now_ms();
        if (!have) {
            have = button_poll(&btn, t, pressed(PIN_BTN_UP), pressed(PIN_BTN_DOWN), &ev);
            // Holding a button: never let the number run more than one step ahead of what the screen shows (a refresh takes ~0.5 s),
            // so it stops right where you let go instead of the screen catching up afterwards. Single taps are never held back.
            if (have && btn.repeated) {
                ev.mult = REPEAT_MULT;                                   // holding adds 5 steps at a time (500 with a step of 100)
                int32_t lead = g_ui.draft - s_job.now.draft; if (lead < 0) lead = -lead;
                if ((s_busy || dirty) && lead >= REPEAT_MULT * g_ui.step) have = false;
            }
        }

        // Record from the first instant of a chord (with a draft): audio is kept only if the hold reaches 1 s.
        bool chord = button_chord_active(&btn) && g_ui.screen == SCR_HOME && g_ui.draft != 0;
        if (chord && !recording) { hal_voice_start(); recording = true; listen_start = t; }
        if (!button_chord_active(&btn) && recording && g_ui.screen != SCR_LISTENING) { hal_voice_cancel(); recording = false; }

        // Sync mode ends by itself: after saving on the page, or after 5 minutes.
        if (g_ui.screen == SCR_SYNC) {
            if (sync_start == 0) sync_start = t ? t : 1;
            static uint32_t saved_at;
            if (hal_sync_saved() && !saved_at) saved_at = t ? t : 1;
            bool saved_done = saved_at && t - saved_at > 1200;                       // let the phone receive its "Saved" page first
            if (saved_done || hal_sync_idle_close() || t - sync_start > SYNC_MAX_MS) { saved_at = 0; ESP_LOGW(TAG, "sync closing (saved=%d)", (int)saved_done); have = true; ev = (Event){ EV_TIMEOUT, CAT_FOOD }; }
        } else sync_start = 0;

        // Utterance over: buttons released, or the safety cap hit.
        bool release = have && ev.type == EV_BOTH_RELEASE;
        bool capped = recording && g_ui.screen == SCR_LISTENING && t - listen_start > VOICE_MAX_MS;
        if (recording && g_ui.screen == SCR_LISTENING && (release || capped)) {
            Category c;
            Event v = hal_voice_finish(&c) ? (Event){ EV_VOICE, c } : (Event){ EV_VOICE_FAIL, CAT_FOOD };
            recording = false; have = true; ev = v; last_input = now_ms();
        }

        if (have) {
            have = false; last_input = t;
            Screen before = g_ui.screen;
            g_ui.credit_ok = g_credit_today - g_card_spent_today > 0;      // card still has credit: card is the normal way to pay
            UiResult r = ui_handle(&g_ui, ev, NULL, today, &g_cats);
            if (before == SCR_SYNC && g_ui.screen != SCR_SYNC) { hal_stop_sync_ap(); reload_all(today, false); r.redraw = true; }   // pick up website edits                          // no RAM ledger on the device
            if (r.saved) {
                Entry e = { 0, (uint8_t)r.cat, r.pay, r.amount };
                hal_ledger_append(&e, today);                                        // 8-byte flash append
                g_spent_today += r.amount;
                if (r.cat < 15 && r.pay == 2) g_card_spent_today += r.amount;
            }
            if (g_ui.screen == SCR_SYNC && r.redraw) {
                int mv = hal_battery_mv();
                if (mv && mv < 3200) { g_ui.screen = SCR_HOME; s_low_batt = true; r.redraw = true; }      // too low for the Wi-Fi radio: it could brown out in the middle of a save
                else hal_start_sync_ap();
            }
            if (r.redraw) { if (!dirty) first_dirty = t; dirty = true; last_dirty = t; }
        }

        // Coalesced refresh: wait for a pause in pressing, but cap how stale the number may get.
        if (dirty && !s_busy && (now_ms() - last_dirty >= REDRAW_QUIET_MS || now_ms() - first_dirty >= REDRAW_MAX_WAIT_MS)) {
            if (redraw(need_full)) { need_full = false; dirty = false; }
        }

        if (g_ui.screen != SCR_SYNC && !BRINGUP && t > MAX_AWAKE_MS) break;       // safety net
        uint32_t limit = (g_ui.screen == SCR_HOME) ? AWAKE_AFTER_INPUT_MS : AWAKE_FLOW_MS;
        if (!dirty && !s_busy && !recording && g_ui.screen != SCR_SYNC && !button_chord_active(&btn) && now_ms() - last_input > limit) {
            static bool cleaned;
            if (!cleaned && g_partials >= FULL_REFRESH_EVERY && g_ui.screen == SCR_HOME) {      // input has stopped: wipe the ghosts now, never in the middle of typing
                cleaned = true; need_full = true; dirty = true; first_dirty = last_dirty = now_ms() - REDRAW_QUIET_MS;
                last_input = now_ms() - limit + 300;                                              // sleep right after this refresh
            } else break;
        }
        vTaskDelay(pdMS_TO_TICKS(4));                // 4 ms polling so quick taps are seen (tick rate is 1000 Hz)
    }
    ESP_LOGI(TAG, "going to sleep");
    sleep_until_button();
}
