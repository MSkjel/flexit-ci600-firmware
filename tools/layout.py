"""Panel flash layout, read from hw/layout.h so the tools and the firmware agree."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_H = (ROOT / "hw" / "layout.h").read_text()


def _get(name):
    return int(re.search(r"#define %s (0x[0-9A-Fa-f]+)" % name, _H).group(1), 16)


BASE = _get("FLASH_BASE_ADDR")  # start of flash, where the stock bootloader is
HEADER = _get("FLASH_HEADER")  # the app header the bootloader checks
APP = _get("FLASH_APP")  # this firmware
SETTINGS = _get("FLASH_SETTINGS")  # settings pages, up to the end of flash
PAGE = _get("FLASH_PAGE")
SIZE = 512 * 1024
APP_MARK = 0xA5B4C3D2  # at HEADER + 4, the bootloader only starts the app if it's there
