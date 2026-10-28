# The simulator and the tests, built for the host as 32-bit with the panel's LVGL heap size.
#   make check          run every test, SAN=1 adds ASan and UBSan
#   make changed        the screens that differ from golden.txt next to the old ones, make golden takes them
#   build/unit_test X   only test X, or those with X in their name. -l lists them. Same for link_test, fuzz_test
#   make shots          every screen as build/out/shots/*.png, plus a contact sheet
#   make gallery        the README's screens and a page of all of them in build/out/gallery, after make check
#   make met            the ESP's weather picture for a set of weathers, in build/out/met
#   make esp_sync       copy proto/ into the ESP repo, which builds the same files. FORCE=1 drops changes there
ROOT     := ..
BUILD    := build$(if $(SAN),-san)
OBJ_DIR  := $(BUILD)/obj
COMPILE   = $(CC) $(CPPFLAGS) $(CFLAGS)
AUTHORS  := MSkjel
ESP_AUTHORS := MSkjel peetervalk bloever schmutzgeier
.DEFAULT_GOAL := check
include $(ROOT)/config.mk
OUT      := $(BUILD)/out
ESP_CODE := $(ESP_REPO)/packages/code
ESP_MET  := $(ESP_CODE)/met
CC       := gcc -m32 -msse2 -mfpmath=sse  # SSE floats round the same everywhere, which the JPEGs in the goldens need
CXX      := g++ -m32 -msse2 -mfpmath=sse
ifdef SAN
SANFLAGS := -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
endif
CPPFLAGS := -MMD -MP -DCI600_SIM -I$(ROOT)/sim -I$(ROOT)/hw $(INCLUDES) $(DEFS)
CFLAGS   := -O1 -g -std=gnu11 $(WARN) $(SANFLAGS)
CXXFLAGS := -O1 -g -std=gnu++17 $(WARN) $(SANFLAGS)
LDLIBS   := $(SANFLAGS) -lm

obj = $(patsubst %,$(OBJ_DIR)/%.o,$(1))
SIMLIB   := $(APP_OBJ) $(call obj,sim/simlib sim/ppm)
HW_OBJ   := $(call obj,sim/board_stub hw/bus hw/mbserver hw/unit hw/room hw/settings)  # hw/ on a fake board
LINK_OBJ := $(call obj,proto/pbulk proto/pbus proto/plink proto/pdeflate)
CHECK_OBJ := $(call obj,sim/check)

