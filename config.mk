# Included by hw/ and sim/, which set ROOT and BUILD first
LVGL     := $(ROOT)/lib/lvgl
INCLUDES := -I$(ROOT)/app -I$(ROOT)/ui -I$(ROOT)/proto -I$(LVGL) -I$(ROOT)/lib
DEFS     := -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DCI600_LV_MEM_KB=16  # LVGL heap size on the panel
WARN     := -Wall -Wextra -Werror
# The newest vX.Y.Z tag, the next patch level on a later commit so the ESP can tell them apart, 0.0.0 without one.
# Same as tools/mkupdate.py. $(BUILD)/version holds it and the objects depend on it, so a new tag rebuilds them.
VERSION  := $(shell git -C $(ROOT) describe --tags --match 'v[0-9]*.[0-9]*.[0-9]*' --exclude 'v*[!0-9.]*' 2>/dev/null \
              | awk -F'[v.-]' '{print $$2 "." $$3 "." $$4 + (NF > 4)}')
VERSION  := $(if $(VERSION),$(VERSION),0.0.0)
VER_PART  = $(or $(word $(1),$(subst ., ,$(VERSION))),0)
DEFS     += -DFW_MAJOR=$(call VER_PART,1) -DFW_MINOR=$(call VER_PART,2) -DFW_PATCH=$(call VER_PART,3)
FW_STAMP := $(BUILD)/version
$(FW_STAMP): FORCE
	@mkdir -p $(BUILD) && { echo $(VERSION) | cmp -s - $@ || echo $(VERSION) > $@; }
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
