# Development

Building and testing are in the [README](../README.md#development). This is the rest: update files, where the time
and the stack go, how the ESP link and the screensaver's overlays work, and where to add things.

- [Update files and versions](#update-files-and-versions)
- [Where the time goes](#where-the-time-goes)
- [How deep the stack goes](#how-deep-the-stack-goes)
- [The ESP link](#the-esp-link)
- [Overlays](#overlays)
- [Adding things](#adding-things)

## Update files and versions

`tools/mkupdate.py hw/build/ci600.bin ci600.fxup` makes the `.fxup`. It needs the bootloader's key, which isn't in
this repo. Give it a stock flash dump with `--stock`, or set `CI600_FXUP_KEY` (64 hex digits). CI has it as a
secret, and pushing a tag like `v1.2.3` makes a release with `ci600.fxup` and `ci600.bin`, a pre-release when the
tag is on dev, for the ESP's Panel Firmware Pre-releases.

The newest `vX.Y.Z` tag is the version the panel reports, 0.0.0 without one. A build from a later commit gets the
next patch level, so the ESP can tell it from the release. The next release has that number too, so after flashing
such a build, turn Panel Firmware Force on to get that release.

For SWD, see [hardware.md](hardware.md#swd).

## Where the time goes

`make -C hw PROF=1` builds `hw/build/prof/ci600.bin`, which looks about 1000 times a second at the function the
processor is in, and every 10 s logs the 24 busiest in the ESP's log, like `prof  15.0 % paint`, and what the others
took together. Its first lines say how much of the time it was asleep, waiting for an interrupt, how much it waited
for the LCD to take a strip, and how many of the pixels it drew went to the LCD.

Make an `.fxup` of it as above and flash it with Panel Firmware Force on, since it has the same version as the build
without it, and the same for going back. The function names take about 28 KB of flash, the counts 264 bytes of RAM.

## How deep the stack goes

`make -C hw stack` builds `hw/build/stack` with the compiler's call graph and lists main, the interrupts and every
callback with the deepest chain of calls below it, in bytes (`tools/stack.py`). The linker keeps `_min_stack` of the
RAM for it (`hw/ci600.ld`) and fails the build without that much left: 2.5 KB, for the 1.8 KB of an animation's
script that steps on and inflates its place's next frame, under the bus interrupt and the two it nests with, 0.5 KB.
Diagnostics shows how much of it was never used.

## The ESP link

About 9 times a second the CX60 sends a status broadcast and then leaves the bus quiet for a moment. The ESP
sends a frame in that gap and the panel answers. Anything left over goes in the gaps after the CX60's polls.
Messages are numbered and resent until the other side has them. `proto/pbus.h` has the bus timing, `pbulk.h` the
frames, `plink_keys.h` and `plink_values.h` the messages. The ESP repo keeps a copy of `proto/`: update it with
`make -C sim esp_sync` (`esp_test` fails if they differ).

Screenshots for the ESP's camera go the other way, a 64x8 tile at a time, JPEG-coded as it's drawn again on its way
to the LCD (`app/shot.c`), so the panel keeps nothing but the tile being sent.

## Overlays

Overlays over the screensaver's picture have 16 colours, each with its own opacity (`K_OVERLAY_BEGIN`). An animated
one keeps its frame and the deflated rectangles that change it, a still one only its packed pieces, unpacked one at a
time as a strip is drawn, and a sheet its frames as a still's pieces each, so a place that plays them on its own
costs nothing more. A field has no pixels at all: the panel works out how much of it there is at each one, from left
to right, round its middle, in a wedge or cloudy, and draws that far along its 16 colours (`OVERLAY_FIELD_*`,
`field_at()` in `app/overlay_draw.c`).

A place shows its overlay in a box of any size, the frame where pan puts it, once or round and round
(`K_OVERLAY_VIEW`), and only the runs of columns its script's shape leaves (`OVERLAY_DO_SHAPE`), fading in at its
edges with soft ones (`OVERLAY_DO_SOFT`). Its colours cover what's behind, or add to it, screen it or multiply it
(`OVERLAY_BLEND_*`), and with `OVERLAY_VIEW_SMOOTH` each pixel is between the four of its frame it falls among.

What a place shows is numbers, its channels: where it is, its box, turn, sizes, opacity, light, pivot and depth.
Glides and drifts set them, from a pool all places share (`moves[]` in `app/overlay_motion.c`), so a place that
stands still takes none, and one that wanders or faces where it goes keeps how in the overlays' memory while it
does (`roam_t`).

A place's script (`K_OVERLAY_SCRIPT`, `app/overlay_script.c`) is steps the panel runs itself, and each value in one
can be worked out by a few operations from the time, random numbers, sines and where the places are
(`OVERLAY_OP_*`). A value can also be bound to a channel, a register, the frame it shows or each row it's drawn in
(`OVERLAY_DO_BIND`, `K_OVERLAY_BIND`): then the panel works it out again every motion step, 20 a second, with
nothing more from the script or the ESP. A wave is such a value, and so is following where it is across its area.

A 16-colour picture can have parts, a box and some of its colours each, in front of the overlays farther off than
them (`K_PIC_PART`): the panel leaves an overlay's pixels undrawn where a nearer part's colours are. An overlay as
near as it starts is in front of them all.

## Adding things

- **Something from Home Assistant:** nothing to do here, the ESP sends it as an item.
- **A value or setting:** a line in `proto/plink_values.h` if the ESP should see it, otherwise in `app/values.h`.
  Then a menu row in `ui/ui_menus.c` and the default in `panel_defaults()` (`app/panel.c`). The key is also its id
  in flash.
- **Optional equipment:** a line in `app/equipment.h`. Anything with `.need = FBIT(FEAT_x)` hides without it.
- **A text:** name and English text in `ui/lang.h`, the translation in each `ui/lang_*.c`.
- **A language:** copy `ui/lang_no.c` and name it in its last line, add it to `LANGS` in `ui/lang.c` and bump `N_LANGS`
  (`proto/plink_values.h`). `make check` catches missing texts, mismatched placeholders and characters the fonts
  don't have (`TEXT_RANGE` in `tools/mkfonts.py`).
- **An icon:** add it to `ICONS` in `tools/mkfonts.py` and run it. Its place in the list is its id on the link.
- **A message:** a new key in `proto/plink_keys.h`. Unknown keys are ignored. Changing what a key means needs a new
  `PLINK_VERSION`.
