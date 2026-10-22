TJpgDec R0.03 by ChaN (http://elm-chan.org/fsw/tjpgd/), as LVGL 8.3.11 ships it in `src/extra/libs/sjpg`, without
LVGL's `LV_USE_SJPG` guard. `tjpgdcnf.h` is set up for the panel, and `jd_decomp_rows()` (marked CI600) decodes a
range of MCU rows of a picture that has a restart marker after every row, so the panel can draw any 8-line strip of
a JPEG without decoding it from the top.
