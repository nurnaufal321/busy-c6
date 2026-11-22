# Protocol

The page and the firmware talk in text lines (ending in a newline) over the board's built-in USB port,
which shows up to the browser as a serial port with USB vendor ID `0x303A`.

## Page to board

| Line | Meaning |
|---|---|
| `HELLO` | Ask for the current state. |
| `BUSYTIME <unix seconds> <UTC offset seconds>` | Set the clock. The board shows local time using the offset, which it keeps across resets. |
| `STATUS <index>` | Show that status. |
| `LAYOUT <index>` | Use that view: 0 Original, 1 Clock. |
| `CUSTOM ADD <name> <4608 hex digits> <x> <y>` | Save a still 72×16 picture as the custom status `<name>` (capitals, digits and `_`, up to 23) and show it, replacing one of that name. Four digits per pixel, the RGB565 value as written (`F800` is red), row by row. `x` / `y` 1 moves it half an LED right / down, so it can sit exactly centred. Up to 8 are kept; past that it's ignored. Firmware 1.2.0 and later. |
| `CUSTOM DELETE <name>` | Delete that custom status. Firmware 1.2.0 and later. |
| `CUSTOM GET <name>` | Ask for that custom status's picture; the board answers `BUSYCUSTOM <name> <hex> <x> <y>`. Firmware 1.2.0 and later. |
| `CUSTOM <4608 hex digits> [<x> <y>]` | Older pages: the same as `CUSTOM ADD CUSTOM …`. The offsets need firmware 1.1.0 or later. |
| `CUSTOM CLEAR` | Delete every custom status. |

## Board to page

```
BUSY names=busy,keep_out,...,BACK_AT_3 status=<i> layout=<i> custom=<n> time=<0|1> fw=<version>
```

Sent in reply to any command, and whenever a swipe or tap changes something. `names` lists the built-in statuses, then the `custom` saved custom statuses (before firmware 1.2.0,
`custom` is 0 or 1 and the one custom status is called `custom`). `time=1` means the clock
has been set since the board last lost power. While `time=0` the board may still show a time: it saves
the time every 30 seconds and restores it after a power cut, so that time is behind by however long the
board was off. `fw=` is the firmware version (`git describe` of the build), which the page compares with
the build published next to it. Everything else on the port is the firmware's ordinary
log output and starts with something other than `BUSY `.

## Notes

- The USB port can reset the chip through its DTR and RTS lines, so the page clears both when it opens
  the port.
- A custom status takes about 2 seconds to arrive, because the firmware reads the port between frames.
- The page flashes firmware with [esptool-js](https://github.com/espressif/esptool-js) on the same port,
  after closing its own connection. It writes the app and the `status` partition, plus the bootloader and
  partition table for a full install, and never the `nvs` partition.
- Only one program can hold the port. Disconnect the page before flashing, and close any serial monitor
  before connecting.
