# M5Stack Core Basic v2.7 — platform reference

Everything a new project needs to build for this board, flash it, talk to it at
runtime, and avoid the traps this one fell into.

This file is meant to be copied into the new project. It does not depend on
anything in this repository.

Every number here was measured on the physical unit on the desk, not read off a
datasheet. Where a figure came from the datasheet or from upstream docs, it says
so.

---

## 1. The board

| Item | Value |
|---|---|
| Chip | ESP32-D0WDQ6-V3 rev3, 2 cores at 240 MHz |
| Flash | 16 MB |
| PSRAM | none |
| RAM, free heap at boot | 222,548 bytes |
| RAM, largest contiguous block at boot | 110,580 bytes |
| Display | 320 x 240, ILI9342C over SPI |
| SD card | shares the SPI bus with the display |
| PMIC | IP5306 at I2C address 0x75 |
| Serial port on this Mac | `/dev/cu.usbserial-5B212245181` |
| Serial speed | 115200 |

**No PSRAM is the fact that shapes every design.** A full 320x240 16-bit
framebuffer is 153 KB. You cannot hold one. Draw straight to the panel, or into
small sprites, and repaint only what changed.

---

## 2. Toolchain

PlatformIO Core 6.1.19, installed by Homebrew.

**Every `pio` command needs `HOMEBREW_PREFIX` set, or it cannot find its
toolchain:**

```sh
HOMEBREW_PREFIX=/opt/homebrew pio run -e esp32
```

Put it in the shell profile and the problem disappears.

### Minimum `platformio.ini` for a new firmware project

```ini
[env:esp32]
platform = espressif32
board = m5stack-core-esp32
framework = arduino
lib_deps = m5stack/M5Unified@^0.2.7

; The Arduino ESP32 core appends -std=gnu++11, which beats anything set in
; [env]. Unset theirs, then set yours, or C++14 features fail to compile.
build_unflags = -std=gnu++11
build_flags = -std=gnu++14

; The default partition table gives the app 1.31 MB. WiFi, mbedTLS and the root
; CA bundle alone fill 96 percent of that. The board has 16 MB, so use it.
board_build.partitions = huge_app.csv
board_upload.flash_size = 16MB
board_build.flash_size = 16MB

monitor_speed = 115200
monitor_filters = esp32_exception_decoder
upload_speed = 1500000
```

`monitor_filters = esp32_exception_decoder` turns a crash backtrace into file
and line numbers. Set it before you need it.

### Build and flash

```sh
HOMEBREW_PREFIX=/opt/homebrew pio run -e esp32
HOMEBREW_PREFIX=/opt/homebrew pio run -e esp32 -t upload \
  --upload-port /dev/cu.usbserial-5B212245181
```

Flashing takes about 18 seconds. A charge-only USB-C cable presents no serial
port at all, so if `/dev/cu.usbserial-*` is missing, change the cable first.

---

## 3. Serial

### Reading the log

`pio device monitor` works. For scripted capture, use **PlatformIO's bundled
Python**, which is the one that has `pyserial`:

```
/opt/homebrew/Cellar/platformio/6.1.19_2/libexec/bin/python3
```

The version number is in that path. It changes when Homebrew upgrades
PlatformIO, so glob it rather than hard-coding it.

### Opening the port resets the board

This cost real debugging time. Attaching to `/dev/cu.usbserial-*` toggles
DTR/RTS, and the board reboots. **You cannot observe a live fault by attaching
after you see it** — attaching destroys the state you wanted.

Plan for it:

- Start the capture first, then reproduce.
- Or accept the reset and wait for the fault to repeat.
- A reset from attaching shows as `reset : power-on` in the boot banner. A real
  crash shows as `reset : PANIC (crash)`. Read that line before concluding
  anything.

### A boot banner is worth writing

This project prints reset reason, consecutive crash count, heap free, largest
contiguous block, SD state and PMIC registers on every boot. It answered more
questions than any other single thing. Copy the idea.

---

## 4. Memory, and why it is the hard limit

Free heap is not the number that matters. **Largest contiguous block is.**

Measured on this board, running a WiFi and TLS workload with sprites:

| Moment | Free heap | Largest block |
|---|---|---|
| Boot | 222,548 | 110,580 |
| Steady state, light view | ~104,000 | 40,948 |
| Steady state, heavy view | ~82,000 | 19,444 |

WiFi, TLS and sprite churn fragment the heap within the first minute, and it
never recovers. A design that works at boot can fail ten minutes later with
100 KB still free.

### Rules that came out of that

1. **Allocate sprites once, in the view's `enter()`, never per frame.** Free
   them in `release()`.
2. **Never call `new` for a large buffer at runtime.** An uncaught `bad_alloc`
   on the ESP32 becomes `abort()`, which is a reboot loop.
3. **Do not reserve a big buffer at boot to avoid churn.** That was tried here.
   It removed the churn and took the memory permanently, and mbedTLS could no
   longer complete a handshake. The crash moved rather than went away.
