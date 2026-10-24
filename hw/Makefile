# Panel firmware for the STM32F103, needs arm-none-eabi-gcc
#   make                  build/ci600.bin, the app at 0x08002100
#   make full STOCK=dump  build/ci600_full.bin, a whole 512 KiB image with the stock bootloader
#   make flash STOCK=dump writes that image over SWD, needs pyocd
ROOT    := ..
BUILD   := build
.DEFAULT_GOAL := all
include $(ROOT)/config.mk
STOCK   ?=
CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size
ARCH    := -mcpu=cortex-m3 -mthumb
CFLAGS  := $(ARCH) -O2 -g -std=gnu11 $(WARN) -ffunction-sections -fdata-sections -MMD -MP -I. $(INCLUDES) $(DEFS)
LDFLAGS := $(ARCH) -T $(BUILD)/ci600.ld -nostartfiles -Wl,--gc-sections -Wl,-Map=$(BUILD)/ci600.map --specs=nano.specs \
           -Wl,--no-warn-rwx-segments

OBJ := $(patsubst $(ROOT)/%.c,$(BUILD)/%.o,$(sort $(wildcard $(ROOT)/hw/*.c)) $(APP_SRC) $(LV_SRC)) $(LV_COPY_SRC:.c=.o)

all: $(BUILD)/ci600.bin
	@echo "ci600 $(VERSION)"
	@$(SIZE) $(BUILD)/ci600.elf

full: $(BUILD)/ci600_full.bin

$(BUILD)/ci600.elf: $(OBJ) $(BUILD)/ci600.ld
	$(CC) -o $@ $(OBJ) $(LDFLAGS) -lc -lgcc

$(BUILD)/ci600.ld: ci600.ld layout.h
	@mkdir -p $(BUILD)
	$(CC) -E -P -x c -I. $< -o $@

$(BUILD)/ci600.bin: $(BUILD)/ci600.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/ci600_full.bin: $(BUILD)/ci600.bin FORCE  # every time, STOCK may name another dump
	@test -n "$(STOCK)" || { echo "needs STOCK=<the stock CI600 dump>"; exit 1; }
	python3 $(ROOT)/tools/mkimage.py "$(STOCK)" $< $@

$(BUILD)/%.o: $(ROOT)/%.c $(ROOT)/config.mk $(FW_STAMP) | $(LVGL_PATCHED)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/ui/ui_info.o: $(AUTHORS_H)

# LVGL, the JPEG decoder and our decoders at -O3, the pixel loops unrolled too. It costs flash, not RAM.
# LVGL's warnings off, they aren't ours
$(BUILD)/lib/%.o: CFLAGS += -O3 -w
$(BUILD)/app/picture.o $(BUILD)/app/anim.o: CFLAGS += -O3
$(BUILD)/lib/lvgl/src/draw/sw/%.o: CFLAGS += -funroll-loops
$(BUILD)/lib/lvgl/src/draw/lv_draw_mask.o: CFLAGS += -funroll-loops
$(BUILD)/lib/lvgl/src/misc/lv_color.o: CFLAGS += -funroll-loops
# the patched copies find their relative includes next to the originals
$(LV_COPY)/%.o: $(LV_COPY)/%.c $(ROOT)/config.mk $(FW_STAMP)
	$(CC) $(CFLAGS) -O3 -w -I$(LVGL)/$(dir $*) -c -o $@ $<

flash: $(BUILD)/ci600_full.bin
	python3 $(ROOT)/tools/flash.py $<

clean:
	rm -rf $(BUILD)

.PHONY: all full flash clean
-include $(OBJ:.o=.d)
