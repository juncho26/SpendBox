// SPENDBOX budget engine. Pure C, no heap, no I/O: runs on the ESP32-S3 and on a PC for tests.
#ifndef BUDGET_H
#define BUDGET_H
#include <stdint.h>
#include <stdbool.h>

typedef struct { int16_t y; uint8_t m, d; } Date;

int  date_cmp(Date a, Date b);
int  date_days_between(Date from, Date to);   // to - from, in days
int  date_weekday(Date d);                    // 0=Sun .. 6=Sat
Date date_add_days(Date d, int n);
int  days_in_month(int y, int m);

// Payday: day-of-month (clamped to month length); weekend -> next Monday if enabled.
Date payday_in_month(int y, int m, int day, bool delay_weekend);
// First payday strictly after `today` (on payday itself a new cycle begins).
Date payday_next(Date today, int day, bool delay_weekend);

typedef struct {
    Date    due;
    int32_t amount;     // bills and savings, positive
    bool    is_default; // true = estimate (shown with ~ on the website)
    bool    counts;     // false for savings with "hold back" turned off
    bool    card;       // pays the credit card: once paid, the card can be used again
} Bill;

typedef struct {
    int32_t balance;
    int32_t min_food;
    int     payday_day;
    bool    delay_weekend;
    int32_t credit;     // credit still free on the card today (typed on the website, plus card bills paid since)
} Settings;

typedef struct {
    Date    payday;
    int     days_left;      // today .. day before payday
    int32_t committed;      // bills + held savings due before payday
    int32_t available;      // balance - committed
    int32_t limit;          // per-day limit (floor division, never above available/days)
    bool    food_ok;        // limit >= min_food
    bool    uses_default;   // any counted bill is an estimate
    bool    on_card;        // cash alone would not cover the minimum food, the card does: limit is lifted to the minimum food
    int32_t credit, min_food;   // copied from Settings so budget_tomorrow_limit can apply the same rule
} Plan;

Plan budget_plan(const Settings *s, const Bill *bills, int n, Date today);
int32_t budget_remaining(int32_t limit, int32_t spent_today);
// Tomorrow's limit if today's spending ends at `spent_today` (limit is fixed at first wake).
int32_t budget_tomorrow_limit(const Plan *p, int32_t spent_today);
// Default for an unconfirmed bill: average of the last up-to-3 months (0 if none).
int32_t default_from_history(const int32_t *last_months, int count);

// Display. Main number: full digits with commas. Bar number: <1000 as is, else 1.25k / 100k / 1.5M.
int fmt_main(int32_t v, char *out, int cap);
int fmt_bar(int32_t v, char *out, int cap);
#endif
