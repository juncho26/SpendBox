#include "screens.h"
#include "../budget.h"
#include "third_party/qrcodegen.h"
#include <stdio.h>
#include <string.h>

#define M 10                       // side margin
#define CW (FB_W - 2 * M)          // content width 180

static int cap_h(const Font *ft) { return text_height_digits(ft); }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// The size depends only on how many characters the number has, never on which digits: it is measured with the widest digit
// of each font, so 1,000 and 1,100 (or 900 and 1,000) never change size and the number does not jump around.
static int worst_width(const Font *f, const char *text) {
    char widest = '0'; int best = 0;
    for (char d = '0'; d <= '9'; d++) { char t[2] = { d, 0 }; int w = text_width(f, t); if (w > best) { best = w; widest = d; } }
    char buf[24]; int n = 0;
    for (const char *p = text; *p && n < 23; p++) buf[n++] = (*p >= '0' && *p <= '9') ? widest : *p;
    buf[n] = 0;
    return text_width(f, buf);
}
const Font *pick_num_font(const char *text, int max_w, int max_px) {
    const Font *list[] = { &F_NUM103, &F_NUM90, &F_NUM78, &F_NUM63, &F_NUM52, &F_NUM44 };
    for (int i = 0; i < 6; i++) if (list[i]->px <= max_px && worst_width(list[i], text) <= max_w) return list[i];
    return &F_NUM44;
}

static void battery(Fb *f, int x, int y, int pct) {
    fb_frame(f, (Rect){ x, y, 17, 9 }, 1, INK_BLACK);
    fb_fill(f, (Rect){ x + 17, y + 3, 2, 3 }, INK_BLACK);
    int w = clampi(pct, 0, 100) * 13 / 100;
    fb_fill(f, (Rect){ x + 2, y + 2, w, 5 }, INK_BLACK);
}

// Progress bar with the remaining amount written inside it, XOR so it reads on both black and white parts.
static void bar(Fb *f, int y, int h, const Font *nf, int32_t spent, int32_t draft, int32_t limit, int32_t remaining) {
    Rect r = { M, y, CW, h };
    fb_frame(f, r, 2, INK_BLACK);
    int iw = CW - 4;
    int32_t a = spent, b = spent + draft;
    int wa, wb;
    if (limit <= 0 || remaining < 0) { wa = iw; wb = iw; }                      // overspent: full bar
    else {
        wa = (int)((int64_t)clampi(a, 0, limit) * iw / limit);
        wb = (int)((int64_t)clampi(b, 0, limit) * iw / limit);
    }
    int lo = wa < wb ? wa : wb, hi = wa < wb ? wb : wa;
    fb_fill(f, (Rect){ M + 2, y + 2, lo, h - 4 }, INK_BLACK);
    if (hi > lo) fb_hatch(f, (Rect){ M + 2 + lo, y + 2, hi - lo, h - 4 });
    char t[16]; fmt_bar(remaining, t, sizeof t);
    int tw = text_width(nf, t);
    int tx = M + CW - 2 - 6 - tw;                                               // text sits on its own white plate with a thin frame,
    Rect plate = { tx - 5, y + 4, tw + 10, h - 8 };                             // so the hatch or the black fill never touches the digits
    fb_fill(f, plate, INK_WHITE);
    fb_frame(f, plate, 1, INK_BLACK);
    fb_text(f, nf, tx, y + (h + cap_h(nf)) / 2, t, INK_BLACK);
}

static void amount_header(Fb *f, int32_t draft) {                  // "'850" at the top right of the list screens
    char t[16]; fmt_main(draft < 0 ? -draft : draft, t, sizeof t);
    int w = text_width(&F_HDR33, t), px = M + CW - w - 12;
    fb_text(f, &F_HDR33, px, M + cap_h(&F_HDR33), t, INK_BLACK);
}

// Small pictures instead of the words CASH / CARD (they read better at a glance on this screen).
// Card: 18x12 outline with a stripe. Cash: a 7x9 dollar sign. `ink` is the drawing colour, so they can sit on a black plate.
static void icon_card(Fb *f, int x, int y, Ink ink) {
    fb_frame(f, (Rect){ x, y, 18, 12 }, 1, ink);
    fb_fill(f, (Rect){ x + 1, y + 3, 16, 2 }, ink);
    fb_fill(f, (Rect){ x + 3, y + 8, 5, 1 }, ink);
}
static void icon_cash(Fb *f, int x, int y, Ink ink) {
    static const char *rows[9] = { "...#...", ".#####.", "##.#.##", "##.#...", ".#####.", "...#.##", "##.#.##", ".#####.", "...#..." };
    for (int r = 0; r < 9; r++) for (int c = 0; c < 7; c++) if (rows[r][c] == '#') fb_px(f, x + c, y + r, ink);
}

