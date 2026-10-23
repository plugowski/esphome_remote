## ── ESPHome local build / flash Makefile ────────────────────────────────────
##
## See README.md for full setup instructions.
##
## Quick usage:
##   make              → compile only (no device needed)
##   make demo         → compile the demo firmware
##   make flash        → compile + flash over USB
##   make ota IP=x.x.x.x  → compile + flash over WiFi (OTA)
##   make espnow       → compile the ESP-NOW remote firmware
##   make flash-espnow → compile + flash the ESP-NOW remote over USB
##   make hub          → compile the ESP-NOW hub (devices/espnow_hub)
##   make flash-hub    → compile + flash the hub over USB
##   make ota-hub IP=x.x.x.x  → flash the hub over WiFi
##   make logs         → open serial monitor (device must be connected)
##   make logs-ota IP=x.x.x.x  → stream logs over WiFi
##   make clean        → remove build artefacts

ESPHOME  := esphome
CONFIG   := remote.yaml
DEMO     := remote_demo.yaml
ESPNOW   := remote_espnow.yaml
HUB      := ../espnow_hub/hub.yaml

# ── targets ──────────────────────────────────────────────────────────────────

.PHONY: all compile flash ota logs logs-ota clean help \
        espnow flash-espnow logs-espnow hub flash-hub ota-hub logs-hub

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

## ── ESP-NOW variant ─────────────────────────────────────────────────────────

## Compile the ESP-NOW remote (remote_espnow.yaml, no WiFi)
espnow:
	$(ESPHOME) compile $(ESPNOW)

## Compile + flash the ESP-NOW remote over USB (no OTA without WiFi)
flash-espnow:
	$(ESPHOME) upload $(ESPNOW) $(if $(PORT),--device $(PORT),)

## Serial monitor for the ESP-NOW remote
logs-espnow:
	$(ESPHOME) logs $(ESPNOW) $(if $(PORT),--device $(PORT),)

## Compile the ESP-NOW hub (devices/espnow_hub/hub.yaml)
hub:
	$(ESPHOME) compile $(HUB)

## Compile + flash the hub over USB
flash-hub:
	$(ESPHOME) upload $(HUB) $(if $(PORT),--device $(PORT),)

## Flash the hub over WiFi.  Usage: make ota-hub IP=192.168.1.43
ota-hub:
	@test -n "$(IP)" || (echo "Usage: make ota-hub IP=x.x.x.x" && exit 1)
	$(ESPHOME) upload $(HUB) --device $(IP)

## Stream hub logs over WiFi.  Usage: make logs-hub IP=192.168.1.43
logs-hub:
	@test -n "$(IP)" || (echo "Usage: make logs-hub IP=x.x.x.x" && exit 1)
	$(ESPHOME) logs $(HUB) --device $(IP)

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
	rm -rf .esphome/build ../espnow_hub/.esphome/build

help:
	@grep -E '^## ' Makefile | sed 's/## //'
