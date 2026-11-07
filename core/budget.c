#include "budget.h"
#include <stdio.h>

static int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int yoe = (int)(y - era * 400);
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static Date civil_from_days(int64_t z) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = (int)(z - era * 146097);
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    int d = doy - (153 * mp + 2) / 5 + 1;
    int m = mp + (mp < 10 ? 3 : -9);
    return (Date){ (int16_t)(y + (m <= 2)), (uint8_t)m, (uint8_t)d };
}

int date_cmp(Date a, Date b) {
    if (a.y != b.y) return a.y < b.y ? -1 : 1;
    if (a.m != b.m) return a.m < b.m ? -1 : 1;
    if (a.d != b.d) return a.d < b.d ? -1 : 1;
    return 0;
}
int date_days_between(Date f, Date t) { return (int)(days_from_civil(t.y, t.m, t.d) - days_from_civil(f.y, f.m, f.d)); }
int date_weekday(Date d) { int64_t z = days_from_civil(d.y, d.m, d.d); return (int)(((z % 7) + 11) % 7); }
Date date_add_days(Date d, int n) { return civil_from_days(days_from_civil(d.y, d.m, d.d) + n); }
int days_in_month(int y, int m) {
    static const int t[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return t[m - 1];
}

Date payday_in_month(int y, int m, int day, bool delay) {
    int dim = days_in_month(y, m);
    if (day > dim) day = dim;
    if (day < 1) day = 1;
    Date p = { (int16_t)y, (uint8_t)m, (uint8_t)day };
    if (delay) {
        int w = date_weekday(p);
        if (w == 6) p = date_add_days(p, 2);
        else if (w == 0) p = date_add_days(p, 1);
    }
    return p;
}
Date payday_next(Date today, int day, bool delay) {
    int y = today.y, m = today.m;
    // Start from last month: a weekend-delayed payday can land in the next month.
    for (int i = 0; i < 4; i++) {
        int mm = m - 1 + i, yy = y + mm / 12; mm = mm % 12 + 1;
        if (m - 1 + i < 0) { yy = y - 1; mm = 12; }
        Date p = payday_in_month(yy, mm, day, delay);
        if (date_cmp(p, today) > 0) return p;
    }
    return payday_in_month(y + 1, m, day, delay);
}

Plan budget_plan(const Settings *s, const Bill *bills, int n, Date today) {
    Plan p = {0};
    p.payday = payday_next(today, s->payday_day, s->delay_weekend);
    p.days_left = date_days_between(today, p.payday);
    for (int i = 0; i < n; i++) {
        const Bill *b = &bills[i];
        if (!b->counts) continue;
        if (date_cmp(b->due, today) >= 0 && date_cmp(b->due, p.payday) < 0) {
            p.committed += b->amount;
            if (b->is_default) p.uses_default = true;
        }
    }
    p.available = s->balance - p.committed;
    int d = p.days_left > 0 ? p.days_left : 1;
    // The limit is the most that can be spent today (cash + free card credit) while every remaining day after today
    // can still afford the minimum food.
    p.credit = s->credit > 0 ? s->credit : 0; p.min_food = s->min_food;
    int32_t cash_limit = p.available - s->min_food * (d - 1);
    p.limit = cash_limit + p.credit;
    p.food_ok = p.limit >= s->min_food;
    p.on_card = p.credit > 0 && cash_limit < s->min_food && p.food_ok;   // cash alone is short of today's food, the card carries it
    return p;
}
int32_t budget_remaining(int32_t limit, int32_t spent) { return limit - spent; }
int32_t budget_tomorrow_limit(const Plan *p, int32_t spent) {
    int d = p->days_left - 1;                              // days left after today
    int32_t left = p->available - spent + p->credit;
    if (d < 1) return left;
    return left - p->min_food * (d - 1);
}
int32_t default_from_history(const int32_t *h, int n) {
    if (n <= 0) return 0;
    if (n > 3) { h += n - 3; n = 3; }
    int64_t sum = 0;
    for (int i = 0; i < n; i++) sum += h[i];
    return (int32_t)((sum + n / 2) / n);
}

int fmt_main(int32_t v, char *out, int cap) {
    char tmp[16]; int64_t a = v < 0 ? -(int64_t)v : v;
    int n = snprintf(tmp, sizeof tmp, "%lld", (long long)a);
    char buf[24]; int k = 0;
    if (v < 0) buf[k++] = '-';
    for (int i = 0; i < n; i++) { if (i && (n - i) % 3 == 0) buf[k++] = ','; buf[k++] = tmp[i]; }
    buf[k] = 0;
    return snprintf(out, cap, "%s", buf);
}
int fmt_bar(int32_t v, char *out, int cap) {
    int64_t a = v < 0 ? -(int64_t)v : v;
    const char *sg = v < 0 ? "-" : "";
    if (a < 1000) return snprintf(out, cap, "%s%lld", sg, (long long)a);
    int64_t unit = a >= 1000000 ? 1000000 : 1000; char suf = unit == 1000 ? 'k' : 'M';
    int64_t whole = a / unit, frac = (a % unit) * 100 / unit;   // truncate to 2 decimals
    if (frac == 0) return snprintf(out, cap, "%s%lld%c", sg, (long long)whole, suf);
    if (frac % 10 == 0) return snprintf(out, cap, "%s%lld.%lld%c", sg, (long long)whole, (long long)(frac / 10), suf);
    return snprintf(out, cap, "%s%lld.%02lld%c", sg, (long long)whole, (long long)frac, suf);
}
