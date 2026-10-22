# The simulator and the tests, built for the host as 32-bit with the panel's LVGL heap size.
#   make check          run every test, SAN=1 adds ASan and UBSan
#   make shots          every screen as build/out/shots/*.png, plus a contact sheet
#   make gallery        the README's screens and a page of all of them in build/out/gallery, after make check
#   make met            the ESP's weather picture for a set of weathers, in build/out/met
#   make esp_sync       copy proto/ into the ESP repo, which builds the same files
ROOT     := ..
BUILD    := build$(if $(SAN),-san)
AUTHORS  := MSkjel
.DEFAULT_GOAL := check
include $(ROOT)/config.mk
OUT      := $(BUILD)/out
ESP_REPO ?= $(abspath $(ROOT)/../esphome-flexit-modbus-server)
ESP_CODE := $(ESP_REPO)/packages/code
CC       := gcc -m32 -msse2 -mfpmath=sse  # SSE floats round the same everywhere, which the JPEGs in the goldens need
CXX      := g++ -m32 -msse2 -mfpmath=sse
ifdef SAN
SANFLAGS := -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
endif
CPPFLAGS := -MMD -MP -DCI600_SIM -I$(ROOT)/sim -I$(ROOT)/hw $(INCLUDES) $(DEFS)
CFLAGS   := -O1 -g -std=gnu11 $(WARN) $(SANFLAGS)
CXXFLAGS := -O1 -g -std=gnu++17 $(WARN) $(SANFLAGS)
LDLIBS   := $(SANFLAGS) -lm

obj = $(patsubst %,$(BUILD)/obj/%.o,$(1))
APP := $(patsubst $(ROOT)/%.c,$(BUILD)/obj/%.o,$(APP_SRC) $(LV_SRC)) $(LV_COPY_SRC:.c=.o)
SIMLIB := $(call obj,sim/simlib sim/ppm)

$(BUILD)/sim: $(APP) $(SIMLIB) $(call obj,sim/sim sim/sim_model sim/sim_esp)
$(BUILD)/lang_test: $(APP) $(SIMLIB) $(call obj,sim/lang_test sim/sim_model sim/sim_esp)
$(BUILD)/replay: $(APP) $(SIMLIB) $(call obj,sim/replay sim/board_stub hw/mbserver hw/unit hw/room)
$(BUILD)/unit_test: $(APP) $(SIMLIB) $(call obj,sim/unit_test sim/board_stub hw/mbserver hw/unit hw/room hw/settings)
# unit_test records the calls input.c, link.c and the week program make into the UI
$(BUILD)/unit_test: LDLIBS += -Wl,--wrap=ui_key,--wrap=ui_idle,--wrap=ui_wake,--wrap=ui_toast,--wrap=ui_layout_changed,--wrap=ui_show_view
$(BUILD)/link_test: $(call obj,sim/link_test proto/pbulk proto/pbus proto/plink proto/pdeflate)
$(BUILD)/bulk_test: $(call obj,sim/bulk_test proto/pbulk proto/pbus proto/plink proto/pdeflate)
$(BUILD)/sim $(BUILD)/lang_test $(BUILD)/unit_test $(BUILD)/replay $(BUILD)/link_test $(BUILD)/bulk_test:
	$(CC) -o $@ $^ $(LDLIBS)

