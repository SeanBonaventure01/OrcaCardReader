# ORCA reader costume: top-level developer commands.
# `make check` is the gate for every change (see docs/ARCHITECTURE.md §9).

IDF_PATH   ?= $(HOME)/esp/esp-idf-v5.5.5
IDF_EXPORT := . $(IDF_PATH)/export.sh >/dev/null 2>&1
PORT       ?= $(firstword $(wildcard /dev/cu.usbserial-*))
HOST_BUILD := host/build

.PHONY: check portable host-test host-configure fw-build flash monitor clean

check: portable host-test fw-build

portable:
	@tools/check_portable.sh

host-configure:
	@cmake -S host -B $(HOST_BUILD) -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null

host-test: host-configure
	@cmake --build $(HOST_BUILD)
	@ctest --test-dir $(HOST_BUILD) --output-on-failure

fw-build:
	@if [ -f $(IDF_PATH)/export.sh ]; then \
		$(IDF_EXPORT) && idf.py -C firmware build; \
	else \
		echo "fw-build: skipped (ESP-IDF not found at $(IDF_PATH))"; \
	fi

flash:
	$(IDF_EXPORT) && idf.py -C firmware -p $(PORT) -b 460800 flash

monitor:
	$(IDF_EXPORT) && idf.py -C firmware -p $(PORT) monitor

clean:
	rm -rf $(HOST_BUILD) firmware/build
