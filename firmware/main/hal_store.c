#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "hal.h"

static const char *TAG = "store";
#define CFG_MAGIC 0x53504E33u   // "SPN3" (card flag on bills, credit left and card limit in the config)
typedef struct { uint32_t magic; Config c; } CfgBlob;

static bool nvs_ok;
static struct { bool ok; int day; int32_t all, card; } tc;     // today's totals from the last read of the ledger (kept until a save or a category change)
static void nvs_ensure(void) {
    if (nvs_ok) return;
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); e = nvs_flash_init(); }
    nvs_ok = (e == ESP_OK);
}
static bool blob_get(const char *key, void *p, size_t n) {
    nvs_ensure(); nvs_handle_t h; if (nvs_open("spend", NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = n; esp_err_t e = nvs_get_blob(h, key, p, &len); nvs_close(h);
    return e == ESP_OK && len == n;
}
static bool blob_put(const char *key, const void *p, size_t n) {
    nvs_ensure(); nvs_handle_t h; if (nvs_open("spend", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_set_blob(h, key, p, n); if (e == ESP_OK) e = nvs_commit(h); nvs_close(h);
    return e == ESP_OK;
}

// ---------------- settings ----------------
void hal_config_default(Config *c) {
    memset(c, 0, sizeof *c);
    c->balance_start = 100000;
    c->ref_day = hal_rtc_today();
    c->s = (Settings){ .balance = 0, .min_food = 500, .payday_day = 25, .delay_weekend = true };
    c->step = 100;
}
bool hal_config_load(Config *c) {
    CfgBlob b;
    if (blob_get("cfg", &b, sizeof b) && b.magic == CFG_MAGIC && b.c.nbills >= 0 && b.c.nbills <= 32) { *c = b.c; for (int i = 0; i < c->nbills; i++) c->bills[i].counts = true; return true; }   // every bill is held back from the daily limit
    hal_config_default(c);
    return false;
}
bool hal_config_save(const Config *c) { CfgBlob b = { CFG_MAGIC, *c }; return blob_put("cfg", &b, sizeof b); }
bool hal_wifi_pass_load(char *p12) { return blob_get("wifipw", p12, 12); }
bool hal_wifi_pass_save(const char *p12) { return blob_put("wifipw", p12, 12); }
bool hal_load_categories(CatTable *t) { return blob_get("cats", t, sizeof *t); }
bool hal_save_categories(const CatTable *t) { tc.ok = false; return blob_put("cats", t, sizeof *t); }
bool hal_clock_lost(void) { uint8_t v = 0; return blob_get("clklost", &v, 1) && v == 1; }
void hal_clock_lost_set(bool lost) { if (hal_clock_lost() == lost) return; uint8_t v = lost ? 1 : 0; blob_put("clklost", &v, 1); }   // written only when it changes

// ---------------- ledger: 8-byte entries in a ring of 4 KB sectors ----------------
#define SECTOR 4096u
static const esp_partition_t *part;
static const esp_partition_t *ledger_part(void) {
    if (!part) {
        part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "ledger");
        uint8_t ready = 0;                                       // a never-used partition holds whatever was in flash before: clear it once
        if (part && !(blob_get("ledger_ok", &ready, 1) && ready == 1)) {
            esp_partition_erase_range(part, 0, part->size);
            ready = 1; blob_put("ledger_ok", &ready, 1);
        }
    }
    return part;
}
static int day_index(Date d) { return date_days_between((Date){ 2020, 1, 1 }, d); }
// A half-written entry (power lost mid-write) or leftover garbage is skipped instead of being added to the totals.
static bool entry_ok(const Entry *e) { return e->cat < 16 && e->pad <= 2 && e->day < 40000; }
static bool is_empty(const Entry *e) { const uint8_t *p = (const uint8_t *)e; for (int i = 0; i < 8; i++) if (p[i] != 0xFF) return false; return true; }

// Visits every stored entry (in storage order). Also reports the write position: the first empty slot after a used one.
typedef void (*Visit)(const Entry *e, void *u);
static uint32_t scan(Visit f, void *u) {
    const esp_partition_t *p = ledger_part(); if (!p) return 0;
    static Entry buf[SECTOR / sizeof(Entry)];
    uint32_t total = p->size / sizeof(Entry), per = SECTOR / sizeof(Entry), wpos = 0; bool prev_used = false, found = false;
    Entry last; if (esp_partition_read(p, p->size - sizeof(Entry), &last, sizeof last) == ESP_OK) prev_used = !is_empty(&last);   // circular: slot 0 follows the last slot
    for (uint32_t s = 0; s < p->size / SECTOR; s++) {
        if (esp_partition_read(p, s * SECTOR, buf, SECTOR) != ESP_OK) continue;
        for (uint32_t i = 0; i < per; i++) {
            bool used = !is_empty(&buf[i]);
            if (used && f && entry_ok(&buf[i])) f(&buf[i], u);
            if (!used && prev_used && !found) { wpos = s * per + i; found = true; }
            prev_used = used;
        }
    }
    if (!found) wpos = 0;   // all empty -> start; never completely full (the next sector is erased ahead)
    (void)total;
    return wpos;
}

typedef struct { int day; int32_t sum; int from, to; bool range; } Acc;
static void acc_cb(const Entry *e, void *u) {
    Acc *a = u;
    if (a->range) { if (e->day >= a->from && e->day < a->to) a->sum += e->amount; }
    else if (e->day == a->day) a->sum += e->amount;
}
int32_t hal_spent_on(Date d) { int di = day_index(d); if (tc.ok && tc.day == di) return tc.all; Acc a = { .day = di }; scan(acc_cb, &a); return a.sum; }
int32_t hal_recover_spent_today(Date today) { return hal_spent_on(today); }

static bool on_card(uint16_t cash, uint8_t cat, uint8_t pad);
static uint16_t cash_mask(void);
void hal_ledger_append(const Entry *e, Date today) {
    const esp_partition_t *p = ledger_part(); if (!p) return;
    Entry w = *e; w.day = (uint16_t)day_index(today);   // pad keeps how it was paid: 1 cash, 2 card, 0 = old entry (category default)
    uint32_t pos = scan(NULL, NULL), per = SECTOR / sizeof(Entry), total = p->size / sizeof(Entry);
    esp_partition_write(p, pos * sizeof(Entry), &w, sizeof w);
    if (tc.ok && tc.day == (int)w.day) { tc.all += w.amount; if (on_card(cash_mask(), w.cat, w.pad)) tc.card += w.amount; }
    uint32_t next = (pos + 1) % total;
    if (next % per == 0) esp_partition_erase_range(p, next * sizeof(Entry), SECTOR);   // sector just filled: clear the next one ahead (oldest data goes)
    ESP_LOGI(TAG, "saved %ld cat %u at slot %lu", (long)w.amount, w.cat, (unsigned long)pos);
}

// Per-day totals for the days between ref_day and today (at most 62 days back), used to run the card credit forward day by day.
typedef struct { int from, n; uint16_t cash; int32_t sum[62]; } DayAcc;
static bool on_card(uint16_t cash, uint8_t cat, uint8_t pad) { return cat < 15 && pad != 1 && (pad == 2 || !(cash >> cat & 1)); }   // mistakes and cash entries never touch the card
static uint16_t cash_mask(void) { CatTable t; uint16_t m = 0; if (!hal_load_categories(&t)) return 0; for (int i = 0; i < 15; i++) if (t.c[i].used && t.c[i].cash) m |= (uint16_t)(1u << i); return m; }
typedef struct { int day; uint16_t cash; int32_t sum; } CardAcc;
static void card_cb(const Entry *e, void *u) { CardAcc *a = u; if ((int)e->day == a->day && on_card(a->cash, e->cat, e->pad)) a->sum += e->amount; }
int32_t hal_card_spent_on(Date d) { int di = day_index(d); if (tc.ok && tc.day == di) return tc.card; CardAcc a = { di, cash_mask(), 0 }; scan(card_cb, &a); return a.sum; }

typedef struct { Acc a; DayAcc d; int today; int32_t tall, tcard; } Pass;
static void pass_cb(const Entry *e, void *u) {
    Pass *p = u; int day = e->day; bool card = on_card(p->d.cash, e->cat, e->pad);
    if (day >= p->a.from && day < p->a.to) p->a.sum += e->amount;
    int i = day - p->d.from; if (i >= 0 && i < p->d.n && card) p->d.sum[i] += e->amount;
    if (day == p->today) { p->tall += e->amount; if (card) p->tcard += e->amount; }
}
// balance at the start of `today` = balance at the start of ref_day minus everything spent on the days in between
bool hal_load_settings_and_bills(Settings *s, Bill *b, int *n, int32_t *step, Date today) {
    Config c; bool had = hal_config_load(&c);
    // One read of the whole ledger gives everything the wake needs: the spending since ref_day (balance), the card spending day by day
    // (credit run forward) and today's totals. Card credit: what you spend on a day uses the card first (it never goes below 0, the
    // rest was cash), and the day a card bill is due the card is paid and goes back to the amount you set.
    static Pass ps; memset(&ps, 0, sizeof ps);
    ps.a = (Acc){ .range = true, .from = day_index(c.ref_day), .to = day_index(today) };
    Acc a = ps.a;
    int span = a.to - a.from; if (span < 0) span = 0; if (span > 62) span = 62;
    ps.d.from = a.to - span; ps.d.n = span; ps.d.cash = cash_mask(); ps.today = a.to;
    scan(pass_cb, &ps);
    a.sum = ps.a.sum; DayAcc da = ps.d;
    tc.ok = true; tc.day = a.to; tc.all = ps.tall; tc.card = ps.tcard;
    *s = c.s; s->balance = c.balance_start - a.sum;
    int32_t credit = c.credit_left;
    for (int k = 0; k < span; k++) {
        credit -= da.sum[k]; if (credit < 0) credit = 0;
        Date day = date_add_days(today, k - span);
        for (int i = 0; i < c.nbills; i++)
            if (c.bills[i].card && date_cmp(c.bills[i].due, day) == 0) credit = c.card_limit > 0 ? c.card_limit : credit + c.bills[i].amount;
    }
    s->credit = credit;
    for (int i = 0; i < c.nbills; i++)                       // a bill whose due date has passed is taken to be paid: the money is gone
        if (date_cmp(c.bills[i].due, c.ref_day) >= 0 && date_cmp(c.bills[i].due, today) < 0) s->balance -= c.bills[i].amount;
    *n = c.nbills; memcpy(b, c.bills, sizeof(Bill) * (size_t)c.nbills);
    *step = c.step > 0 ? c.step : 100;
    return had;
}


// Read-only walk over every stored entry (storage order, not date order). Used by the website's ledger page.
void hal_ledger_visit(void (*f)(const Entry *e, void *u), void *u) { scan(f, u); }
