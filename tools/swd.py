"""SWD session over an ST-LINK with pyocd, for flash.py and perf.py."""

from pyocd.core.helpers import ConnectHelper
from pyocd.target.builtin.target_STM32F103RC import STM32F103RC

DBGMCU_CR = 0xE0042004

# pyocd's STM32F103RC target has 512 KiB of flash like the panel's STM32F103RE. By default pyocd sets DBGMCU_CR
# to 0x7E3FFF00, which freezes every timer while the core is halted. With TIM1 stopped and the backlight on the
# panel draws more than its supply can give and restarts, and the SWD link drops. 0x107 keeps debug running in
# sleep, stop and standby and freezes only the watchdog.
STM32F103RC.post_connect_hook = lambda self: self.write_memory(DBGMCU_CR, 0x107)


def session(frequency=1000000):
    return ConnectHelper.session_with_chosen_probe(
        target_override="stm32f103rc", connect_mode="attach", options={"frequency": frequency}
    )
