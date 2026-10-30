#!/bin/bash
# Build the welcome / key-tester firmware for one or every PCB and print where
# the factory images are. Run from anywhere:
#   ./compile_welcome.sh            # v3, v4 and v4_1
#   ./compile_welcome.sh v4_1       # one board
# The factory image for the website's flasher page (static/firmware/smart-oled-remote/)
# is .esphome/build/welcome_<nn>/.pioenvs/welcome_<nn>/firmware.factory.bin.
cd "$(dirname "$0")"
ESPHOME="${ESPHOME:-esphome}"
for pcb in ${@:-v3 v4 v4_1}; do
  "$ESPHOME" -s PCB "$pcb" compile welcome.yaml || exit 1
done
ls -1 .esphome/build/welcome_*/.pioenvs/welcome_*/firmware.factory.bin
