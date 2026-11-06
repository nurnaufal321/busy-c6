# Protocol

The page and the firmware talk in text lines (ending in a newline) over the board's built-in USB port,
which shows up to the browser as a serial port with USB vendor ID `0x303A`.

## Page to board

| Line | Meaning |
|---|---|
| `HELLO` | Ask for the current state. |
| `BUSYTIME <unix seconds> <UTC offset seconds>` | Set the clock. The board shows local time using the offset, which it keeps across resets. |
| `STATUS <index>` | Show that status. |
| `LAYOUT <index>` | Use that view: 0 Original, 1 Tall, 2 Pan, 3 Glow, 4 Clock. |
| `CUSTOM <4608 hex digits>` | Save a still 72×16 picture as the custom status and show it. Four digits per pixel, the RGB565 value as written (`F800` is red), row by row. |
| `CUSTOM CLEAR` | Remove the custom status. |

## Board to page

```
BUSY names=busy,keep_out,... status=<i> layout=<i> custom=<0|1> time=<0|1>
```

Sent in reply to any command, and whenever a swipe or tap changes something. `time=1` means the clock
has been set since the board last lost power. Everything else on the port is the firmware's ordinary
log output and starts with something other than `BUSY `.

## Notes

- The USB port can reset the chip through its DTR and RTS lines, so the page clears both when it opens
  the port.
- A custom status takes about 2 seconds to arrive, because the firmware reads the port between frames.
- Only one program can hold the port. Disconnect the page before flashing, and close any serial monitor
  before connecting.
