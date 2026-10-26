#   make               build the firmware, hw/build/ci600.bin
#   make check         run all tests (make check SAN=1 adds ASan and UBSan)
#   make format        run clang-format and ruff
#   make format-check  check the formatting and ruff's lints, as CI does
C_FILES := $$(git ls-files '*.c' '*.h' '*.cpp')

all:
	$(MAKE) -C hw

check:
	$(MAKE) -C sim check

format:
	clang-format -i $(C_FILES)
	ruff format

format-check:
	clang-format --dry-run --Werror $(C_FILES)
	ruff format --check
	ruff check

.PHONY: all check format format-check
