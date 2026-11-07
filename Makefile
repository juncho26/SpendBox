# Runs on a normal computer (no board needed):
#   make test      run the unit tests (budget maths, button gestures, UI logic, drawing)
#   make screens   draw every device screen into docs/images (needs Python + Pillow)
CC      ?= cc
CFLAGS   = -Wall -Wextra -O1 -fsanitize=address,undefined -Icore -Icore/gfx
export ASAN_OPTIONS = detect_leaks=0
CORE     = core/budget.c core/ledger.c core/categories.c core/ui.c
GFX      = core/gfx/gfx.c core/gfx/screens.c core/gfx/display.c core/gfx/font_data.c core/gfx/third_party/qrcodegen.c
BUILD    = build-host

test:
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $(BUILD)/test_budget core/budget.c core/tests/test_budget.c && $(BUILD)/test_budget
	$(CC) $(CFLAGS) -o $(BUILD)/test_ui $(CORE) core/tests/test_ui.c && $(BUILD)/test_ui
	$(CC) $(CFLAGS) -o $(BUILD)/test_button core/button.c core/tests/test_button.c && $(BUILD)/test_button
	$(CC) $(CFLAGS) -o $(BUILD)/test_gfx core/budget.c core/categories.c core/ledger.c $(GFX) core/gfx/test_gfx.c && $(BUILD)/test_gfx

screens:
	@mkdir -p $(BUILD) docs/images
	$(CC) $(CFLAGS) -o $(BUILD)/render_demo core/budget.c core/categories.c core/ledger.c $(filter-out core/gfx/display.c,$(GFX)) core/gfx/render_demo.c && $(BUILD)/render_demo $(BUILD)
	python3 tools/topng.py $(BUILD) docs/images

clean:
	rm -rf $(BUILD)
.PHONY: test screens clean