void draw_home(Fb *f, const HomeData *h) {
    if (h->food_at_risk) { draw_warning(f, h); return; }
    fb_clear(f);
    fb_text(f, &F_SMALL, M, 18, h->date, INK_BLACK);
    battery(f, M + CW - 19, 10, h->battery_pct);
    fb_text(f, &F_SMALL, M, 33, "SPENT TODAY", INK_BLACK);
    if (h->draft != 0) {                                            // inverted tag: +850 / -300
        char t[16], u[20]; fmt_main(h->draft < 0 ? -h->draft : h->draft, t, sizeof t);
        snprintf(u, sizeof u, "%c%s", h->draft < 0 ? '-' : '+', t);
        int w = text_width(&F_SMALL, u) + 10;
        fb_fill(f, (Rect){ M + CW - w, 24, w, 13 }, INK_BLACK);
        fb_text(f, &F_SMALL, M + CW - w + 5, 33, u, INK_WHITE);
        if (h->pay) {                                               // how this one is paid (both-button tap): outlined tag left of the amount
            int pw = 26; Rect pr = { M + CW - w - pw - 4, 24, pw, 13 };
            fb_fill(f, pr, INK_WHITE); fb_frame(f, pr, 1, INK_BLACK);
            if (h->pay == 1) icon_cash(f, pr.x + 9, 26, INK_BLACK); else icon_card(f, pr.x + 4, 25, INK_BLACK);
        }
    }
    const int ay = 40, ah = 73;                        // fixed number area: nothing below it ever moves
    char num[16]; fmt_main(h->spent, num, sizeof num);
    const Font *nf = pick_num_font(num, CW + 1, 103);               // size from the character count alone; the whole width is available
    int nxc = M + (CW - text_width(nf, num)) / 2;                   // centered, so the space is shared left and right
    fb_text(f, nf, nxc, ay + (ah + cap_h(nf)) / 2, num, INK_BLACK);
    bar(f, ay + ah + 10, 37, &F_BAR27, h->spent_committed, h->draft, h->limit, h->remaining);
    if (h->draft != 0) {
        fb_text(f, &F_SMALL, M, 190, "▲ ▼ SET", INK_BLACK);
        const char *r = "HOLD BOTH = NAME"; fb_text(f, &F_SMALL, M + CW - text_width(&F_SMALL, r), 190, r, INK_BLACK);
    } else {
        char t[24]; if (h->low_batt) snprintf(t, sizeof t, "CHARGE FIRST"); else if (h->clock_lost) snprintf(t, sizeof t, "SET CLOCK"); else snprintf(t, sizeof t, "%dd to pay", h->days_left);
        fb_text(f, &F_SMALL, M, 190, t, INK_BLACK);
        if (h->credit > 0) {                                        // card credit left now: what the day's spending has not used yet
            char k[16]; int32_t left = h->credit - h->card_spent - h->draft; fmt_bar(left < 0 ? 0 : left, k, sizeof k);
            if (left <= 0) {                                        // card used up: from here on it is cash
                Rect pl = { M + CW - 22, 178, 22, 15 }; fb_fill(f, pl, INK_BLACK); icon_cash(f, pl.x + 8, 181, INK_WHITE);
            } else {
                int tw = text_width(&F_SMALL, k), ix = M + CW - tw - 6 - 18;
                if (h->on_card) { Rect pl = { ix - 3, 178, 24, 16 }; fb_fill(f, pl, INK_BLACK); icon_card(f, ix, 180, INK_WHITE); }   // food is paid by the card: icon on a black plate
                else icon_card(f, ix, 180, INK_BLACK);
                fb_text(f, &F_SMALL, M + CW - tw, 190, k, INK_BLACK);
            }
        }
    }
}

void draw_warning(Fb *f, const HomeData *h) {
    fb_clear(f);
    fb_fill(f, (Rect){ 0, 0, FB_W, 25 }, INK_BLACK);
    fb_text(f, &F_SMALL, M, 17, "! FOOD AT RISK", INK_WHITE);
    fb_text(f, &F_SMALL, FB_W - M - text_width(&F_SMALL, h->date), 17, h->date, INK_WHITE);
    fb_text(f, &F_SMALL, M, 42, "FOOD PER DAY UNTIL PAYDAY", INK_BLACK);
    char num[16]; fmt_main(h->limit < 0 ? 0 : h->limit, num, sizeof num);
    const Font *nf = pick_num_font(num, CW, 90);
    fb_text(f, nf, M, 48 + (76 + cap_h(nf)) / 2, num, INK_BLACK);
    char t[48], m[16]; fmt_main(h->min_food, m, sizeof m);
    snprintf(t, sizeof t, "%d DAYS · MIN %s · SHORT %d", h->days_left, m, (int)(h->min_food - (h->limit < 0 ? 0 : h->limit)));
    fb_text(f, &F_SMALL, M, 136, t, INK_BLACK);
    bar(f, 146, 29, &F_BAR22, h->spent_committed, 0, h->limit, h->remaining);
    char s[32], sm[16]; fmt_main(h->spent_committed, sm, sizeof sm); snprintf(s, sizeof s, "SPENT %s", sm);
    fb_text(f, &F_SMALL, M, 190, s, INK_BLACK);
}

