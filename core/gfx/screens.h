// Draws every device screen into a Fb. Pure logic, runs on a PC for visual checks.
#ifndef SCREENS_H
#define SCREENS_H
#include "gfx.h"
#include "../ui.h"
#include "../categories.h"

typedef struct {
    const char *date;          // "TUE 20 OCT"
    int         battery_pct;   // 0..100
    int32_t     spent;         // committed today + draft
    int32_t     draft;         // unsaved part (0 = none)
    int32_t     limit;         // today's fixed limit
    int32_t     remaining;     // limit - spent (may be negative)
    int         days_left;
    int32_t     min_food;
    bool        food_at_risk;  // limit < min_food: show the Food-at-risk screen
    int32_t     spent_committed;
    bool        on_card;       // food is covered by the credit card: show a small "ON CARD" note
    int32_t     credit;        // card credit at the start of today (0 = no card in use)
    int32_t     card_spent;    // part of today's saved entries that went on the card (cash-only categories excluded)
    uint8_t     pay;           // draft payment: 0 = category default, 1 = cash, 2 = card
    bool        clock_lost;    // the clock chip lost power: ask for the clock to be set from the phone
    bool        low_batt;      // sync was refused because the battery is too low
} HomeData;

void draw_home(Fb *f, const HomeData *h);
void draw_listening(Fb *f, int32_t draft, const CatTable *cats);
void draw_picker(Fb *f, int32_t draft, const CatTable *cats, int cursor);
void draw_sync(Fb *f, const char *ssid, const char *pass, const char *ip);

// Fits number text: largest of the NUM fonts whose width <= max_w.
const Font *pick_num_font(const char *text, int max_w, int max_px);
void draw_warning(Fb *f, const HomeData *h);   // Food at risk
#endif
