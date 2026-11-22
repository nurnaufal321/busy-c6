# BUSY C6

A desk status light on a [Waveshare ESP32-C6-Touch-LCD-1.9](https://docs.waveshare.com/ESP32-C6-LCD-1.9),
showing the [BUSY Bar](https://busy.bar)'s 13 status animations in LED dots. Swipe to change status, tap to
change the view, and turn the board over to flip the picture.

Open the site in **Chrome or Edge on a computer** (it uses Web Serial, which Safari and Firefox don't have),
plug the board in with its USB cable, and press **Connect**.

**https://nurnaufal321.github.io/busy-c6/**

- **Clock:** the board has no clock battery, so it gets the time from your computer through this
  page, by itself: when the page connects, then every minute. The **Clock synced** chip shows when it
  last did. The board keeps time by itself while it has power. It saves the
  time every 30 seconds, so after being unplugged it carries on from there, behind by however long it was
  off, with a steady colon until the page syncs it again.
- **Status and view:** the same choices as swiping and tapping on the board.
- **Custom status:** type one or two lines, pick colours, and **Save to board**. It's a still picture in
  the style of the built-in statuses, named after its text. The board keeps up to 8; they join the end
  of the Status list and the swipe order, and each has a ✕ there to delete it. Saving the same text
  again replaces it.
- **Firmware:** shows the board's version and the latest build, and updates it over the cable. **Full
  install** sets up a new board. Status, view and custom status are kept.

## On the board

| Do this | To get this |
|---|---|
| Swipe left / right | Next / previous status |
| Tap | Switch view: Original or Clock |
| Turn it over | The picture flips |

It remembers the status and view after power-off.

## Firmware

The [`firmware`](firmware) folder has the ESP-IDF project. Every push to `main` builds it in GitHub
Actions and publishes it with the page (in `firmware-dist/`), so you can install or update it from the
page's **Firmware** card without ESP-IDF. The site has to be published by GitHub Actions
(Settings → Pages → Source → GitHub Actions). [`PROTOCOL.md`](PROTOCOL.md) documents how the page and board talk.

## Versions

The firmware has a version number, `MAJOR.MINOR.PATCH`, kept in
[`firmware/version.txt`](firmware/version.txt). The board reports it, and the page shows it next to
**Connected**: green when it's up to date, or **Update available** with what's new.

To release a firmware change:

1. Bump `firmware/version.txt`:
   - **PATCH** (1.0.**1**) for fixes.
   - **MINOR** (1.**1**.0) for new features, such as a new status or view.
   - **MAJOR** (**2**.0.0) for a change that needs a full install, or that the page has to be updated
     for, such as a new protocol or partition table.
2. Add a `## <version>` section to [`CHANGELOG.md`](CHANGELOG.md) with a line per change. The page shows
   these lines when your board is behind.
3. Push to `main`. GitHub Actions builds it, publishes it with the page, and tags it `v<version>`.

Changes to the page alone don't need a new version. Changing the firmware without bumping the version
fails the build.

## Running the page locally

It's a static page with no build step:

```
python3 -m http.server 8000
```

then open `http://localhost:8000` in Chrome. To try the Firmware card locally, build the firmware and run
`python3 firmware/tools/make_dist.py` first.

## Credits

Unofficial. Not affiliated with BUSY.

- Status animations and themes come from [BUSY Bar's firmware](https://github.com/busy-app/busybar-firmware):
  animations under CC-BY-SA-4.0, themes under GPL-2.0-or-later, and the pixel fonts in [`fonts`](fonts) under
  OFL-1.1.
- The clock font is Roboto (Apache-2.0).
- Pin assignments and panel setup come from Waveshare's
  [examples](https://github.com/waveshareteam/ESP32-C6-LCD-1.9).
