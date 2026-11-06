# BUSY C6

A desk status light on a [Waveshare ESP32-C6-Touch-LCD-1.9](https://docs.waveshare.com/ESP32-C6-LCD-1.9),
showing the [BUSY Bar](https://busy.bar)'s 13 status animations in LED dots. Swipe to change status, tap to
change the view, and turn the board over to flip the picture.

Open the site in **Chrome or Edge on a computer** (it uses Web Serial, which Safari and Firefox don't have),
plug the board in with its USB cable, and press **Connect**.

**https://nurnaufal321.github.io/busy-c6/**

- **Sync clock:** the board has no clock battery, so it gets the time from your computer through this
  page. It stays in sync while the page is open, keeps time by itself while it has power, and forgets it
  when it loses power.
- **Status and view:** the same choices as swiping and tapping on the board.
- **Custom status:** type one or two lines, pick colours, send. It's a still picture, kept on the board,
  and joins the end of the swipe order.

## On the board

| Do this | To get this |
|---|---|
| Swipe left / right | Next / previous status |
| Tap | Next view: Original, A. Tall, B. Pan, C. Glow, D. Clock |
| Turn it over | The picture flips |

It remembers the status and view after power-off.

## Firmware

The [`firmware`](firmware) folder has the ESP-IDF project and how to flash it. It has to be flashed once
over USB. [`PROTOCOL.md`](PROTOCOL.md) documents how the page and board talk.

## Running the page locally

It's a static page with no build step:

```
python3 -m http.server 8000
```

then open `http://localhost:8000` in Chrome.

## Credits

Unofficial. Not affiliated with BUSY.

- Status animations and themes come from [BUSY Bar's firmware](https://github.com/busy-app/busybar-firmware):
  animations under CC-BY-SA-4.0, themes under GPL-2.0-or-later, and the pixel fonts in [`fonts`](fonts) under
  OFL-1.1.
- The clock font is Roboto (Apache-2.0).
- Pin assignments and panel setup come from Waveshare's
  [examples](https://github.com/waveshareteam/ESP32-C6-LCD-1.9).
