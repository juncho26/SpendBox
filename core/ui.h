// Button/voice state machine. Pure logic: the firmware feeds events in and draws from the state.
#ifndef UI_H
#define UI_H
#include "categories.h"

typedef enum { SCR_HOME, SCR_LISTENING, SCR_PICKER, SCR_SYNC } Screen;
typedef enum {
    EV_UP, EV_DOWN,
    EV_BOTH_TAP,         // both buttons pressed and released quickly (before the 1 s hold)
    EV_BOTH_HOLD,        // both buttons ~1 s
    EV_BOTH_HOLD_LONG,   // both buttons 3 s
    EV_BOTH_RELEASE,     // a chord that had reached the 1 s hold was let go: the utterance is over
    EV_VOICE,            // recognized a category word: ev.cat
    EV_VOICE_FAIL,       // nothing recognized
    EV_TIMEOUT           // listening or sync timed out
} EventType;
typedef struct { EventType type; Category cat; uint8_t mult; } Event;   // mult: step multiplier for auto-repeat presses (0 = 1)

typedef struct {
    Screen  screen;
    int32_t draft;       // unsaved amount; 0 = none (no prime). Negative = correction
    int32_t step;        // amount per press, set on the website
    uint8_t cursor;      // picker row: FOOD/HOBBY/SCHOOL
    uint8_t pay;         // 1 = the draft was switched to the other payment with a both-button tap (tap again: back to normal)
    bool    credit_ok;   // set by the firmware: the card still has credit, so the normal choice is card and the other choice is cash
} Ui;

typedef struct { bool redraw; bool saved; Category cat; int32_t amount; uint8_t pay; } UiResult;   // pay: 1 cash, 2 card (what was saved)

void     ui_init(Ui *u, int32_t step);
UiResult ui_handle(Ui *u, Event ev, Ledger *l, Date today, const CatTable *cats);
// Saving rule: MISTAKE always stores a negative amount; a negative draft is always a MISTAKE.
#endif
