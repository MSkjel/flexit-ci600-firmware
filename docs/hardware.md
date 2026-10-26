# Hardware

What's in a CI600, as far as this firmware cares. All of it comes from my own panel and from reading the stock
firmware. I haven't seen any other revisions of the board, but if yours looks different, open an issue with a
picture.

## The board

| What | Part |
|---|---|
| MCU | STM32F103RET6 (LQFP64), 72 MHz, 512 KiB flash, 64 KiB RAM |
| Screen | 320x240 TFT with an ILI932x controller, on SPI |
| Keys | Five: up, down, OK, back and ? |
| Lights | Alarm, filter and OK |
| Backlight | PWM, so it can dim |
| Room sensor | A thermistor on the ADC |
| Clock | The STM32's RTC on a 32.768 kHz crystal |
| Bus | RS-485 on USART1, 115200 8N1 |

The panel gets both power and the bus from the unit over the same cable. The pinout of the unit's header is in
the [ESP repo](https://github.com/MSkjel/esphome-flexit-modbus-server/blob/main/hardware/xiao-esp32-c3-rs485-breakout-board-for-seeed-studio-xiao-tp8485e.md).

## Pins

| What | Pins |
|---|---|
| Screen | PA5 SCK, PA6 MISO, PA7 MOSI, PA4 CS, PA1 reset |
| Keys | PB7 up, PB5 down, PB6 OK, PB9 back, PB8 help. High when pressed |
| Lights | PC10 alarm, PC11 filter, PC12 OK. Low is on |
| Backlight | PA8, TIM1 CH1 |
| Room sensor | PC0, ADC channel 10 |
| RS-485 | PA9 TX, PA10 RX, PA12 driver enable, PA11 receiver disable |
| Bootloader | PB4, low at power-on keeps the panel in the bootloader |
| SWD | PA13 SWDIO, PA14 SWCLK |

`hw/board.h` has the same list.

## Flash

| Address | What |
|---|---|
| 0x08000000 | Flexit's bootloader, 8 KiB. Nothing here ever writes to it |
| 0x08002000 | The app header. The bootloader won't start anything without it |
| 0x08002100 | This firmware |
| 0x08070000 | Settings, 32 pages of 2 KiB that the saves take turns on |

## The bootloader

Flexit's bootloader is the reason this can be flashed over the bus. At power-on it starts the firmware, unless:

- there's no valid firmware, like after an update that stopped halfway
- the firmware asked for it. That's how both the stock firmware and this one get there when you flash
- PB4 is held low

While it's running the screen is dark and all three lights are on. It always answers at address 1, whatever
address the panel had, and only knows one Modbus function, 0x64. Update files are encrypted and signed with
Flexit's key, so it only takes files made with that key.

When an update starts it erases everything after itself. The firmware isn't marked valid until the whole file
has been checked, so a broken or half-done update just leaves the panel in the bootloader, waiting for another
try.

It also answers really fast, without the pause Modbus wants between frames. An ESP that switches its RS-485
direction pin in software is often still sending when the reply comes, and misses it.

## Read protection

The STM32 is read protected from the factory. SWD can't read the flash, and turning the protection off erases
the whole chip, the bootloader too. Getting a dump of a stock panel takes hardware mods and glitching the chip.
That's why the bus is the normal way in.

## Forcing the bootloader

If the firmware is broken and the panel doesn't answer on the bus anymore, you can force it into the bootloader.
On my board PB4 (pin 56, on the bottom row of the chip) goes to a small cap to GND. Short that cap, power the
panel up, hold it for a second and let go. Dark screen and three lights means it worked, then flash as usual.

## SWD

With an ST-LINK and `pip install pyocd`:

    make -C hw flash STOCK=<a stock flash dump>

The bootloader comes from the dump, since the chip is erased once the protection is off. Leave the ST-LINK's
3.3 V off when the unit powers the panel. SWD keeps the panel's settings.