4. **Stream large files rather than buffering them.** LovyanGFX's
   `drawJpgFile` decodes straight off the card with no large allocation.
5. **Log free heap and largest block periodically.** Add a loud warning below a
   floor value. This project warns under 40,000 bytes.

### TLS is the biggest single consumer

An established mbedTLS session allocates a **~16.7 KB input buffer and a
~16.7 KB output buffer, each of which must be contiguous.** So roughly 34 KB of
contiguous heap per session.

Two established sessions do not fit on this board. Measured, mid-failure:

```
free 81712,  largest block 19444  -> handshake fails, two sessions live
free 122348, largest block 32756  -> handshake fails, one session live
```

The second line is the important one. Even one session fails once the largest
block falls near 32 KB, because the first buffer is carved out and the remainder
is just under what the second needs.

**Practical rules:**

- Hold **at most one TLS session at a time**, for the traffic that genuinely
  needs it.
- Send bulk or public data over plain HTTP. There is no handshake and no
  contiguous allocation, and it is far faster. Downloads here went from
  1.3–4.5 s to 0.4–1.0 s.
- If a handshake returns `-32512`, that is `MBEDTLS_ERR_SSL_ALLOC_FAILED`. It is
  a memory problem, not a network or certificate problem.

### An Arduino `HTTPClient` trap

`HTTPClient` keeps a raw pointer to the `WiFiClient` it was given, and **never
clears it** — not in `end()`, not in `disconnect()`. Delete the client while the
`HTTPClient` still exists and the pointer dangles. The crash arrives later, in
`HTTPClient::connected()` called from `setTimeout()`, which runs before
`begin()` can rebind it. It looks like a fault in innocent code.

Own the two together, and destroy the `HTTPClient` first, while its client is
still alive.

---

## 5. Display and graphics

Use **M5Unified** (`m5stack/M5Unified`), which brings M5GFX and LovyanGFX.

```cpp
#include <M5Unified.h>
auto cfg = M5.config();
M5.begin(cfg);
M5.Display.fillScreen(TFT_BLACK);
```

### Sprite buffers are byte-swapped

**This is the single most confusing trap on the board, and it bit this project
twice.**

`M5Canvas` sprite buffers store RGB565 **byte-swapped**, ready for SPI. Copying
raw texels between two buffers is safe either way. But **any arithmetic on a
raw texel must swap first and swap back**, or you paint rainbow noise.

```cpp
inline uint16_t unswap(uint16_t c) { return (c >> 8) | (c << 8); }
```

Anything that averages, fogs, tints or quantises pixels needs this.

### The SPI bus is shared with the SD card

The LCD and the SD card are on one bus. **Touching the SD card inside a display
transaction deadlocks the UI thread.** If a `startWrite()` is open, do not read
a file. Sample what you need before the transaction begins.

### Reading pixels back off the panel is unreliable

ILI9342C readback is slow and does not work on every unit. Decode into an
off-screen sprite and read that instead. Sprite reads are plain memory.

---

## 6. SD card

- **FAT32, 16 GB or smaller.** Larger cards and exFAT do not mount.
- Arduino's `SD` library registers with the ESP-IDF VFS at **`/sd`**, so
  ordinary `fopen`, `stat`, `rename` and `opendir` work on paths like
  `/sd/art/cover.jpg`.
- Mounts at 25 MHz on this unit.
- Mount state is read once at boot. A card that stops responding later still
  reports as mounted, so a read failure and a missing card look different in
  code but identical on screen. Say which one you mean in the log.

---

## 7. Power

Read this before diagnosing anything battery-related.

- **The M5-Bottom module has a physical 0/1 battery isolation switch, and it
  ships set to 0.** With it off, the device dies the instant USB is unplugged,
  and **no register can distinguish that from having no battery at all** — the
  gauge sits at a confident 100 percent and the charger reports charging
  forever. Check the switch first.
- The PMIC is an **IP5306 at 0x75**. It cannot report pack voltage
  (`getBatteryVoltage()` returns 0 mV) and reports charge in **25 percent
  steps**. Design battery UI as four levels, never a percentage.
- **`M5.Power.isCharging()` is useless on this board.** It mirrors the
  charger-enable bit, so it reads true forever, on USB or off. Do not build
  behaviour on it.
- **M5Unified never latches the boost converter on.** The chip boots with
  `SYS_CTL0 = 0x31`: the boost is enabled but not latched, so the 5 V rail
  collapses the moment USB is removed and the ESP32 dies mid-instruction. The
  battery is not the problem. M5Stack's own library writes these bits at
  startup; M5Unified does not.

  Fix it with a **read-modify-write on each boot**, not a literal store — the
  other bits in these registers matter:

  ```
  SYS_CTL0 (0x00): set boost-keep-on and auto-power-on-load
  SYS_CTL1 (0x01): set boost-switchable, clear low-load-shutdown
  ```

  On this unit that lands at `SYS_CTL0 = 0x37` and `SYS_CTL1 = 0x3D`. Treat
  those as the expected result to verify, not as values to write. The registers
  are volatile and revert whenever the PMIC loses power, so this runs at every
  boot. See `src/platform/esp32/Esp32Power.cpp` for a working implementation.

