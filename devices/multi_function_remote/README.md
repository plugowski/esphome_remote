# Multi-Function Remote

Full documentation — setup, entity configuration, build commands, and screenshots — lives in the **[repository root README](../../README.md)**.

```
esphome compile welcome_mosfet.yaml && esphome upload welcome_mosfet.yaml --device /dev/cu.usbserial-11130 && picocom -b115200 /dev/cu.usbserial-11130 --imap lfcrlf --omap crlf
```