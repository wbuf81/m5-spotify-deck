# Putting the Spotify deck back on the board

Read this when you have been away, or when the board has been flashed with
something else and you want the Spotify controller back.

The full story is in the top-level `README.md`. This file is the short path,
with the exact values for the unit on the desk.

---

## The one-liner

```sh
cd ~/Vibecoding/m5stackfirmware
HOMEBREW_PREFIX=/opt/homebrew pio run -e esp32 -t upload \
  --upload-port /dev/cu.usbserial-5B212245181
```

Takes about 18 seconds. That is the whole job **if the device still has its NVS
configuration**, which survives reflashing. Nothing else is needed.

---

## What the board keeps, and what it does not

| Thing | Survives a reflash? |
|---|---|
| WiFi credentials, Spotify tokens (NVS) | **Yes** |
| Album art cache on the SD card | **Yes** |
| Enabled views, settings (NVS) | **Yes** |
| The firmware itself | No, obviously |

NVS is only lost if something erases the whole flash. `pio run -t erase` does
that. A plain upload does not.

So the usual case is: flash, and it comes back playing.

---

## If it boots into SETUP MODE

That means NVS is empty. The device brings up its own WiFi network and waits.

1. Join **`M5SPOTIFY-SETUP`** from a phone. The screen shows a QR code.
2. Open **`192.168.4.1`**.
3. Enter the WiFi and Spotify details.
4. It reboots into player mode and stores everything in NVS.

Hold the **left button through power-on** to force setup mode at any time.

### Developer path instead

If you would rather compile the credentials in, create
`src/config/secrets.h` from `src/config/secrets.h.example`:

```sh
python3 tools/get_refresh_token.py
```

**Run that in your own terminal, not through an assistant.** It prompts for the
sensitive values itself and writes the file with mode 0600. `secrets.h` is
gitignored. Compiled secrets act as the fallback for any field the portal has
not stored.

---

## Checking it worked

Watch the boot banner:

```sh
HOMEBREW_PREFIX=/opt/homebrew pio device monitor -p /dev/cu.usbserial-5B212245181
```

A healthy boot says:

```
=== m5 spotify ===
reset     : power-on
heap free : 222548 bytes
largest   : 110580 bytes contiguous
sd card   : mounted at 25MHz
display   : 320x240
==================
[net] wifi connected, ip=..., rssi=-49
[net] token refresh: HTTP 200
[net] GET https://api.spotify.com/v1/me/player -> 200
[net] link -> online
```

Things worth reading in that banner:

- **`reset : PANIC (crash)`** with a crash streak means it is rebooting on its
  own. That is a real fault.
- **`sd card : not mounted`** means covers will not render. The card must be
  **FAT32 and 16 GB or smaller**.
- **`-> 204`** instead of `200` is not a fault. It means Spotify has no active
  playback session anywhere on the account. The deck shows the idle screen with
  the sleeping dog. Start playing anything and it returns.

---

## The tests, if you have changed something

```sh
HOMEBREW_PREFIX=/opt/homebrew pio run -e native      # emulator builds
HOMEBREW_PREFIX=/opt/homebrew pio test -e test       # 26 host unit tests
python3 tools/visual_tests.py                        # 33 pixel assertions
```

`visual_tests.py` sometimes reports a retry after an M5GFX `Panel_sdl` startup
race. That is upstream, host-only, and harmless. It can raise a macOS crash
dialog.

---

## Watching the network while it runs

Serial capture needs PlatformIO's bundled Python, which is the one with
`pyserial`:

```sh
/opt/homebrew/Cellar/platformio/6.1.19_2/libexec/bin/python3
```

The version in that path changes when Homebrew upgrades PlatformIO.

**Opening the serial port reboots the board.** You cannot attach to look at a
fault that is happening right now — attaching destroys it. Start the capture
first, then reproduce.

---

## Known-good state

Commit `e3c2935`, verified on hardware on 2026-08-10:

- 19 consecutive album-art downloads with no failure, including at a largest
  contiguous block of 19,444 bytes, which is the figure that used to fail every
  time.
- No TLS errors, no crashes, over about 55 minutes of running.
- 33/33 visual checks, 26/26 unit tests.

**One thing is still open.** A single artwork download failed early once, during
the transfer, and never repeated. The cause is unknown. Every download exit path
now reports its own cause in the log, so if "no artwork" comes back, the serial
log will say which step failed and how many bytes it got. Start there.