static const Font *row_font(int n) { int p = cats_row_font(n); return p >= 30 ? &F_ROW25 : p >= 26 ? &F_ROW22 : &F_ROW18; }
static int row_pad(int n, bool wide) { return n <= 3 ? (wide ? 7 : 8) : n == 4 ? (wide ? 5 : 5) : (wide ? 4 : 4); }

static void mic_icon(Fb *f, int x, int y) {                         // 24x30 microphone, drawn from rectangles
    fb_frame(f, (Rect){ x + 7, y, 10, 16 }, 2, INK_BLACK);
    fb_fill(f, (Rect){ x + 2, y + 12, 2, 6 }, INK_BLACK); fb_fill(f, (Rect){ x + 20, y + 12, 2, 6 }, INK_BLACK);
    fb_fill(f, (Rect){ x + 2, y + 18, 20, 2 }, INK_BLACK);
    fb_fill(f, (Rect){ x + 11, y + 20, 2, 7 }, INK_BLACK);
}

void draw_listening(Fb *f, int32_t draft, const CatTable *cats) {
    fb_clear(f);
    amount_header(f, draft);
    mic_icon(f, 17, 54);
    fb_disc(f, 29, 98, 8, INK_BLACK);                                // recording dot
    Category ids[MAX_CATS]; int n = cats_active(cats, ids, MAX_CATS);
    const Font *rf = row_font(n); int ch = cap_h(rf), pad = row_pad(n, true), rh = ch + 2 * pad + 2;
    int x = 67, w = FB_W - M - x, y = 52;
    for (int i = 0; i < n; i++) {
        fb_fill(f, (Rect){ x, y, w, 2 }, INK_BLACK);
        fb_text(f, rf, x, y + 2 + pad + ch, cats_label(cats, ids[i]), INK_BLACK);
        y += rh;
    }
    fb_fill(f, (Rect){ x, y, w, 2 }, INK_BLACK);
    fb_text(f, &F_SMALL, M, 190, "HOLD · LET GO WHEN DONE", INK_BLACK);
}

void draw_picker(Fb *f, int32_t draft, const CatTable *cats, int cursor) {
    fb_clear(f);
    fb_text(f, &F_SMALL, M, 18, "PICK ONE", INK_BLACK);
    amount_header(f, draft);
    Category ids[MAX_CATS]; int n = cats_active(cats, ids, MAX_CATS);
    const Font *rf = row_font(n); int ch = cap_h(rf), pad = row_pad(n, false), rh = ch + 2 * pad + 2;
    int y = 52;
    fb_fill(f, (Rect){ M, y, CW, 2 }, INK_BLACK);
    for (int i = 0; i < n; i++) {
        int top = y + 2;
        bool sel = i == cursor;
        if (sel) fb_fill(f, (Rect){ M, top, CW, rh - 2 }, INK_BLACK);
        fb_text(f, rf, M + 6, top + pad + ch, cats_label(cats, ids[i]), sel ? INK_WHITE : INK_BLACK);
        if (sel) fb_text(f, &F_SMALL, M + CW - 14, top + (rh - 2 + 7) / 2, "◄", INK_WHITE);
        fb_fill(f, (Rect){ M, top + rh - 2, CW, 2 }, INK_BLACK);
        y += rh;
    }
    const char *hint = "▲ ▼ MOVE · HOLD BOTH = SAVE";
    fb_text(f, &F_SMALL, (FB_W - text_width(&F_SMALL, hint)) / 2, 190, hint, INK_BLACK);
}

void draw_sync(Fb *f, const char *ssid, const char *pass, const char *ip) {
    fb_clear(f);
    fb_text(f, &F_SMALL, M, 18, "SYNC MODE · SCAN TO JOIN", INK_BLACK);
    char wifi[96]; snprintf(wifi, sizeof wifi, "WIFI:T:WPA;S:%s;P:%s;;", ssid, pass);
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
    if (qrcodegen_encodeText(wifi, tmp, qr, qrcodegen_Ecc_LOW, 1, 6, qrcodegen_Mask_AUTO, true)) {
        int n = qrcodegen_getSize(qr), sc = 116 / n; if (sc < 2) sc = 2;
        int x0 = (FB_W - n * sc) / 2, y0 = 28;
        for (int yy = 0; yy < n; yy++) for (int xx = 0; xx < n; xx++)
            if (qrcodegen_getModule(qr, xx, yy)) fb_fill(f, (Rect){ x0 + xx * sc, y0 + yy * sc, sc, sc }, INK_BLACK);
    }
    char t[40], u[48]; snprintf(t, sizeof t, "PASS %s · THEN OPEN", pass); fb_text(f, &F_SMALL, M, 162, t, INK_BLACK);
    snprintf(u, sizeof u, "%s · %s", ssid, ip); fb_text(f, &F_ROW18, M, 188, u, INK_BLACK);
}
