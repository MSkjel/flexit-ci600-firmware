/* CI600, STM32F103xE with 512 KiB flash and 64 KiB RAM. Run through cpp first for layout.h. */
#include "layout.h"

MEMORY
{
    FLASH (rx) : ORIGIN = FLASH_APP, LENGTH = FLASH_SETTINGS - FLASH_APP
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 64K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
/* How deep the stack goes at the most, by the compiler's own count (-fcallgraph-info=su, make stack): an animation's
   script that steps on and inflates its place's next frame, 1.8 KB with the timer above it, under the bus interrupt
   and the two it nests with, 0.5 KB */
_min_stack = 0xA00;

SECTIONS
{
    .text : {
        KEEP(*(.isr_vector))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    .ARM.exidx : { *(.ARM.exidx*) } > FLASH

    _sidata = LOADADDR(.data);
    .data : {
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) : {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* not zeroed at start-up, survives a reset. The fault record and the restart and start counters live here */
    .noinit (NOLOAD) : {
        *(.noinit*)
        . = ALIGN(4);
        _end = .;
    } > RAM

    ASSERT(_end + _min_stack <= _estack, "not enough RAM left for the stack")
}