---

## 8. WiFi

- **2.4 GHz only.** The ESP32 cannot see 5 GHz. A 5 GHz-only network is the most
  common cause of a device stuck on "connecting".
- Store credentials in NVS (`Preferences`) rather than compiling them in. A
  first-boot captive portal is a good pattern: the device brings up a soft AP,
  serves a form, saves to NVS and reboots.

---

## 9. Getting data onto the device at runtime

Three transports work on this board. They are listed in the order worth trying.

### A. Device polls an HTTP endpoint (recommended)

The device is an HTTP client and asks a host or a service for state on an
interval. This is what the Spotify firmware does.

**Why it is first choice:** no inbound firewall problem, no listening socket to
harden, and the device controls its own timing so it can back off. It survives
the host restarting.

Costs and traps:

- Keep the connection alive between polls. A fresh connection per poll cost
  about 930 ms here against roughly 200 ms reused.
- Keep-alive state lives on `HTTPClient`, not on the `WiFiClient` it wraps, so
  construct one and reuse it.
- **A `204` or `304` carries no body and no `Content-Length`.** `getSize()`
  returns `-1`, and `getString()` on `-1` reads until the peer closes. With
  keep-alive on, a server that never closes will hang the task **forever**.
  Guard on the status code before reading a body. This wedged the device here
  and the symptom was a screen frozen on "connecting".
- Servers drop idle keep-alive sockets after about 60 seconds. The next request
  writes into a dead socket and waits out the read timeout, and it does not heal
  on its own. Detect the transport error, destroy the session, and retry once on
  a fresh one.
- Poll on a **separate task** from the render loop.

### B. Device runs an HTTP server and the host pushes

The device listens, and something on the network POSTs to it.

Use it when the host must push immediately and cannot wait for a poll. Costs:

- The device needs a stable address. Reserve a DHCP lease or use mDNS.
- Anything on the LAN can reach it. It has no authentication unless you write
  some.
- A blocked or slow client ties up the device. Bound every read.

### C. USB serial from the Mac

Simplest of all, and the right answer when the device is tethered anyway.

- 115200 works. Higher rates are possible but 115200 has never been the
  bottleneck here.
- **Opening the port resets the board** (see section 3). A host program that
  reconnects will reboot the device every time, so design the protocol to
  survive a restart, or hold the port open.
- Use a framed protocol with a length prefix. Serial gives you a byte stream and
  no message boundaries, and the ESP32's boot messages share the same line.

### Sending images

- **Send JPEG, not raw pixels.** A 320x240 raw RGB565 frame is 153 KB and does
  not fit in heap. A cover-sized JPEG is about 40 KB and streams.
- **Write it to the SD card as you receive it, then decode from the file.**
  `drawJpgFile` decodes off the card with no large allocation. Buffering the
  whole image in RAM first is what fails.
- Write to a temporary name and rename on success, so a truncated transfer never
  becomes a file that fails to decode later.
- Downscaling is free: `drawJpgFile(path, x, y, w, h, 0, 0, 0.0f, 0.0f)` with a
  scale of `0.0` fits the image to the box, including non-power-of-two ratios.

---

## 10. Tasks and the watchdog

- Run the network on its own FreeRTOS task, separate from rendering.
- **Subscribe only the render loop to the hardware watchdog.** Subscribing the
  network task put this device into a reboot loop every 30 seconds, because that
  task legitimately blocks on network I/O. A watchdog is for code that must
  never block. Give the network task a heartbeat counter instead, and check it
  from somewhere else.
- Count consecutive abnormal resets in NVS and print the streak at boot. A
  device that quietly reboots every 20 seconds looks exactly like a slow one.

---

## 11. Emulator, if you want one

This project builds the same source for macOS against SDL2, using M5GFX's
`Panel_sdl`. It is worth the setup: the UI can be developed without the board,
and it supports scripted screenshot tests.

Two things to know:

- The device build must be excluded from the host build and the reverse, with
  `build_src_filter`.
- **`Panel_sdl` has an unsynchronised startup race upstream.** It segfaults on
  start now and then. It is host-only and the device build does not contain
  `Panel_sdl` at all, but it will produce macOS crash dialogs. Retry the run.

---

## 12. Checklist for a new firmware project

1. `platformio.ini` from section 2, with `huge_app.csv`.
2. Boot banner: reset reason, crash streak, free heap, largest block, SD, PMIC.
3. Periodic heap log with a floor warning.
4. Latch the IP5306 boost bits at boot, by read-modify-write, if it must run on
   battery.
5. Network on its own task. Watchdog on the render loop only.
6. Sprites allocated in `enter()`, freed in `release()`, never per frame.
7. One TLS session at most. Plain HTTP for bulk.
8. Guard every body read on the status code.
9. Stream files to SD, decode from the file.
10. Start the serial capture before reproducing a fault, not after.
