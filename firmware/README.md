# BUSY C6 firmware

Standalone firmware that shows the BUSY Bar's 13 status animations on the 1.9-inch touch board.
It has no Wi-Fi or Bluetooth. A web page sets the time and status over USB; see `../PROTOCOL.md`.

- **Swipe left / right:** next / previous status, wrapping.
- **Tap:** the next way of filling the screen, in this order:
  - **Original:** 4 px LEDs in the middle of the screen, with black bars.
  - **Tall:** the picture at its own shape on fine 3 px dots, in a rounded panel that fills the
    screen, with its edge colours fading out above and below.
  - **Pan:** 10 px LEDs filling the height, the view sliding slowly across the picture.
  - **Glow:** the original picture, with its edge colours fading out into the bars.
  - **Clock:** the picture at the top, with the date and time below. The time comes from the
    [web page](https://nurnaufal321.github.io/busy-c6/) over USB.
- **Turn the board over:** the picture flips, and left stays left.

It remembers the last status and layout after power-off. The picture is the bar's 72×16 display
on the 320×170 screen.

## Build and flash

Needs ESP-IDF 5.5 or later.

```bash
python3 tools/pack_status.py
. ~/esp/v5.5.2/esp-idf/export.sh
idf.py set-target esp32c6
idf.py -p /dev/cu.usbmodemXXXX flash
```

`tools/pack_status.py` packs the animations from `assets/` into `pack/status.pack`
(about 1.5 MB), and checks the result frame by frame against the source PNGs. `idf.py flash`
writes the pack to the `status` partition along with the app.

`main/font_data.c` is generated from Roboto Bold by `tools/gen_font.swift` and checked in, so you only
need to rerun that to change the clock's font or sizes.

## Credits

See the [main README](../README.md).
