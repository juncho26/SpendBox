// Append-only ledger that never "fills up": when the entry ring is full, the oldest
// entries are folded into per-month category totals (history keeps its totals, loses line items).
#ifndef LEDGER_H
#define LEDGER_H
#include "budget.h"
#include <stddef.h>

// Categories are editable on the website, so entries store a stable numeric id (never reused for another meaning).
typedef uint8_t Category;
#define MAX_CATS     16          // ids 0..14 are user categories (ever created), id 15 is the built-in mistake
#define CAT_MISTAKE  15
#define CAT_FOOD      0          // the first three ids are the defaults seeded by cats_default()
#define CAT_HOBBY     1
#define CAT_SCHOOL    2

#define LEDGER_CAP    2048   // 8 bytes each = 16 KB
#define LEDGER_MONTHS 36

typedef struct { uint16_t day; uint8_t cat; uint8_t pad; int32_t amount; } Entry;  // day = days since 2020-01-01
typedef struct { uint16_t ym; int32_t total[MAX_CATS]; } MonthSum;                // ym = (year-2020)*12 + month-1

typedef struct {
    Entry    e[LEDGER_CAP];
    uint16_t head, count;               // ring: oldest at head
    MonthSum m[LEDGER_MONTHS];
    uint8_t  nm;
} Ledger;

void    ledger_init(Ledger *l);
void    ledger_add(Ledger *l, Date d, Category c, int32_t amount);   // amount signed; MISTAKE is negative
int32_t ledger_day_total(const Ledger *l, Date d);                   // all categories incl. mistakes
int32_t ledger_month_total(const Ledger *l, int y, int m, Category c);  // entries + folded history
int32_t ledger_balance_delta_since(const Ledger *l, Date d);         // sum from `d` onward (for balance updates)
// Persistence: whole-image save/load with magic + CRC32. Returns bytes written / 0 on bad data.
size_t  ledger_save(const Ledger *l, uint8_t *buf, size_t cap);
bool    ledger_load(Ledger *l, const uint8_t *buf, size_t len);
#endif
