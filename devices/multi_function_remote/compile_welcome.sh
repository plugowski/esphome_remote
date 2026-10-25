#!/bin/bash
# Run from: devices/multi_function_remote/
cd "$(dirname "$0")"
esphome compile welcome_mosfet.yaml
