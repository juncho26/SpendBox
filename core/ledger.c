#include "ledger.h"
#include <string.h>

static uint16_t day_index(Date d) { return (uint16_t)date_days_between((Date){2020,1,1}, d); }
static uint16_t ym_of(Date d)     { return (uint16_t)((d.y - 2020) * 12 + d.m - 1); }

void ledger_init(Ledger *l) { memset(l, 0, sizeof *l); }

static MonthSum *month_slot(Ledger *l, uint16_t ym) {
    for (int i = 0; i < l->nm; i++) if (l->m[i].ym == ym) return &l->m[i];
    if (l->nm == LEDGER_MONTHS) {                       // drop the oldest month, keep newest 36
        memmove(&l->m[0], &l->m[1], sizeof(MonthSum) * (LEDGER_MONTHS - 1));
        l->nm--;
    }
    MonthSum *s = &l->m[l->nm++];
    memset(s, 0, sizeof *s); s->ym = ym;
    return s;
}

void ledger_add(Ledger *l, Date d, Category c, int32_t amount) {
    if (l->count == LEDGER_CAP) {                       // fold the oldest entry into its month total
        Entry *o = &l->e[l->head];
        Date od = date_add_days((Date){2020,1,1}, o->day);
        month_slot(l, ym_of(od))->total[o->cat] += o->amount;
        l->head = (l->head + 1) % LEDGER_CAP; l->count--;
    }
    Entry *n = &l->e[(l->head + l->count) % LEDGER_CAP];
    n->day = day_index(d); n->cat = (uint8_t)c; n->pad = 0; n->amount = amount;
    l->count++;
}

int32_t ledger_day_total(const Ledger *l, Date d) {
    uint16_t di = day_index(d); int32_t t = 0;
    for (int i = l->count - 1; i >= 0; i--) {           // newest first; stop once older than the day
        const Entry *e = &l->e[(l->head + i) % LEDGER_CAP];
        if (e->day < di) break;
        if (e->day == di) t += e->amount;
    }
    return t;
}

int32_t ledger_month_total(const Ledger *l, int y, int m, Category c) {
    uint16_t ym = (uint16_t)((y - 2020) * 12 + m - 1); int32_t t = 0;
    for (int i = 0; i < l->nm; i++) if (l->m[i].ym == ym) t += l->m[i].total[c];
    for (int i = 0; i < l->count; i++) {
        const Entry *e = &l->e[(l->head + i) % LEDGER_CAP];
        Date ed = date_add_days((Date){2020,1,1}, e->day);
        if (ym_of(ed) == ym && e->cat == c) t += e->amount;
    }
    return t;
}

int32_t ledger_balance_delta_since(const Ledger *l, Date d) {
    uint16_t di = day_index(d); int32_t t = 0;
    for (int i = l->count - 1; i >= 0; i--) {
        const Entry *e = &l->e[(l->head + i) % LEDGER_CAP];
        if (e->day < di) break;
        t += e->amount;
    }
    return t;
}

static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1))); }
    return ~c;
}
#define MAGIC 0x53504E44u  /* "SPND" */
size_t ledger_save(const Ledger *l, uint8_t *buf, size_t cap) {
    size_t need = 8 + sizeof *l;
    if (cap < need) return 0;
    uint32_t magic = MAGIC;
    memcpy(buf, &magic, 4);
    memcpy(buf + 8, l, sizeof *l);
    uint32_t c = crc32(buf + 8, sizeof *l);
    memcpy(buf + 4, &c, 4);
    return need;
}
bool ledger_load(Ledger *l, const uint8_t *buf, size_t len) {
    if (len < 8 + sizeof *l) return false;
    uint32_t magic, c; memcpy(&magic, buf, 4); memcpy(&c, buf + 4, 4);
    if (magic != MAGIC || crc32(buf + 8, sizeof *l) != c) return false;
    memcpy(l, buf + 8, sizeof *l);
    if (l->count > LEDGER_CAP || l->head >= LEDGER_CAP || l->nm > LEDGER_MONTHS) { ledger_init(l); return false; }
    return true;
}