PROGS := sim lang_test unit_test replay link_test bulk_test fuzz_test
$(BUILD)/sim: $(SIMLIB) $(call obj,sim/sim sim/sim_model sim/sim_esp)
$(BUILD)/lang_test: $(SIMLIB) $(call obj,sim/lang_test sim/sim_model sim/sim_esp)
UNIT_OBJ := $(patsubst $(ROOT)/%.c,$(OBJ_DIR)/%.o,$(sort $(wildcard $(ROOT)/sim/unit_test/*.c)))
$(BUILD)/unit_test: $(SIMLIB) $(HW_OBJ) $(CHECK_OBJ) $(UNIT_OBJ)
$(BUILD)/replay: $(SIMLIB) $(HW_OBJ) $(call obj,sim/replay)
# unit_test records the calls input.c, link.c and the week program make into the UI
$(BUILD)/unit_test: LDLIBS += -Wl,--wrap=ui_key,--wrap=ui_idle,--wrap=ui_wake,--wrap=ui_toast,--wrap=ui_key_lock_notice,--wrap=ui_layout_changed,--wrap=ui_show_view,--wrap=ui_value_changed
$(BUILD)/link_test: $(LINK_OBJ) $(CHECK_OBJ) $(call obj,sim/link_test)
$(BUILD)/fuzz_test: $(SIMLIB) $(CHECK_OBJ) $(call obj,sim/fuzz_test sim/sim_model sim/sim_esp)
# bulk_test has its own link.c and Modbus server
$(BUILD)/bulk_test: $(LINK_OBJ) $(CHECK_OBJ) $(call obj,sim/bulk_test hw/bus)
$(addprefix $(BUILD)/,$(PROGS)):
	$(CC) -o $@ $^ $(LDLIBS)

# packages/code/met/*.cpp, built apart from main.cpp as on the ESP
MET_OBJ := $(patsubst $(ESP_MET)/%.cpp,$(OBJ_DIR)/esp_met/%.o,$(wildcard $(ESP_MET)/*.cpp))
$(OBJ_DIR)/esp_met/%.o: $(ESP_MET)/%.cpp $(BUILD_MK)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

# packages/code/panel/*.cpp, built apart from main.cpp as on the ESP. Not update.cpp, it needs ESP-IDF and the ESP
# repo's update_test.cpp runs it
PANEL_CPP := $(filter-out %/update.cpp,$(wildcard $(ESP_CODE)/panel/*.cpp))
PANEL_OBJ := $(patsubst $(ESP_CODE)/panel/%.cpp,$(OBJ_DIR)/esp_panel/%.o,$(PANEL_CPP))
$(OBJ_DIR)/esp_panel/%.o: $(ESP_CODE)/panel/%.cpp $(BUILD_MK)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

# esp_test runs the ESP's panel packages, YAML lambdas and all, against the panel's link code on a fake CX60
ESP_YAML := $(wildcard $(ESP_REPO)/packages/panel.yaml $(ESP_REPO)/packages/panel/*.yaml)
# gen.py also checks the ESP's copies of proto/ against proto.lock, and lists the headers in packages/code
ESP_PROTO := $(wildcard $(ROOT)/proto/*.[ch] $(ESP_CODE)/panel/* $(ESP_MET)/*)
$(BUILD)/esp_yaml/gen_panel.cpp: esp_yaml/gen.py esp_yaml/esp_sync.py $(ESP_YAML) $(ESP_PROTO)
	@test -d $(ESP_REPO) || { echo "needs the ESP repo: ESP_REPO=<esphome-flexit-modbus-server>"; exit 1; }
	@mkdir -p $(dir $@)
	python3 esp_yaml/gen.py $(ESP_REPO) $@
$(OBJ_DIR)/sim/esp_yaml/harness.o: $(BUILD)/esp_yaml/gen_panel.cpp
$(OBJ_DIR)/sim/esp_yaml/harness.o: CPPFLAGS += -I$(BUILD)/esp_yaml -Iesp_yaml -DMET_SAMPLE='"$(abspath esp_yaml/met_sample.json)"'
$(BUILD)/esp_test: $(SIMLIB) $(CHECK_OBJ) $(call obj,sim/sim_model sim/esp_yaml/panel_side sim/esp_yaml/harness) \
                   $(PANEL_OBJ) $(MET_OBJ)
	$(CXX) -o $@ $^ $(LDLIBS)

# met_render draws the ESP's weather picture from packages/code/met/met_scene.h for a set of weathers and times
$(OBJ_DIR)/sim/met_render.o: CPPFLAGS += -I$(ESP_MET)
$(BUILD)/met_render: $(call obj,sim/met_render proto/plink proto/pdeflate sim/ppm) $(MET_OBJ)
	$(CXX) -o $@ $^ $(LDLIBS)

$(OBJ_DIR)/%.o: $(ROOT)/%.cpp $(BUILD_MK)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<
# LVGL's warnings aren't ours
$(OBJ_DIR)/lib/%.o $(LV_COPY)/%.o: CFLAGS += -w
# TJpgDec's IDCT overflows on fuzz_test's damaged JPEGs. BYTECLIP() saturates it, so only the pixels are wrong
$(OBJ_DIR)/lib/tjpgd/%.o: CFLAGS += -fno-sanitize=signed-integer-overflow

# Every screen the tests draw. Their hashes are in golden.txt.
SCREENS := shots/*.ppm shots_no/*.ppm shots*/checks/*.ppm esp/*.ppm replay/*.ppm
# hashed and sorted by name in C order, since wildcards sort by the locale and CI has another one
HASH_SCREENS := sha256sum $(SCREENS) | LC_ALL=C sort -k 2

# bulk_test SECONDS CONTROL_TICK_MS SEED ESP_RX_THRESHOLD PANEL_2 [REFUSED], three runs: the defaults; a slow CX60,
# the ESP's bytes late in big chunks and a second panel on the bus; and a CX60 in between with another seed
check: $(addprefix $(BUILD)/,$(PROGS)) $(BUILD)/esp_test lang
	rm -rf $(OUT)/shots $(OUT)/shots_no $(OUT)/esp $(OUT)/replay
	$(BUILD)/unit_test
	$(BUILD)/link_test
	$(BUILD)/fuzz_test
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

# changed makes a contact sheet of the screens that differ from golden.txt, each next to the old one. golden takes
# the screens as they are and keeps a copy in GOLDEN for changed.
GOLDEN := $(abspath $(OUT))/golden
changed:
	@cmp -s golden.txt $(GOLDEN)/golden.txt || $(MAKE) --no-print-directory golden_head
	@rm -rf $(OUT)/changed && mkdir -p $(OUT)/changed/before
	@cd $(OUT) && $(HASH_SCREENS) | diff $(CURDIR)/golden.txt - | sed -n 's/^> [0-9a-f]*  //p' \
	  | while read f; do n=$$(echo $$f | tr / _); cp $$f changed/$$n; \
	      if test -f $(GOLDEN)/$$f; then cp $(GOLDEN)/$$f changed/before/$$n; fi; done
	python3 shots.py --before $(OUT)/changed/before $(OUT)/changed

golden:
	cd $(OUT) && $(HASH_SCREENS) > $(CURDIR)/golden.txt
	@rm -rf $(GOLDEN) && mkdir -p $(GOLDEN) && cd $(OUT) && cp --parents $(SCREENS) $(GOLDEN)
	@cp golden.txt $(GOLDEN)/golden.txt

# When make golden left no copy: HEAD's screens that match golden.txt, drawn in a worktree of its own with this LVGL
golden_head:
	@echo "drawing HEAD's screens, make golden keeps a copy next time"
	@rm -rf $(GOLDEN) && mkdir -p $(GOLDEN) && tree=$$(mktemp -d) && \
	  git -C $(ROOT) worktree add -q --detach $$tree HEAD && rmdir $$tree/lib/lvgl && \
	  ln -s $(abspath $(LVGL)) $$tree/lib/lvgl && \
	  { $(MAKE) -C $$tree/sim -i check ESP_REPO=$(abspath $(ESP_REPO)) SAN= > $(OUT)/golden_head.log 2>&1; \
	    cd $$tree/sim/build/out && sha256sum -c $(CURDIR)/golden.txt 2>/dev/null | sed -n 's/: OK$$//p' \
	      | xargs -r cp --parents -t $(GOLDEN); }; \
	  rm $$tree/lib/lvgl && git -C $(ROOT) worktree remove --force $$tree
	@echo "HEAD drew $$(find $(GOLDEN) -name '*.ppm' | wc -l) of golden.txt's $$(wc -l < golden.txt) screens as it has" \
	  "them, see $(OUT)/golden_head.log"
	@cp golden.txt $(GOLDEN)/golden.txt

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
	python3 esp_yaml/esp_sync.py $(ROOT) $(ESP_REPO) $(if $(filter 1,$(FORCE)),--force)

clean:
	rm -rf build build-san

.PHONY: lang check changed golden golden_head shots gallery met esp_sync clean
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
