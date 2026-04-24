## ── ESPHome local build / flash Makefile ────────────────────────────────────
##
## See README.md for full setup instructions.
##
## Quick usage:
##   make              → compile only (no device needed)
##   make demo         → compile the demo firmware
##   make flash        → compile + flash over USB
##   make ota IP=x.x.x.x  → compile + flash over WiFi (OTA)
##   make logs         → open serial monitor (device must be connected)
##   make logs-ota IP=x.x.x.x  → stream logs over WiFi
##   make clean        → remove build artefacts

ESPHOME  := esphome
CONFIG   := remote.yaml
DEMO     := remote_demo.yaml

# ── targets ──────────────────────────────────────────────────────────────────

.PHONY: all compile flash ota logs logs-ota clean help

all: compile

## Compile production firmware (remote.yaml)
compile:
	$(ESPHOME) compile $(CONFIG)

## Compile demo firmware (remote_demo.yaml, no HA required)
demo:
	$(ESPHOME) compile $(DEMO)

## Compile + flash over USB serial
## Auto-detects the port; set PORT=/dev/cu.usbserial-XXXX if it fails
flash:
	$(ESPHOME) upload $(CONFIG) $(if $(PORT),--device $(PORT),)

## Compile + flash demo over USB
flash-demo:
	$(ESPHOME) upload $(DEMO) $(if $(PORT),--device $(PORT),)

## Compile + flash over WiFi (OTA)
## Usage: make ota IP=192.168.1.42
ota:
	@test -n "$(IP)" || (echo "Usage: make ota IP=x.x.x.x" && exit 1)
	$(ESPHOME) upload $(CONFIG) --device $(IP)

## Open serial monitor (USB, device must be connected)
logs:
	$(ESPHOME) logs $(CONFIG) $(if $(PORT),--device $(PORT),)

## Stream logs over WiFi
## Usage: make logs-ota IP=192.168.1.42
logs-ota:
	@test -n "$(IP)" || (echo "Usage: make logs-ota IP=x.x.x.x" && exit 1)
	$(ESPHOME) logs $(CONFIG) --device $(IP)

## Remove generated build directories
clean:
	rm -rf .esphome/build

help:
	@grep -E '^## ' Makefile | sed 's/## //'
