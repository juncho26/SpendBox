// Sync mode: a WPA2 access point "SPENDBOX" (192.168.4.1) with a small setup website. The password is made once, kept on the device and
// shown on the device as text and QR. Nothing is stored on the phone; the page is served from the device.
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "hal.h"

static const char *TAG = "sync";
static char g_pass[12];
static volatile bool g_saved;
static httpd_handle_t g_http;
static esp_netif_t *g_ap;
static bool g_wifi_up;
static uint32_t g_t0, g_tact; static bool g_joined; static int g_nsta;     // for closing sync when nobody is using it
static uint32_t ms_now(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static uint32_t g_quiet_ms;                                                    // after a save: how long without a request before sync closes by itself
static void touch(void) { g_tact = ms_now(); }
bool hal_sync_idle_close(void) {
    if (!g_wifi_up) return false;
    uint32_t n = ms_now();
    if (g_quiet_ms) return n - g_tact > g_quiet_ms;               // something was saved: close once the phone goes quiet
    if (!g_joined) return n - g_t0 > 120000;                      // nobody ever joined
    return n - g_tact > (g_nsta > 0 ? 300000u : 120000u);          // a phone is connected but silent / the phone has left
}

static void load_pass(void) {
    if (g_pass[0]) return;
    char b[12] = { 0 };
    if (hal_wifi_pass_load(b) && strlen(b) == 8) { memcpy(g_pass, b, 12); return; }
    static const char alphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";    // no look-alike characters
    for (int i = 0; i < 8; i++) b[i] = alphabet[esp_random() % (sizeof alphabet - 1)];
    b[8] = 0; hal_wifi_pass_save(b); memcpy(g_pass, b, 12);
}
const char *hal_sync_password(void) { load_pass(); return g_pass; }
bool hal_sync_saved(void) { return g_saved; }

// ---------- form helpers ----------
static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static bool form_get(const char *body, const char *key, char *out, size_t cap) {
    size_t kl = strlen(key);
    for (const char *p = body; p && *p; ) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            p += kl + 1; size_t o = 0;
            while (*p && *p != '&' && o + 1 < cap) {
                if (*p == '+') out[o++] = ' ';
                else if (*p == '%' && hexv(p[1]) >= 0 && hexv(p[2]) >= 0) { out[o++] = (char)(hexv(p[1]) * 16 + hexv(p[2])); p += 2; }
                else out[o++] = *p;
                p++;
            }
            out[o] = 0; return true;
        }
        p = strchr(p, '&'); if (p) p++;
    }
    out[0] = 0; return false;
}
static int form_int(const char *body, const char *key, int dflt) { char b[24]; return form_get(body, key, b, sizeof b) && b[0] ? atoi(b) : dflt; }

// ---------- pages: Setup (/), Ledger (/ledger), Daily limit (/limit) ----------
static char ob[800];                                         // one output buffer: the http server handles one request at a time
static void out(httpd_req_t *r, const char *s) { if (s && *s) httpd_resp_send_chunk(r, s, HTTPD_RESP_USE_STRLEN); }   // a zero-length chunk would end the page
#define outf(r, ...) do { snprintf(ob, sizeof ob, __VA_ARGS__); out(r, ob); } while (0)

