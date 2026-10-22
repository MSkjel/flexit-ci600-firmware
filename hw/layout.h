// Flash layout of the STM32F103RE, 512 KiB in 2 KiB pages. The linker script and the tools read it too.
#pragma once

#define FLASH_BASE_ADDR 0x08000000  // the stock bootloader we keep, 0x08000000..0x08001FFF
#define FLASH_HEADER 0x08002000     // app header the bootloader checks, 0xA5B4C3D2 at +4
#define FLASH_APP 0x08002100        // this firmware, the bootloader jumps here
#define FLASH_SETTINGS 0x08070000   // settings pages, up to the end of flash
#define FLASH_SETTINGS_PAGES 32     // saves rotate through them, so each page is erased 1/32 as often
#define FLASH_PAGE 0x800
