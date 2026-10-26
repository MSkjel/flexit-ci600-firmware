# Included by hw/ and sim/, which set ROOT, BUILD, OBJ_DIR and COMPILE first
# The objects depend on that Makefile and this file, which set their flags. Taken before the .d files join
# MAKEFILE_LIST.
BUILD_MK := $(MAKEFILE_LIST)
LVGL     := $(ROOT)/lib/lvgl
INCLUDES := -I$(ROOT)/app -I$(ROOT)/ui -I$(ROOT)/proto -I$(LVGL) -I$(ROOT)/lib
DEFS     := -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DCI600_LV_MEM_KB=16  # LVGL heap size on the panel
WARN     := -Wall -Wextra -Werror
# The newest vX.Y.Z tag, the next patch level on a later commit so the ESP can tell them apart, 0.0.0 without one.
# $(BUILD)/version holds it and only changes with it, tools/mkupdate.py names the stream from hw/build/version.
# $(VERSION_H) splits it into FW_MAJOR, FW_MINOR and FW_PATCH, so a new tag only rebuilds what includes it.
VERSION  := $(shell git -C $(ROOT) describe --tags --match 'v[0-9]*.[0-9]*.[0-9]*' --exclude 'v*[!0-9.]*' 2>/dev/null \
              | awk -F'[v.-]' '{print $$2 "." $$3 "." $$4 + (NF > 4)}')
VERSION  := $(if $(VERSION),$(VERSION),0.0.0)
VER_PART  = $(or $(word $(1),$(subst ., ,$(VERSION))),0)
FW_STAMP := $(BUILD)/version
$(FW_STAMP): FORCE
	@mkdir -p $(BUILD) && { echo $(VERSION) | cmp -s - $@ || echo $(VERSION) > $@; }
VERSION_H := $(BUILD)/gen/version.h
$(VERSION_H): $(FW_STAMP)
	@mkdir -p $(dir $@)
	@printf '#define FW_MAJOR %s\n#define FW_MINOR %s\n#define FW_PATCH %s\n' \
	  $(call VER_PART,1) $(call VER_PART,2) $(call VER_PART,3) > $@
# The authors for the About pages, from git the same way: $(AUTHORS_H) only changes with the list, which is empty
# without python3. The simulator sets AUTHORS so its screens don't change with the history.
AUTHORS_H := $(BUILD)/gen/authors.h
INCLUDES += -I$(BUILD)/gen
$(AUTHORS_H): FORCE
	@if command -v python3 >/dev/null; then python3 $(ROOT)/tools/mkauthors.py $(ROOT) $@ $(AUTHORS); \
	else mkdir -p $(dir $@) && { echo '#define AUTHORS_N 0' | cmp -s - $@ || echo '#define AUTHORS_N 0' > $@; }; fi
FORCE:
.PHONY: FORCE
# Sources in sorted order, so they link in the same order everywhere and the firmware comes out byte for byte the
# same. find goes by the order on disk, and wildcard by the locale.
APP_SRC  := $(sort $(wildcard $(ROOT)/app/*.c $(ROOT)/ui/*.c $(ROOT)/ui/fonts/*.c $(ROOT)/proto/*.c $(ROOT)/lib/tjpgd/*.c))
# lib/lvgl stays as LVGL released it. The files lvgl-ci600.patch changes are copied into the build, patched there
# and built instead of the originals.
LV_PATCH     := $(ROOT)/lib/lvgl-ci600.patch
LV_CHANGED   := $(shell sed -n 's|^+++ b/||p' $(LV_PATCH))
LV_COPY      := $(BUILD)/lvgl-ci600
LV_SRC       := $(sort $(filter-out $(addprefix $(LVGL)/,$(LV_CHANGED)),$(shell find $(LVGL)/src -name '*.c')))
LV_COPY_SRC  := $(addprefix $(LV_COPY)/,$(LV_CHANGED))
LVGL_PATCHED := $(LV_COPY)/.patched
$(LVGL_PATCHED): $(LV_PATCH) $(addprefix $(LVGL)/,$(LV_CHANGED))  # the originals too, for a submodule bump
	@rm -rf $(LV_COPY) && mkdir -p $(LV_COPY)
	@cd $(LVGL) && cp --parents $(LV_CHANGED) $(abspath $(LV_COPY))
	@patch -s -p1 -d $(LV_COPY) < $(LV_PATCH)
	@touch $@
$(LV_COPY_SRC): $(LVGL_PATCHED)

# The objects, the same rules for both. COMPILE is the compiler with its flags, OBJ_DIR where the objects go.
# $(VERSION_H) is there before anything compiles, the .d files then know which objects include it.
APP_OBJ := $(patsubst $(ROOT)/%.c,$(OBJ_DIR)/%.o,$(APP_SRC) $(LV_SRC)) $(LV_COPY_SRC:.c=.o)
$(OBJ_DIR)/%.o: $(ROOT)/%.c $(BUILD_MK) | $(LVGL_PATCHED) $(VERSION_H)
	@mkdir -p $(dir $@)
	$(COMPILE) -c -o $@ $<
$(OBJ_DIR)/ui/ui_info.o: $(AUTHORS_H)
# the patched copies find their relative includes next to the originals
$(LV_COPY)/%.o: $(LV_COPY)/%.c $(BUILD_MK)
	$(COMPILE) -I$(LVGL)/$(dir $*) -c -o $@ $<
