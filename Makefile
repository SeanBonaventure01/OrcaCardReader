# ORCA reader costume: top-level developer commands.
# `make check` is the gate for every change (see docs/ARCHITECTURE.md §9).

IDF_PATH   ?= $(HOME)/esp/esp-idf-v5.5.5
IDF_EXPORT := . $(IDF_PATH)/export.sh >/dev/null 2>&1
PORT       ?= $(firstword $(wildcard /dev/cu.usbserial-*))
HOST_BUILD := host/build


.PHONY: web-test web-e2e web-build hil app-flash
.PHONY: check portable host-test host-configure fw-build flash monitor clean sim repl scenarios scenarios-update props-long

check: portable host-test web-test web-e2e fw-build

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

web/node_modules:
	cd web && npm install

web-build: web/node_modules
	cd web && npm run build --silent

# Type check + unit/contract tests (Vitest)
web-test: web/node_modules
	cd web && npx tsc --noEmit && npx vitest run

# Playwright against orca-sim (builds the UI first)
web-e2e: web/node_modules host-test
	cd web && npx playwright test

fw-build: web-build
	@if [ -f $(IDF_PATH)/export.sh ]; then \
		$(IDF_EXPORT) && idf.py -C firmware build; \
	else \
		echo "fw-build: skipped (ESP-IDF not found at $(IDF_PATH))"; \
	fi

flash:
	$(IDF_EXPORT) && idf.py -C firmware -p $(PORT) -b 460800 flash

# Flash just the app (keeps the storage partition: config, settings, snapshot)
app-flash: fw-build
	$(IDF_EXPORT) && idf.py -C firmware -p $(PORT) -b 460800 app-flash

tools/hil/.venv:
	python3 -m venv tools/hil/.venv && tools/hil/.venv/bin/pip install -q -r tools/hil/requirements.txt

# Hardware-in-the-loop: flash the board, then drive it over serial and check the screen by webcam
hil: app-flash tools/hil/.venv
	tools/hil/.venv/bin/python tools/hil/smoke.py

monitor:
	$(IDF_EXPORT) && idf.py -C firmware -p $(PORT) monitor

clean:
	rm -rf $(HOST_BUILD) firmware/build
