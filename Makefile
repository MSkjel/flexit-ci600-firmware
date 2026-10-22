#   make         build the firmware, hw/build/ci600.bin
#   make check   run all tests (make check SAN=1 adds ASan and UBSan)
#   make format  run clang-format and ruff
all:
	$(MAKE) -C hw

check:
	$(MAKE) -C sim check

format:
	clang-format -i $$(git ls-files '*.c' '*.h' '*.cpp')
	ruff format

.PHONY: all check format
