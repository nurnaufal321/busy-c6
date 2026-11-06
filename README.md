# BUSY status on a Waveshare ESP32-C6-Touch-LCD-1.9

Standalone firmware that shows the BUSY Bar's 13 status animations on the 1.9-inch touch board.
It has no Wi-Fi, Bluetooth, timer or clock.

- **Swipe left / right:** next / previous status, wrapping. It remembers the last one after
  power-off.
- **Turn the board over:** the picture flips, and left stays left.

The picture is the bar's 72×16 display, drawn at 4 px per LED (288×64) in the middle of the
320×170 screen.

## Build and flash

Needs ESP-IDF 5.5 or later.

```bash
python3 tools/pack_status.py
. ~/esp/v5.5.2/esp-idf/export.sh
idf.py set-target esp32c6
idf.py -p /dev/cu.usbmodemXXXX flash
```

`tools/pack_status.py` packs the animations from `../vendor/apps_assets` into `pack/status.pack`
(about 1.5 MB), and checks the result frame by frame against the source PNGs. `idf.py flash`
writes the pack to the `status` partition along with the app.

## Credits

The animations and themes come from [BUSY Bar's firmware](https://github.com/busy-app/busybar-firmware):
animations under CC-BY-SA-4.0, themes under GPL-2.0-or-later (see `../UPSTREAM.md`). Pin assignments
and the panel setup come from Waveshare's
[ESP32-C6-LCD-1.9 examples](https://github.com/waveshareteam/ESP32-C6-LCD-1.9).
