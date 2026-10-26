TJpgDec R0.03 by ChaN (http://elm-chan.org/fsw/tjpgd/), as LVGL 8.3.11 ships it in `src/extra/libs/sjpg`, without
LVGL's `LV_USE_SJPG` guard. `tjpgdcnf.h` is set up for the panel. The changes are marked CI600: `jd_decomp_row()`
decodes the MCUs of one MCU row that meet a column range, in a picture with a restart marker after every row, so the
panel can draw any part of a JPEG without decoding it from the top or across the whole width. The MCUs left of the
range are only read past. A Huffman code takes one compare per length, and the colours go straight to RGB565.
