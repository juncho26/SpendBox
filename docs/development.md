# Development notes

## Layout

```
core/           no hardware access; compiles for the ESP32-S3 and for a PC
  budget.*      dates, payday, the daily-limit plan
  ledger.*      purchase record format and totals
  categories.*  editable category table
  button.*      two-button gesture state machine
  ui.*          screen state machine (home / category list / sync)
  gfx/          1-bit drawing code, fonts, screens, QR code
  tests/        unit tests
firmware/       ESP-IDF project
  main/main.c        wake, input loop, redraw task, sleep
  main/hal_epd.c     e-paper driver (SSD1681-style controller)
  main/hal_store.c   NVS settings + ledger ring in a flash partition
  main/hal_sync.c    Wi-Fi hotspot, captive portal, website
  main/hal_misc.c    real-time clock and battery reading
  main/hal_voice.c   empty placeholder for voice input
  main/pins.h        pin numbers of the V2 board
```

The `hal_*` files are the only code that touches hardware. To port to another board, change `pins.h` and `hal_epd.c`.

## Tests

```
make test
```

Builds with `-fsanitize=address,undefined` and runs 199 checks. Please add a test for any change in `core/`.

## Changing the screens

Edit `core/gfx/screens.c`, then `make screens` and look at `docs/images/screen-*.png`. Fonts come from `tools/genfont.py` (needs Python and Pillow).

## Ideas that are not built yet

- **Voice input** for the category: `hal_voice.c` is a stub. Espressif's ESP-SR offline command recognition would be the natural fit; check its language support first.
- A page listing **all** purchases (the Ledger tab shows the newest 12) and a monthly history.
- CSV export of the ledger.
- Detecting USB power to allow sync on a nearly empty battery.
