# ORCA reader costume: top-level developer commands.
# `make check` is the gate for every change (see docs/ARCHITECTURE.md §9).

IDF_PATH   ?= $(HOME)/esp/esp-idf-v5.5.5
IDF_EXPORT := . $(IDF_PATH)/export.sh >/dev/null 2>&1
PORT       ?= $(firstword $(wildcard /dev/cu.usbserial-*))
HOST_BUILD := host/build

.PHONY: check portable host-test host-configure fw-build flash monitor clean sim repl scenarios scenarios-update props-long

check: portable host-test fw-build

portable:
	@tools/check_portable.sh

host-configure:
	@cmake -S host -B $(HOST_BUILD) -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null

host-test: host-configure
	@cmake --build $(HOST_BUILD)
	@$(HOST_BUILD)/orca_tests --gtest_brief=1

# Simulator: real API + WebSocket on http://localhost:8080 (SPEED=10 makes time 10x faster)
SPEED ?= 1
sim: host-configure
	@cmake --build $(HOST_BUILD) --target orca-sim
	$(HOST_BUILD)/orca-sim serve --port 8080 --speed $(SPEED) --web web/dist --state $(HOST_BUILD)/sim-state.json

repl: host-configure
	@cmake --build $(HOST_BUILD) --target orca-sim
	$(HOST_BUILD)/orca-sim repl --game fare_paid

scenarios: host-configure
	@cmake --build $(HOST_BUILD)
	@$(HOST_BUILD)/orca_tests --gtest_brief=1 --gtest_filter='Golden/*:ScenarioRunner.*'

# Rewrites golden transcripts after an intentional change. Review the git diff afterwards!
scenarios-update: host-configure
	@cmake --build $(HOST_BUILD) --target orca-sim
	@for f in host/scenarios/*.scenario.json; do $(HOST_BUILD)/orca-sim run $$f --update-golden || exit 1; done

props-long: host-configure
	@cmake --build $(HOST_BUILD)
	ORCA_PROP_SEEDS=2000 $(HOST_BUILD)/orca_tests --gtest_brief=1 --gtest_filter='AllGames/*'

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
