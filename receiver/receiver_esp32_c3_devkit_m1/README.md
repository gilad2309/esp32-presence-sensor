# ESP32-C3 Receiver

## What It Does

Receives **detection**, **range**, and **speed** over ESP-NOW, then changes the RGB LED color accordingly.

## Build and Run

From project root:

```bash
idf.py set-target esp32c3
idf.py build
idf.py -p <PORT> flash monitor
```

Windows example:

```bash
idf.py -p COM5 flash monitor
```
