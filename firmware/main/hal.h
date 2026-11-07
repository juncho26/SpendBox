// Board layer. Everything that touches hardware lives behind these calls so the logic stays testable on a PC.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "budget.h"
#include "ledger.h"
#include "categories.h"

// ---- time, battery, power rails (hal_misc.c) ----
Date hal_rtc_today(void);                                   // PCF85063; sets itself from the build time if it never ran
bool hal_rtc_set(int y, int mo, int d, int h, int mi, int s);
int  hal_battery_pct(void);                                 // 0..100; 100 when no battery is fitted (USB powered)
void hal_epd_power(bool on);                                // GPIO6, active low

// ---- storage (hal_store.c): settings/categories in NVS, ledger in the "ledger" partition ----
typedef struct {
    int32_t balance_start;      // balance at the start of ref_day, i.e. before that day's spending
    Date    ref_day;
    Settings s;                 // s.balance is unused here; derived from balance_start and the ledger
    int32_t credit_left;        // card credit free on ref_day (typed on the website)
    int32_t card_limit;         // credit the card goes back to once a card bill is paid (typed on the website); 0 = add the bill amount instead
    int32_t step;               // amount per press
    int     nbills;
    Bill    bills[32];
} Config;
void    hal_config_default(Config *c);
bool    hal_config_load(Config *c);
bool    hal_config_save(const Config *c);
bool    hal_load_settings_and_bills(Settings *s, Bill *b, int *n, int32_t *step, Date today);
bool    hal_load_categories(CatTable *t);
bool    hal_save_categories(const CatTable *t);
int32_t hal_recover_spent_today(Date today);                // scans the ledger
void    hal_ledger_append(const Entry *e, Date today);      // 8 bytes; erases the next sector ahead of time
int32_t hal_spent_on(Date d);
int32_t hal_card_spent_on(Date d);   // today-style total, but only entries that went on the card
void    hal_ledger_visit(void (*f)(const Entry *e, void *u), void *u);   // every stored entry, storage order

// ---- voice (hal_voice.c): not built yet, so "not heard" leads to the category list ----
void hal_voice_set_commands(const CatTable *t);
void hal_voice_start(void);
void hal_voice_cancel(void);
bool hal_voice_finish(Category *out);

// ---- sync mode (hal_sync.c): Wi-Fi access point + the setup page ----
bool        hal_clock_lost(void);                    // the clock chip lost power: the date is the build date until the phone sets it
void        hal_clock_lost_set(bool lost);
bool        hal_wifi_pass_load(char *p12);          // the sync Wi-Fi password is made once and kept, so a phone that saved it can always rejoin
bool        hal_wifi_pass_save(const char *p12);
void        hal_start_sync_ap(void);
void        hal_stop_sync_ap(void);
const char *hal_sync_password(void);
bool        hal_sync_saved(void);
bool        hal_sync_idle_close(void);              // nobody joined for 2 minutes, or the phone has been quiet for a few minutes: close sync to save the battery
int         hal_battery_mv(void);                   // last battery reading in mV, 0 = not measured yet or no battery fitted                           // true once the page was saved
