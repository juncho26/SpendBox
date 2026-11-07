# How it works

## The daily limit

SpendBox answers one question: **"What is the most I can spend today without running out of food money before payday?"**

```
limit = balance now (before today's spending)
        - bills due before payday
        + credit still free on the card
        - minimum food x (days left - 1)
```

- *Balance* is what you type in step 1. The device keeps it current by subtracting the purchases you log.
- *Bills* are the ones you list in step 2 with a due date before the next payday. Every bill is held back from the limit.
- *Days left* counts today up to the day before payday. Today's own food is part of what you spend, so only the **later** days are reserved.
- The limit is **fixed for the whole day**. What you spend today is subtracted from it, so the bar fills as you spend; it does not move the limit.
- If the result is below the minimum food for one day, the screen shows **FOOD AT RISK** and how much you are short.

Payday is a day of the month (clamped to short months), optionally moved to the next Monday when it falls on a weekend. On payday itself a new cycle starts.

### Credit card

- You enter the credit left on the card and the credit you get back after paying it.
- Each purchase is saved as **cash** or **card**. The default is card while credit is left and cash when it is used up; "Cash only" categories are always cash; a quick two-button tap switches one purchase.
- Card purchases lower the remaining credit (never below zero). A bill marked *pays my credit card* resets the credit on its due day to the "credit after paying" amount, so from the next day the limit rises by the credit that came back.
- If cash alone cannot cover today's minimum food but cash plus credit can, the screen shows the card as carrying the food.

## Data and storage

| What | Where | Size |
|---|---|---|
| Settings, bills, categories, Wi-Fi password | NVS (flash key-value store) | small, written only when you save the website |
| Purchases ("ledger") | a raw 128 KB flash partition used as a ring | 8 bytes per purchase: day, category, payment, amount; 16,384 purchases |
| Today's total, limit, days left | RTC memory (kept in deep sleep) | few bytes |

Saving a purchase appends 8 bytes. When a flash sector fills, the next one is erased and reused, which spreads wear evenly over the whole partition; at 10 purchases a day it takes about 4.5 years to go once around, and each sector would then be erased once. The oldest purchases are overwritten first.

A normal wake touches no flash: totals live in RTC memory and are recomputed only on a new day or after a sync.

## Display

The 200×200 e-paper panel keeps its picture with **no power**. The firmware:

- powers the panel only while updating and puts it to sleep afterwards,
- uses fast **partial refreshes** for number changes and re-loads the previous image into the controller first (its memory is lost when power is cut),
- does a **full refresh** on a new day, and a clean-up full refresh after 30 partial ones, done when you stop pressing, to clear ghosting,
- draws in a background task and merges quick presses so button reads are never lost.

The drawing code (`core/gfx`) is plain C with built-in bitmap fonts (Barlow Condensed and DejaVu Mono rasterized by `tools/genfont.py`), so you can render every screen on a PC (`make screens`).

## Buttons

`core/button.c` is a small state machine that turns raw pin levels into events: up, down, auto-repeat while held, **both pressed together**, a quick **both tap**, **both held 1 s**, **both held 3 s**. It is tested on a PC without hardware. Holding adds 5 steps at a time, and the number is never allowed to run more than one step ahead of what the screen shows, so it stops where you let go.

## Power

- Deep sleep between presses; either button wakes the board.
- The CPU runs at 80 MHz (40 MHz idle with light sleep while awake).
- Wi-Fi exists only in sync mode, at reduced transmit power, with a password generated once per device and stored in flash. Sync closes when you save, when idle, after 10 minutes at most, or when you hold both buttons.
- The battery is measured once per wake. Below about 3.2 V sync is refused.
- The panel, audio rail and Wi-Fi are all off in sleep. Real battery life has not been measured yet; a USB power meter on the battery lead is the way to check it.

## Sync website

A tiny web server (`firmware/main/hal_sync.c`) serves plain HTML pages with a little JavaScript. A captive-portal DNS answers every name with 192.168.4.1 so phones open the page by themselves. It is intended for short sessions on a private hotspot, protected by WPA2, limited to two connections.