static const char *money(long v, int slot) {                 // 12,345 with a real minus sign
    static char b[4][20]; char t[16]; int n = 0; unsigned long a = v < 0 ? (unsigned long)-v : (unsigned long)v;
    do { if (n % 4 == 3) t[n++] = ','; t[n++] = (char)('0' + a % 10); a /= 10; } while (a);
    char *o = b[slot & 3]; int k = 0; if (v < 0) { memcpy(o, "\xE2\x88\x92", 3); k = 3; }
    while (n) o[k++] = t[--n];
    o[k] = 0; return o;
}
static const char *MON[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char *WDAY[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *MONFULL[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };

static const char CSS[] =
"*{box-sizing:border-box}body{margin:0;background:#0c0d0f;color:#ecebe4;font:16px system-ui,-apple-system,sans-serif}main{max-width:430px;margin:0 auto;padding:16px 16px 96px}"
"h1{font-size:24px;margin:6px 0 4px}.sub{font-size:12px;color:#9c9c95}nav{display:flex;gap:6px;margin:4px 0 12px}"
".tabs{position:fixed;left:0;right:0;bottom:0;display:flex;border-top:1px solid #2e3036;background:#15161a;padding-bottom:env(safe-area-inset-bottom)}.tabs a{flex:1;min-height:56px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:3px;font-size:11px;font-weight:600;color:#9c9c95;text-decoration:none}.tabs a.on{color:#ecebe4}.tabs i{width:22px;height:4px;border-radius:2px;background:transparent}.tabs a.on i{background:#ecebe4}"
".dots{display:flex;align-items:center;justify-content:space-between;margin:2px 0 4px}.dots div{display:flex;gap:5px}.dots i{width:8px;height:8px;border-radius:4px;background:#5a5c63}.dots i.on{width:22px;background:#ecebe4}.dots span{font-size:12px;color:#9c9c95}"
".dc{width:48px;height:48px;border-radius:12px;background:#2a2c31;display:flex;flex-direction:column;align-items:center;justify-content:center;font-size:11px;font-weight:700;line-height:1.1}.dc b{font-size:17px}.dc.inv{background:#ecebe4;color:#111}"
".br{display:flex;align-items:center;gap:12px;margin-top:8px;padding:10px 12px;background:#1e2024;border:1px solid #2e3036;border-radius:16px;color:inherit;text-decoration:none}.br>div:nth-child(2){flex:1;font-size:17px;font-weight:700}.tg{display:inline-block;padding:2px 8px;border-radius:99px;background:#2a2c31;font-size:11px;font-weight:600;margin:3px 5px 0 0}"
".bn{margin-top:6px;padding:10px 14px;border-radius:12px;background:#ecebe4;color:#111;font-size:14px;font-weight:600}.nv{display:flex;gap:10px;margin-top:16px}.nv a{flex:0 0 34%;min-height:52px;display:flex;align-items:center;justify-content:center;border:1px solid #5a5c63;border-radius:14px;color:#ecebe4;text-decoration:none;font-weight:600}.nv .go{margin-top:0;flex:1}.sec{width:100%;min-height:48px;margin-top:8px;border:1px solid #5a5c63;border-radius:12px;background:transparent;color:#ecebe4;font:600 14px system-ui}"
".c{background:#1e2024;border:1px solid #2e3036;border-radius:16px;padding:12px 14px;margin-top:10px}.c h2{font-size:14px;margin:0 0 2px}.c p{font-size:12px;color:#9c9c95;margin:2px 0 8px;line-height:1.5}"
"label{display:block;font-size:12px;color:#9c9c95;margin:8px 0 3px}input{width:100%;height:44px;padding:0 12px;border:1px solid #5a5c63;border-radius:10px;background:#15161a;color:#ecebe4;font:inherit;font-size:16px}"
"input[type=checkbox]{width:22px;height:22px;accent-color:#ecebe4}.ck{display:flex;align-items:center;gap:10px;min-height:44px;font-size:14px;color:#ecebe4;margin:0}.ck input{flex:none}"
".r{display:flex;gap:8px;align-items:flex-end;flex-wrap:wrap}.r>div{flex:1 1 0;min-width:0}input[type=date]{-webkit-appearance:none;appearance:none;display:block;min-width:0;max-width:100%;text-align:left;line-height:42px}input[type=date]::-webkit-date-and-time-value{text-align:left;min-height:1.2em}.bill,.cat{padding:10px 0;border-top:1px solid #2e3036}"
".x{width:44px;height:44px;border:0;border-radius:10px;background:transparent;color:#f87171;font-size:20px}.add{width:100%;min-height:44px;margin-top:8px;border:1px dashed #8a887e;border-radius:12px;background:transparent;color:#ecebe4;font-size:14px;font-weight:600}"
".go{width:100%;min-height:52px;margin-top:16px;border:0;border-radius:14px;background:#ecebe4;color:#111;font:600 16px system-ui}.pre{display:flex;gap:6px;margin-top:8px}.pre button{flex:1;min-height:44px;border:1px solid #5a5c63;border-radius:10px;background:transparent;color:#ecebe4;font-weight:600}"
".big{font-size:34px;font-weight:800;line-height:1.15}.pill{padding:14px 16px;border-radius:16px;background:#ecebe4;color:#111;display:flex;justify-content:space-between;align-items:center;margin-top:12px}"
".kv{display:flex;justify-content:space-between;align-items:center;min-height:44px;border-top:1px solid #2e3036;font-size:14px}.kv b{font-size:16px}.bar{height:12px;border-radius:6px;background:#2a2c31;margin:4px 0 10px}.bar i{display:block;height:100%;border-radius:6px;background:#ecebe4}"
".warn{color:#fdba74}.tag{display:inline-block;min-width:62px;text-align:center;padding:2px 8px;border-radius:99px;background:#2a2c31;font-size:12px;font-weight:600;margin-right:10px}"
".seg{display:flex;height:26px;border-radius:8px;overflow:hidden;border:2px solid #ecebe4;margin:12px 0}.seg i{display:block;height:100%}";

static int g_tab;
static void page_head(httpd_req_t *r, int tab, const char *title, const char *sub) {
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    out(r, "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>Spendbox</title><style>");
    out(r, CSS);
    out(r, "</style></head><body><main>");
    g_tab = tab; touch();
    outf(r, "<div class=sub>%s</div><h1>%s</h1>", sub, title);
}
static esp_err_t page_end(httpd_req_t *r, const char *script) {
    out(r, "<script>"); out(r, script); out(r, "</script></main><div class=tabs>");
    static const char *H[4] = { "/", "/ledger", "/limit", "/s1" }, *N[4] = { "Upcoming", "Ledger", "Limit", "Settings" };
    for (int i = 0; i < 4; i++) outf(r, "<a href=%s%s><i></i>%s</a>", H[i], i == g_tab ? " class=on" : "", N[i]);
    out(r, "</div></body></html>");
    return httpd_resp_send_chunk(r, NULL, 0);
}
static const char NOWJS[] = "function stamp(f){var d=new Date();f.now.value=[d.getFullYear(),d.getMonth()+1,d.getDate(),d.getHours(),d.getMinutes(),d.getSeconds()].join(',')}";

// ---- Upcoming (home) and the setup steps (Settings) ----
static void bill_row(httpd_req_t *r, const char *date, long amt, bool est, bool card) {
    outf(r, "<div class=bill><div class=r><div><label>Due date</label><input type=date class=d value='%s'></div>"
            "<div><label>Amount</label><input class=a inputmode=numeric placeholder=0 value='%s'></div></div>"
            "<div class=r><label class=ck><input type=checkbox class=e %s>Estimate</label>"
            "<label class=ck><input type=checkbox class=k %s>This pays my credit card (frees the card again)</label>"
            "<button type=button class=x aria-label='Remove bill'>&#10005;</button></div></div>",
            date, amt ? money(amt, 0) : "", est ? "checked" : "", card ? "checked" : "");
}
static void cat_row(httpd_req_t *r, const char *label, const char *voice, bool cash) {
    outf(r, "<div class=cat><div class=r><div><label>Name on device</label><input class=l maxlength=8 placeholder='up to 8 letters' value='%s'></div>"
            "<div><label>You say</label><input class=v maxlength=23 placeholder='e.g. text books' value='%s'></div>"
            "<button type=button class=x aria-label='Remove category'>&#10005;</button></div>"
            "<div class=r><label class=ck><input type=checkbox class=c2 %s>Cash only (never uses card credit)</label></div></div>", label, voice, cash ? "checked" : "");
}
static void chip(httpd_req_t *r, Date d, bool inv) { outf(r, "<div class='dc%s'>%s<b>%d</b></div>", inv ? " inv" : "", MON[d.m - 1], d.d); }

static esp_err_t page_upcoming(httpd_req_t *r) {
    Date today = hal_rtc_today();
    Settings s; Bill bl[32]; int nb = 0; int32_t step = 0;
    hal_load_settings_and_bills(&s, bl, &nb, &step, today);
    Plan p = budget_plan(&s, bl, nb, today);
    Config c; if (!hal_config_load(&c)) hal_config_default(&c);
    long card_now = (long)s.credit - (long)hal_card_spent_on(today); if (card_now < 0) card_now = 0;
    char sub[48]; snprintf(sub, sizeof sub, "%s %d %s on the device", WDAY[date_weekday(today)], today.d, MON[today.m - 1]);
    page_head(r, 0, "Upcoming", sub);
    outf(r, "<div class=bn>%d days to payday &middot; today's limit %s%s</div>", p.days_left, p.uses_default ? "~" : "", money(p.limit, 0));
    outf(r, "<form class=c method=post action=/save onsubmit='stamp(this)'><input type=hidden name=now><input type=hidden name=next value=/>"
            "<h2>%s</h2><div class=r><div><p style='margin:0'>Device clock: %s %d %s</p></div>"
            "<button class=add style='width:auto;padding:0 16px;margin:0;border-style:solid'>Set from phone</button></div></form>",
         hal_clock_lost() ? "Clock needs setting" : "Clock", WDAY[date_weekday(today)], today.d, MON[today.m - 1]);
    int shown = 0;
    for (int i = 0; i < nb; i++) {
        if (date_cmp(bl[i].due, today) < 0) continue;
        shown++;
        out(r, "<a class=br href=/s2>"); chip(r, bl[i].due, false);
        outf(r, "<div>%s%s<br>%s%s</div><span>&rsaquo;</span></a>", bl[i].is_default ? "~" : "", money(bl[i].amount, 0),
             bl[i].is_default ? "<span class=tg>estimate</span>" : "", bl[i].card ? "<span class=tg>pays card</span>" : "");
    }
    if (!shown) out(r, "<div class=c><p>No bills coming up. Add one under Settings.</p></div>");
    (void)0;
    out(r, "<a class=br href=/s3>"); chip(r, p.payday, true);
    outf(r, "<div>Payday<br><span class=tg>%s %d %s</span></div><span>&rsaquo;</span></a>", WDAY[date_weekday(p.payday)], p.payday.d, MON[p.payday.m - 1]);
    if (s.credit > 0 || c.card_limit > 0)
        outf(r, "<div class=c><div class=kv style='border:0'><span>Card credit left</span><b>%s</b></div><div class=kv><span>After your card payment</span><b>%s</b></div></div>",
             money(card_now, 0), c.card_limit > 0 ? money(c.card_limit, 1) : "adds the bill");
    out(r, "<a class=sec style='display:flex;align-items:center;justify-content:center;text-decoration:none' href=/s2>+ Add bill or saving</a>");
    return page_end(r, NOWJS);
}

static void step_head(httpd_req_t *r, int n, const char *title, const char *sub) {
    page_head(r, 3, title, sub);
    out(r, "<div class=dots><div>");
    for (int i = 1; i <= 5; i++) outf(r, "<i%s></i>", i == n ? " class=on" : "");
    outf(r, "</div><span>Step %d of 5</span></div>", n);
}
static void step_open(httpd_req_t *r, int n, const char *extra) {
    outf(r, "<form method=post action=/save accept-charset=utf-8 onsubmit='return go(this)'><input type=hidden name=now><input type=hidden name=next value=/s%d>%s", n + 1, extra);
}
static void step_close(httpd_req_t *r, int n, const char *label) {
    out(r, "<div class=nv>");
    if (n > 1) outf(r, "<a href=/s%d>Back</a>", n - 1);
    outf(r, "<button class=go>%s</button></div></form>", label);
}
static const char STEPJS[] =
    "function q(s,e){return(e||document).querySelector(s)}function qa(s){return[].slice.call(document.querySelectorAll(s))}"
    "function add(t,b){q('#'+b).appendChild(q('#'+t).content.cloneNode(true))}"
    "document.addEventListener('click',function(e){var x=e.target.closest('.x');if(x)x.closest('.bill,.cat').remove();var p=e.target.closest('[data-v]');if(p)q('[name=step]').value=p.getAttribute('data-v')});"
    "function stamp(f){var d=new Date();f.now.value=[d.getFullYear(),d.getMonth()+1,d.getDate(),d.getHours(),d.getMinutes(),d.getSeconds()].join(',')}"
    "function go(f){stamp(f);if(f.bills)f.bills.value=qa('.bill').map(function(r){var d=q('.d',r).value,a=q('.a',r).value.replace(/\\D/g,'');return d&&a?d+' '+a+(q('.e',r).checked?' est':'')+(q('.k',r).checked?' card':''):''}).filter(Boolean).join('\\n');"
    "if(f.cats)f.cats.value=qa('.cat').map(function(r){var l=q('.l',r).value.trim(),v=q('.v',r).value.trim();return l?l+', '+(v||l)+(q('.c2',r).checked?', cash':''):''}).filter(Boolean).join('\\n');"
    "['balance','credit','cardlimit'].forEach(function(k){if(f[k])f[k].value=f[k].value.replace(k=='balance'?/[^0-9-]/g:/[^0-9]/g,'')});return true}";

static esp_err_t page_step(httpd_req_t *r) {
    int n = (int)(intptr_t)r->user_ctx;
    Config c; if (!hal_config_load(&c)) hal_config_default(&c);
    Date today = hal_rtc_today();
    Settings s; Bill bl[32]; int nb = 0; int32_t step = 0;
    hal_load_settings_and_bills(&s, bl, &nb, &step, today);
    long card_today = (long)hal_card_spent_on(today), bal_now = (long)s.balance - (long)hal_spent_on(today);
    if (n == 1) {
        step_head(r, 1, "Money now", "Open your bank app first, then type what you see.");
        step_open(r, 1, "<input type=hidden name=full value=1>");
        outf(r, "<div class=c><h2>Balance now</h2><input name=balance inputmode=numeric value=%ld><p style='margin-top:6px'>The device keeps this up to date from your entries.</p></div>", bal_now);
        outf(r, "<div class=c><h2>Credit left on your card</h2><input name=credit inputmode=numeric value=%ld><p style='margin-top:6px'>Goes down as you record card spending. Categories ticked Cash only do not use it.</p></div>",
             (long)(s.credit > card_today ? s.credit - card_today : 0));
        outf(r, "<div class=c><h2>Credit after you pay the card</h2><input name=cardlimit inputmode=numeric value=%ld><p style='margin-top:6px'>When a bill ticked &ldquo;pays my credit card&rdquo; has been paid, your credit goes back to this amount, for example your card limit. Empty = add the bill amount.</p></div>", (long)c.card_limit);
        out(r, "<p style='margin-top:10px'>Every step saves on its own, so you can stop any time.</p>");
        step_close(r, 1, "Save and next &middot; Bills");
    } else if (n == 2) {
        step_head(r, 2, "Bills before payday", "Add each bill or saving. Tap &#10005; to remove.");
        step_open(r, 2, "<input type=hidden name=bills>");
        out(r, "<div class=c><p>Pick the date and type the amount. Tick Estimate if you are not sure yet.</p><div id=bills>");
        for (int i = 0; i < c.nbills; i++) {
            char ds[16]; snprintf(ds, sizeof ds, "%04d-%02d-%02d", c.bills[i].due.y, c.bills[i].due.m, c.bills[i].due.d);
            bill_row(r, ds, (long)c.bills[i].amount, c.bills[i].is_default, c.bills[i].card);
        }
        out(r, "</div><button type=button class=add onclick=\"add('bt','bills')\">+ Add bill or savings</button></div>");
        step_close(r, 2, "Next &middot; Rules");
        out(r, "<template id=bt>"); bill_row(r, "", 0, false, false); out(r, "</template>");
    } else if (n == 3) {
        step_head(r, 3, "Rules", "Payday, food and your button step.");
        step_open(r, 3, "<input type=hidden name=rules value=1>");
        outf(r, "<div class=c><h2>Payday</h2><label>Day of the month</label><input name=payday inputmode=numeric style='width:90px;text-align:center;font-weight:700' value=%d>"
                "<label class=ck><input type=checkbox name=delay value=1 %s>If it falls on a weekend, use the next Monday</label></div>", s.payday_day, s.delay_weekend ? "checked" : "");
        outf(r, "<div class=c><h2>Minimum food per day</h2><input name=minfood inputmode=numeric value=%ld></div>", (long)s.min_food);
        outf(r, "<div class=c><h2>Clock</h2><p>Device says %s %d %s. A new day starts at midnight on this clock. Travelling? Set it from this phone.</p>"
                "<button type=button class=sec onclick=\"this.form.next.value='/s3';this.form.requestSubmit()\">Set clock from this phone</button></div>", WDAY[date_weekday(today)], today.d, MON[today.m - 1]);
        outf(r, "<div class=c><h2>Amount per button press</h2><input name=step inputmode=numeric value=%ld><div class=pre>"
                "<button type=button data-v=10>10</button><button type=button data-v=100>100</button><button type=button data-v=500>500</button><button type=button data-v=1000>1000</button><button type=button data-v=5000>5000</button></div></div>", (long)step);
        step_close(r, 3, "Next &middot; Categories");
    } else if (n == 4) {
        CatTable t; if (!hal_load_categories(&t)) cats_default(&t);
        step_head(r, 4, "Categories", "The names shown on the device. Up to 5.");
        step_open(r, 4, "<input type=hidden name=cats>");
        out(r, "<div class=c><p>Removing one keeps your history. Tick Cash only for things you can only pay in cash.</p><div id=cats>");
        for (int i = 0; i < MAX_CATS - 1; i++) if (t.c[i].used && t.c[i].active) cat_row(r, t.c[i].label, t.c[i].voice, t.c[i].cash);
        out(r, "</div><button type=button class=add onclick=\"if(document.querySelectorAll('.cat').length<5)add('ct','cats');else alert('Remove one first (up to 5)')\">+ Add category</button></div>");
        step_close(r, 4, "Next &middot; Check");
        out(r, "<template id=ct>"); cat_row(r, "", "", false); out(r, "</template>");
    } else {
        Plan p = budget_plan(&s, bl, nb, today);
        step_head(r, 5, "All set", "Here is what the device will use.");
        long due = 0; for (int i = 0; i < nb; i++) if (date_cmp(bl[i].due, today) >= 0 && date_cmp(bl[i].due, p.payday) < 0) due += bl[i].amount;
        out(r, "<div class=c>");
        outf(r, "<div class=kv style='border:0'><span>Balance now</span><b>%s</b></div>", money(bal_now, 0));
        long spent_today = (long)s.balance - bal_now;                       // today's limit is worked out from the balance before today's spending
        if (spent_today > 0) outf(r, "<div class=kv><span>Already spent today</span><b>+%s</b></div>", money(spent_today, 0));
        outf(r, "<div class=kv><span>Bills until payday</span><b>%s</b></div>", money(-due, 0));
        outf(r, "<div class=kv><span>Available for the days left</span><b>%s</b></div>", money(p.available, 0));
        outf(r, "<div class=kv><span>Credit left on card</span><b>%s</b></div>", money(s.credit > card_today ? s.credit - card_today : 0, 0));
        outf(r, "<div class=kv><span>Credit after paying the card</span><b>%s</b></div></div>", money(c.card_limit, 0));
        outf(r, "<div class=pill><div><div style='font-size:12px'>%d days left &middot; food kept</div><div style='font-size:14px;font-weight:600'>Today's limit</div></div><div style='font-size:38px;font-weight:800'>%s%s</div></div>", p.days_left, p.uses_default ? "~" : "", money(p.limit, 1));
        out(r, "<form method=post action=/save onsubmit='stamp(this)'><input type=hidden name=now><input type=hidden name=done value=1><div class=nv><a href=/s4>Back</a><button class=go>Done &middot; send to device</button></div>"
               "<p style='text-align:center;margin-top:8px'>The device closes sync and updates its screen.</p></form>");
    }
    return page_end(r, STEPJS);
}

// ---- Ledger ----
typedef struct { int ym0; int32_t tot[MAX_CATS]; uint32_t days; struct { int day; uint8_t cat, pad; int32_t amt; } last[12]; int nl; } LedgerView;
static void ledger_cb(const Entry *e, void *u) {
    LedgerView *v = u; Date d = date_add_days((Date){ 2020, 1, 1 }, e->day);
    if (d.y * 12 + d.m - 1 == v->ym0) { v->tot[e->cat & 15] += e->amount; v->days |= 1u << (d.d & 31); }
    int k = v->nl;                                           // keep the 12 newest entries, newest first
    if (k < 12) v->nl++; else if (e->day < v->last[11].day) return; else k = 11;
    while (k > 0 && v->last[k - 1].day < e->day) { v->last[k] = v->last[k - 1]; k--; }
    v->last[k].day = e->day; v->last[k].cat = e->cat; v->last[k].pad = e->pad; v->last[k].amt = e->amount;
}
static esp_err_t page_ledger(httpd_req_t *r) {
    Date today = hal_rtc_today();
    CatTable t; if (!hal_load_categories(&t)) cats_default(&t);
    static LedgerView v; memset(&v, 0, sizeof v); v.ym0 = today.y * 12 + today.m - 1;
    hal_ledger_visit(ledger_cb, &v);
    int ndays = 0; for (int i = 1; i < 32; i++) if (v.days & (1u << i)) ndays++;
    int32_t total = 0, mx = 1; for (int i = 0; i < MAX_CATS; i++) { total += v.tot[i]; int32_t a = v.tot[i] < 0 ? -v.tot[i] : v.tot[i]; if (a > mx) mx = a; }
    char sub[40]; snprintf(sub, sizeof sub, "%d days logged", ndays);
    page_head(r, 1, MONFULL[today.m - 1], sub);
    outf(r, "<div class=c><div class=sub>Spent this month</div><div class=big>%s</div>", money(total, 0));
    for (int i = 0; i < MAX_CATS; i++) {
        bool show = (i == CAT_MISTAKE) ? v.tot[i] != 0 : (t.c[i].used && (t.c[i].active || v.tot[i] != 0));
        if (!show) continue;
        int32_t a = v.tot[i] < 0 ? -v.tot[i] : v.tot[i];
        outf(r, "<div class='kv%s' style='border:0;min-height:30px;margin-top:6px'><span>%s</span><b>%s</b></div><div class=bar><i style='width:%d%%%s'></i></div>",
             i == CAT_MISTAKE ? " warn" : "", cats_label(&t, (Category)i), money(v.tot[i], 1), (int)((int64_t)a * 100 / mx), i == CAT_MISTAKE ? ";background:#fb923c" : "");
    }
    out(r, "</div><div class=c><h2>Latest</h2>");
    if (!v.nl) out(r, "<p>No entries yet.</p>");
    for (int i = 0; i < v.nl; i++) {
        Date d = date_add_days((Date){ 2020, 1, 1 }, v.last[i].day);
        outf(r, "<div class=kv><span><span class=tag%s>%s</span>%s %d %s%s</span><b%s>%s</b></div>", v.last[i].cat == CAT_MISTAKE ? " style='color:#fdba74'" : "",
             cats_label(&t, v.last[i].cat), WDAY[date_weekday(d)], d.d, MON[d.m - 1], (v.last[i].pad == 1 || (v.last[i].pad == 0 && v.last[i].cat < 15 && t.c[v.last[i].cat].cash)) ? " · cash" : "", v.last[i].amt < 0 ? " class=warn" : "", money(v.last[i].amt, 2));
    }
    out(r, "</div>");
    return page_end(r, "");
}

// ---- Daily limit ----
static esp_err_t page_limit(httpd_req_t *r) {
    Date today = hal_rtc_today();
    Settings s; Bill bl[32]; int nb = 0; int32_t step = 0;
    hal_load_settings_and_bills(&s, bl, &nb, &step, today);
    Plan p = budget_plan(&s, bl, nb, today);
    long spent = (long)hal_spent_on(today), bal_now = (long)s.balance - spent;
    char sub[64]; snprintf(sub, sizeof sub, "%s %d %s", WDAY[date_weekday(today)], today.d, MON[today.m - 1]);
    char title[48]; snprintf(title, sizeof title, "%d days to payday", p.days_left);
    page_head(r, 2, "Daily limit", sub);
    outf(r, "<div class=c style='margin-top:0'><div class=sub>%s &middot; payday %s %d %s</div></div>", title, WDAY[date_weekday(p.payday)], p.payday.d, MON[p.payday.m - 1]);
    outf(r, "<form class=c method=post action=/save onsubmit='stamp(this)'><input type=hidden name=now><input type=hidden name=next value=/limit><h2>Balance now</h2><p>Type the real amount to correct it.</p><div class=r><div><input name=balance inputmode=numeric value=%ld></div>"
            "<button class=add style='width:auto;padding:0 16px;margin:0;border-style:solid'>Correct</button></div></form>", bal_now);
    long bal0 = (long)s.balance;
    out(r, "<div class=c><h2>Where the money goes until payday</h2>");
    if (bal0 > 0) {
        out(r, "<div class=seg>");
        long used = 0;
        for (int i = 0; i < nb; i++) if (date_cmp(bl[i].due, today) >= 0 && date_cmp(bl[i].due, p.payday) < 0) {
            int w = (int)((int64_t)bl[i].amount * 100 / bal0); if (used * 100 / bal0 + w > 100) w = (int)(100 - used * 100 / bal0); used += bl[i].amount;
            if (w > 0) outf(r, "<i style='width:%d%%;background:%s'></i>", w, bl[i].is_default ? "repeating-linear-gradient(135deg,#fb923c 0 3px,#3d2210 3px 7px)" : "#8a887e");
        }
        outf(r, "<i style='flex:1;background:#ecebe4'></i></div>");
    }
    for (int i = 0; i < nb; i++) if (date_cmp(bl[i].due, today) >= 0 && date_cmp(bl[i].due, p.payday) < 0)
        outf(r, "<div class=kv><span>%s %d%s</span><b>%s%s</b></div>", MON[bl[i].due.m - 1], bl[i].due.d, bl[i].is_default ? " &middot; estimate" : "",
             bl[i].is_default ? "~" : "", money(bl[i].amount, 0));
    outf(r, "<div class=kv><span>Available until payday</span><b>%s</b></div></div>", money(p.available, 0));
    outf(r, "<div class=pill><div><div style='font-size:12px'>%d days left &middot; food kept</div><div style='font-size:14px;font-weight:600'>Today's limit</div></div><div style='font-size:38px;font-weight:800'>%s%s</div></div>",
         p.days_left, p.uses_default ? "~" : "", money(p.limit, 1));
    if (p.on_card) outf(r, "<div class=c><h2>Food is covered by your card</h2><p>Cash alone would not cover today's minimum food of %s, so the card carries it. Credit left on the card: %s.</p></div>", money(s.min_food, 2), money(p.credit, 3));
    else if (!p.food_ok) outf(r, "<div class='c warn'>Not enough to keep the minimum food of %s per day.</div>", money(s.min_food, 2));
    out(r, "<div class=c><h2>What tomorrow looks like</h2>");
    long tries[3] = { 0, p.limit, p.limit + p.limit / 2 };
    for (int i = 0; i < 3; i++) outf(r, "<div class=kv><span>Spend %s today</span><b>%s</b></div>", money(tries[i], 0), money(budget_tomorrow_limit(&p, (int32_t)tries[i]), 1));
    out(r, "<p style='margin-top:6px'>The limit is the most you can spend while keeping the minimum food for every day after.</p></div>");
    return page_end(r, NOWJS);
}

static bool parse_bill(const char *line, Bill *b) {
    int y, m, d; long amt;
    if (sscanf(line, "%d-%d-%d %ld", &y, &m, &d, &amt) != 4) return false;
    if (y < 2020 || m < 1 || m > 12 || d < 1 || d > 31 || amt < 0) return false;
    *b = (Bill){ .due = { (int16_t)y, (uint8_t)m, (uint8_t)d }, .amount = (int32_t)amt, .is_default = strstr(line, "est") != NULL, .counts = true, .card = strstr(line, "card") != NULL };
    return true;
}
static void trim(char *s) { size_t n = strlen(s); while (n && (s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = 0; while (*s == ' ') memmove(s, s + 1, strlen(s)); }

static esp_err_t save_post(httpd_req_t *r) {
    touch();
    static char body[4500];
    int len = r->content_len; if (len <= 0 || len >= (int)sizeof body) { httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "too big"); return ESP_FAIL; }
    for (int got = 0; got < len; ) { int n = httpd_req_recv(r, body + got, len - got); if (n <= 0) return ESP_FAIL; got += n; }
    body[len] = 0;

    char nowb[40]; int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (form_get(body, "now", nowb, sizeof nowb) && sscanf(nowb, "%d,%d,%d,%d,%d,%d", &y, &mo, &d, &h, &mi, &s) == 6) { hal_rtc_set(y, mo, d, h, mi, s); hal_clock_lost_set(false); }
    Date today = hal_rtc_today();

    Config c; if (!hal_config_load(&c)) hal_config_default(&c);
    char tmp[8]; bool full = form_get(body, "full", tmp, sizeof tmp);   // the Setup page sends everything; the Limit page only the balance
    if (form_get(body, "balance", tmp, sizeof tmp) && tmp[0]) {
        int32_t typed = form_int(body, "balance", 0);
        c.balance_start = typed + hal_spent_on(today);      // balance at the start of today, before today's entries
        c.ref_day = today;
        if (form_get(body, "credit", tmp, sizeof tmp) && tmp[0]) c.credit_left = atoi(tmp) + (int32_t)hal_card_spent_on(today);   // credit at the start of today
        else if (full) c.credit_left = 0;
        if (form_get(body, "cardlimit", tmp, sizeof tmp) && tmp[0]) c.card_limit = atoi(tmp);
        else if (full) c.card_limit = 0;
    }
    if (form_get(body, "payday", tmp, sizeof tmp) && tmp[0]) { c.s.payday_day = atoi(tmp); if (c.s.payday_day < 1 || c.s.payday_day > 31) c.s.payday_day = 25; }
    if (form_get(body, "rules", tmp, sizeof tmp)) c.s.delay_weekend = form_get(body, "delay", tmp, sizeof tmp);
    if (form_get(body, "minfood", tmp, sizeof tmp) && tmp[0]) c.s.min_food = atoi(tmp);
    if (form_get(body, "step", tmp, sizeof tmp) && tmp[0]) { c.step = atoi(tmp); if (c.step < 1) c.step = 100; }

    static char text[1500];
    if (form_get(body, "bills", text, sizeof text)) {
        c.nbills = 0;
        for (char *line = strtok(text, "\n"); line && c.nbills < 32; line = strtok(NULL, "\n")) { trim(line); Bill b; if (parse_bill(line, &b)) c.bills[c.nbills++] = b; }
    }
    hal_config_save(&c);

    // categories: keep ids of labels that stay, archive the ones that went, add new ones
    CatTable t; if (!hal_load_categories(&t)) cats_default(&t);
    static char ctext[600]; static char want[CAT_MAX_ACTIVE][2][CAT_VOICE_MAX + 1]; bool want_cash[CAT_MAX_ACTIVE]; int nw = 0;
    if (form_get(body, "cats", ctext, sizeof ctext)) {
        for (char *line = strtok(ctext, "\n"); line && nw < CAT_MAX_ACTIVE; line = strtok(NULL, "\n")) {
            trim(line); if (!*line) continue;
            char *comma = strchr(line, ','); char *spoken = line;
            bool cash = false;
            if (comma) { *comma = 0; spoken = comma + 1; char *c2 = strchr(spoken, ','); if (c2) { *c2 = 0; cash = strstr(c2 + 1, "cash") != NULL; } trim(spoken); }
            char lab[CAT_LABEL_MAX + 1]; size_t o = 0;
            for (char *q = line; *q && o < CAT_LABEL_MAX; q++) if ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9')) lab[o++] = (char)((*q >= 'a' && *q <= 'z') ? *q - 32 : *q);
            lab[o] = 0; if (!o) continue;
            strncpy(want[nw][0], lab, CAT_LABEL_MAX); want[nw][0][CAT_LABEL_MAX] = 0;
            strncpy(want[nw][1], *spoken ? spoken : line, CAT_VOICE_MAX); want[nw][1][CAT_VOICE_MAX] = 0;
            for (char *q = want[nw][1]; *q; q++) { if (*q >= 'A' && *q <= 'Z') *q += 32; else if (!(*q >= 'a' && *q <= 'z') && *q != ' ') *q = ' '; }
            want_cash[nw] = cash; nw++;
        }
        if (nw > 0) {
            for (int i = 0; i < MAX_CATS - 1; i++) {
                if (!(t.c[i].used && t.c[i].active)) continue;
                bool keep = false; for (int k = 0; k < nw; k++) if (!strcmp(t.c[i].label, want[k][0])) keep = true;
                if (!keep) cats_remove(&t, (Category)i);
            }
            for (int k = 0; k < nw; k++) {
                int at = -1; for (int i = 0; i < MAX_CATS - 1; i++) if (t.c[i].used && t.c[i].active && !strcmp(t.c[i].label, want[k][0])) at = i;
                Category id; if (at < 0 && cats_add(&t, want[k][0], want[k][1], &id) == CATS_OK) at = id;
                if (at >= 0) t.c[at].cash = want_cash[k];
            }
            hal_save_categories(&t);
        }
    }
    char nxt[12];
    if (form_get(body, "next", nxt, sizeof nxt) && nxt[0] == '/') {          // a setup step: stay in sync mode, show the next step
        ESP_LOGW(TAG, "step saved, next %s", nxt);
        bool setup = nxt[1] == 's' && nxt[2] >= '1' && nxt[2] <= '5';
        g_quiet_ms = (setup || nxt[1] == 0) ? 120000u : 20000u;                                  // setup steps: more time to fill in the next one
        httpd_resp_set_status(r, "303 See Other"); httpd_resp_set_hdr(r, "Location", nxt);
        return httpd_resp_send(r, "", 0);
    }
    g_saved = true;
    ESP_LOGW(TAG, "saved: balance_start %ld, %d bills", (long)c.balance_start, c.nbills);
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_sendstr(r, "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><body style='background:#0c0d0f;color:#ecebe4;font:18px system-ui;padding:24px'>"
                          "<h2>Saved</h2><p>The device is updating. You can close this page.</p></body>");
    return ESP_OK;
}


// ---------- captive portal: answer every DNS lookup with our own address and redirect every unknown page to the setup page,
// so a phone that joins SPENDBOX opens the setup page by itself (iOS and Android both probe for internet and show a sheet).
static int g_dns_sock = -1;
static TaskHandle_t g_dns_task;
static void dns_task(void *arg) {
    static uint8_t buf[300];
    int sock = g_dns_sock;
    while (g_dns_sock == sock) {                                      // stops when dns_stop() clears it (checked at least once a second)
        struct sockaddr_in from; socklen_t fl = sizeof from;
        int n = recvfrom(sock, buf, sizeof buf - 16, 0, (struct sockaddr *)&from, &fl);
        if (n < 12) continue;
        int q = 12; while (q < n && buf[q]) q += buf[q] + 1;          // end of the question name
        q += 5;                                                       // zero byte + type + class
        if (q > n) continue;
        buf[2] = 0x81; buf[3] = 0x80; buf[6] = 0; buf[7] = 1; buf[8] = buf[9] = buf[10] = buf[11] = 0;   // response, one answer
        static const uint8_t ans[] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 30, 0, 4, 192, 168, 4, 1 };
        memcpy(buf + q, ans, sizeof ans);
        sendto(sock, buf, q + sizeof ans, 0, (struct sockaddr *)&from, fl);
    }
    close(sock); g_dns_task = NULL; vTaskDelete(NULL);
}
static void dns_start(void) {
    g_dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_dns_sock < 0) return;
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (bind(g_dns_sock, (struct sockaddr *)&a, sizeof a) < 0) { close(g_dns_sock); g_dns_sock = -1; return; }
    struct timeval tv = { .tv_sec = 1 }; setsockopt(g_dns_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    xTaskCreate(dns_task, "dns", 3072, NULL, 4, &g_dns_task);
}
static void dns_stop(void) { g_dns_sock = -1; for (int i = 0; i < 15 && g_dns_task; i++) vTaskDelay(pdMS_TO_TICKS(100)); }
static esp_err_t redirect_404(httpd_req_t *r, httpd_err_code_t e) {
    (void)e; touch();
    httpd_resp_set_status(r, "302 Found");
    httpd_resp_set_hdr(r, "Location", "http://192.168.4.1/");
    return httpd_resp_send(r, "", 0);
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (id == WIFI_EVENT_AP_START) ESP_LOGW(TAG, "wifi: access point started");
    else if (id == WIFI_EVENT_AP_STOP) ESP_LOGW(TAG, "wifi: access point stopped");
    else if (id == WIFI_EVENT_AP_STACONNECTED) { wifi_event_ap_staconnected_t *e = data; ESP_LOGW(TAG, "wifi: phone joined " MACSTR " aid %d", MAC2STR(e->mac), e->aid); g_joined = true; g_nsta++; touch(); }
    else if (id == WIFI_EVENT_AP_STADISCONNECTED) { wifi_event_ap_stadisconnected_t *e = data; ESP_LOGW(TAG, "wifi: phone left " MACSTR " reason %d", MAC2STR(e->mac), e->reason); if (g_nsta > 0) g_nsta--; touch(); }
}
#define CK(what, call) do { esp_err_t e_ = (call); if (e_ != ESP_OK) ESP_LOGE(TAG, "%s failed: %s", what, esp_err_to_name(e_)); } while (0)
void hal_start_sync_ap(void) {
    if (g_wifi_up) return;                       // already running: keep the password that is on the screen (a new one would not match the QR)
    load_pass(); g_saved = false; g_quiet_ms = 0; g_joined = false; g_nsta = 0; g_t0 = g_tact = ms_now();
    ESP_LOGW(TAG, "starting sync network, password %s", g_pass);
    esp_netif_init(); esp_event_loop_create_default();
    static bool hooked; if (!hooked) { CK("event handler", esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL)); hooked = true; }
    g_ap = esp_netif_create_default_wifi_ap();
    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    CK("wifi init", esp_wifi_init(&ic));
    esp_wifi_set_storage(WIFI_STORAGE_RAM);         // no Wi-Fi settings written to flash on every session
    wifi_config_t wc = { 0 };
    strcpy((char *)wc.ap.ssid, "SPENDBOX"); wc.ap.ssid_len = 8;
    strcpy((char *)wc.ap.password, g_pass);
    wc.ap.channel = 6; wc.ap.max_connection = 2; wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    CK("wifi mode", esp_wifi_set_mode(WIFI_MODE_AP));
    CK("wifi config", esp_wifi_set_config(WIFI_IF_AP, &wc));
    CK("wifi start", esp_wifi_start());
    esp_wifi_set_max_tx_power(40);                  // 10 dBm is plenty at arm's length and keeps the battery current spikes small
    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.stack_size = 8192; hc.lru_purge_enable = true; hc.max_uri_handlers = 12;
    esp_err_t he = httpd_start(&g_http, &hc); if (he != ESP_OK) ESP_LOGE(TAG, "web server failed: %s", esp_err_to_name(he));
    if (he == ESP_OK) {
        httpd_uri_t get = { .uri = "/", .method = HTTP_GET, .handler = page_upcoming };
        static const char *sp[5] = { "/s1", "/s2", "/s3", "/s4", "/s5" };
        for (int i = 0; i < 5; i++) { httpd_uri_t st = { .uri = sp[i], .method = HTTP_GET, .handler = page_step, .user_ctx = (void *)(intptr_t)(i + 1) }; httpd_register_uri_handler(g_http, &st); }
        httpd_uri_t led = { .uri = "/ledger", .method = HTTP_GET, .handler = page_ledger };
        httpd_uri_t lim = { .uri = "/limit", .method = HTTP_GET, .handler = page_limit };
        httpd_uri_t post = { .uri = "/save", .method = HTTP_POST, .handler = save_post };
        httpd_register_uri_handler(g_http, &get);
        httpd_register_uri_handler(g_http, &led);
        httpd_register_uri_handler(g_http, &lim);
        httpd_register_err_handler(g_http, HTTPD_404_NOT_FOUND, redirect_404);
        httpd_register_uri_handler(g_http, &post);
    }
    dns_start();
    g_wifi_up = true;
    ESP_LOGW(TAG, "sync AP up: SPENDBOX / %s", g_pass);
}

void hal_stop_sync_ap(void) {
    if (!g_wifi_up) return;
    dns_stop();
    if (g_http) { httpd_stop(g_http); g_http = NULL; }
    esp_wifi_stop(); esp_wifi_deinit();
    if (g_ap) { esp_netif_destroy_default_wifi(g_ap); g_ap = NULL; }
    g_wifi_up = false;
}
