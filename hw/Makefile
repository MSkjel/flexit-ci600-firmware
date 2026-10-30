# Panel firmware for the STM32F103, needs arm-none-eabi-gcc
#   make                  build/ci600.bin, the app at 0x08002100
#   make full STOCK=dump  build/ci600_full.bin, a whole 512 KiB image with the stock bootloader
#   make flash STOCK=dump writes that image over SWD, needs pyocd
ROOT    := ..
BUILD   := build
OBJ_DIR := $(BUILD)
COMPILE  = $(CC) $(CFLAGS)
.DEFAULT_GOAL := all
include $(ROOT)/config.mk
DEFS    += -DPJPEG_ALSO=0  # the panel's JPEGs are one quality, see pjpeg.h
STOCK   ?=
CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size
ARCH    := -mcpu=cortex-m3 -mthumb
CFLAGS  := $(ARCH) -O2 -g -std=gnu11 $(WARN) -ffunction-sections -fdata-sections -MMD -MP -I. $(INCLUDES) $(DEFS)
LDFLAGS := $(ARCH) -T $(BUILD)/ci600.ld -nostartfiles -Wl,--gc-sections -Wl,-Map=$(BUILD)/ci600.map --specs=nano.specs \
           -Wl,--no-warn-rwx-segments

OBJ := $(patsubst $(ROOT)/%.c,$(BUILD)/%.o,$(sort $(wildcard $(ROOT)/hw/*.c))) $(APP_OBJ)

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

# LVGL and the JPEG decoder at -O3, the pixel loops unrolled too. It costs flash, not RAM. picture.c and
# anim_draw.c mark only their pixel loops -O3.
# LVGL's warnings off, they aren't ours
$(BUILD)/lib/%.o: CFLAGS += -O3 -w
# or GCC turns their byte loops back into calls to themselves
$(BUILD)/hw/mem.o: CFLAGS += -fno-tree-loop-distribute-patterns
$(BUILD)/lib/lvgl/src/draw/sw/%.o $(LV_COPY)/src/draw/sw/%.o: CFLAGS += -funroll-loops
$(BUILD)/lib/lvgl/src/draw/lv_draw_mask.o: CFLAGS += -funroll-loops
$(BUILD)/lib/lvgl/src/misc/lv_color.o: CFLAGS += -funroll-loops
# the patched LVGL files like the rest of it
$(LV_COPY)/%.o: CFLAGS += -O3 -w

flash: $(BUILD)/ci600_full.bin
	python3 $(ROOT)/tools/flash.py $<

clean:
	rm -rf $(BUILD)

.PHONY: all full flash clean
-include $(OBJ:.o=.d)