# esp_test runs the ESP's panel packages, YAML lambdas and all, against the panel's link code on a fake CU60
ESP_YAML := $(wildcard $(ESP_REPO)/packages/panel.yaml $(ESP_REPO)/packages/panel/*.yaml)
# gen.py also checks the ESP's copies of proto/
ESP_PROTO := $(wildcard $(ROOT)/proto/*.[ch] $(ESP_CODE)/panel/*)
$(BUILD)/esp_yaml/gen_panel.cpp: esp_yaml/gen.py $(ESP_YAML) $(ESP_PROTO)
	@test -d $(ESP_REPO) || { echo "needs the ESP repo: ESP_REPO=<esphome-flexit-modbus-server>"; exit 1; }
	@mkdir -p $(dir $@)
	python3 esp_yaml/gen.py $(ESP_REPO) $@
$(BUILD)/obj/sim/esp_yaml/harness.o: $(BUILD)/esp_yaml/gen_panel.cpp
$(BUILD)/obj/sim/esp_yaml/harness.o: CPPFLAGS += -I$(BUILD)/esp_yaml -Iesp_yaml -DMET_SAMPLE='"$(abspath esp_yaml/met_sample.json)"'
$(BUILD)/esp_test: $(APP) $(SIMLIB) $(call obj,sim/sim_model sim/esp_yaml/panel_side sim/esp_yaml/harness)
	$(CXX) -o $@ $^ $(LDLIBS)

# met_render draws the ESP's weather picture from packages/code/panel/met_scene.h for a set of weathers and times
$(BUILD)/obj/sim/met_render.o: CPPFLAGS += -I$(ESP_CODE)/panel
$(BUILD)/met_render: $(call obj,sim/met_render proto/plink proto/pdeflate sim/ppm)
	$(CXX) -o $@ $^ $(LDLIBS)

$(BUILD)/obj/%.o: $(ROOT)/%.c $(ROOT)/config.mk $(FW_STAMP) | $(LVGL_PATCHED)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<
$(BUILD)/obj/%.o: $(ROOT)/%.cpp $(ROOT)/config.mk $(FW_STAMP)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<
$(BUILD)/obj/lib/%.o: CFLAGS += -w
$(BUILD)/obj/ui/ui_info.o: $(AUTHORS_H)
# the patched LVGL copies find their relative includes next to the originals
$(LV_COPY)/%.o: $(LV_COPY)/%.c $(ROOT)/config.mk $(FW_STAMP)
	$(CC) $(CPPFLAGS) $(CFLAGS) -w -I$(LVGL)/$(dir $*) -c -o $@ $<

# Every screen the tests draw. Their hashes are in golden.txt.
SCREENS := shots/*.ppm shots_no/*.ppm shots*/checks/*.ppm esp/*.ppm replay/*.ppm
# hashed and sorted by name in C order, since wildcards sort by the locale and CI has another one
HASH_SCREENS := sha256sum $(SCREENS) | LC_ALL=C sort -k 2

check: $(BUILD)/sim $(BUILD)/lang_test $(BUILD)/unit_test $(BUILD)/replay $(BUILD)/link_test $(BUILD)/bulk_test $(BUILD)/esp_test lang
	rm -rf $(OUT)/shots $(OUT)/shots_no $(OUT)/esp $(OUT)/replay
	$(BUILD)/unit_test
	$(BUILD)/link_test
	$(BUILD)/bulk_test 120
	$(BUILD)/bulk_test 60 4 7 114 1
	$(BUILD)/bulk_test 60 2 3 1 0
	$(BUILD)/esp_test $(OUT)/esp
	$(BUILD)/replay captures/cap1.txt $(OUT)/replay
	$(BUILD)/replay captures/cap1.txt $(OUT)/replay glued
	$(BUILD)/sim $(OUT)/shots
	SIM_LANG=1 $(BUILD)/sim $(OUT)/shots_no
	$(BUILD)/lang_test
	@cd $(OUT) && $(HASH_SCREENS) > screens.txt
	@diff $(CURDIR)/golden.txt $(OUT)/screens.txt | sed -n 's/^> [0-9a-f]*  /changed: /p'
	@cmp -s $(CURDIR)/golden.txt $(OUT)/screens.txt \
	  || { echo "screens changed: make changed shows them, make golden takes them"; exit 1; }
	@echo "all tests passed"

# changed makes a contact sheet of the screens that differ from golden.txt. golden takes the screens as they are.
changed:
	@rm -rf $(OUT)/changed && mkdir -p $(OUT)/changed
	@cd $(OUT) && $(HASH_SCREENS) | diff $(CURDIR)/golden.txt - | sed -n 's/^> [0-9a-f]*  //p' \
	  | while read f; do cp $$f changed/$$(echo $$f | tr / _); done
	python3 shots.py $(OUT)/changed

golden:
	cd $(OUT) && $(HASH_SCREENS) > $(CURDIR)/golden.txt

# lang compiles ui/*.c with the English text in place of each _(S_X) so the compiler checks every format
# against its arguments, then checks that every string id is used somewhere.
$(BUILD)/lang_en.h: $(ROOT)/ui/lang.h
	@mkdir -p $(dir $@)
	printf '#include "lang.h"\n#define X(id, en) EN(id) en\nSTRINGS(X)\n' | $(CC) -E -P -I$(ROOT)/ui -x c - \
	  | sed 's/EN(\([A-Z0-9_]*\))/\n#define EN_S_\1/g' | grep '^#define EN_S_' > $@

lang: $(BUILD)/lang_en.h $(AUTHORS_H) | $(LVGL_PATCHED)
	$(CC) $(filter-out -MMD -MP,$(CPPFLAGS)) $(CFLAGS) -fsyntax-only -DCI600_FMTCHECK -include $< -Werror=format $(wildcard $(ROOT)/ui/*.c)
	@for s in $$(sed -n 's/^ *X(\([A-Z0-9_]*\),.*/\1/p' $(ROOT)/ui/lang.h); do \
	  grep -rqw --include='*.c' --include='*.h' --exclude='lang*' "S_$$s" $(ROOT)/ui $(ROOT)/app $(ROOT)/hw \
	    || grep -qw "$$s" $(ROOT)/app/equipment.h || { echo "S_$$s isn't used"; exit 1; }; \
	done

shots: $(BUILD)/sim
	$(BUILD)/sim $(OUT)/shots
	python3 shots.py $(OUT)/shots

gallery: met
	rm -rf $(OUT)/gallery
	python3 gallery.py $(OUT) $(OUT)/gallery

met: $(BUILD)/met_render
	$(BUILD)/met_render $(OUT)/met esp_yaml/met_sample.json
	python3 shots.py --scale 1 --cols 6 $(OUT)/met

esp_sync:
	cp $(wildcard $(ROOT)/proto/*.[ch]) $(ESP_CODE)/panel/

clean:
	rm -rf build build-san

.PHONY: lang check changed golden shots gallery met esp_sync clean
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
