# ESP32-S3 Smart Camera

**English** | [简体中文](README_CN.md)

A complete, self-contained smart camera built on the ESP32-S3 (N16R8): OV2640 camera,
1.44" ST7735S TFT viewfinder, joystick menu, microSD storage, WiFi web dashboard,
on-device AVI recording, photo upload and OTA updates.

```
┌──────────────────────────────────────────────┐
│  TFT 128×128 viewfinder + Chinese UI         │
│  OV2640 stills up to UXGA 1600×1200          │
│  Digital zoom 1–4× · manual shutter · anti-shake │
│  microSD photos / thumbnails / AVI clips     │
│  WiFi dashboard: stream, capture, gallery,   │
│  settings, status, firmware update           │
└──────────────────────────────────────────────┘
```

---

## Table of Contents

- [Quick Start](#quick-start)
- [Features](#features)
- [Hardware Requirements](#hardware-requirements)
- [Wiring](#wiring)
- [Software Setup](#software-setup)
- [Build and Flash](#build-and-flash)
- [First Boot and WiFi Provisioning](#first-boot-and-wifi-provisioning)
- [Using the Camera](#using-the-camera)
- [Web Interface](#web-interface)
- [Configuration Reference](#configuration-reference)
- [SD Card Layout](#sd-card-layout)
- [Design Notes](#design-notes)
- [Troubleshooting](#troubleshooting)
- [FAQ and Limitations](#faq-and-limitations)
- [Project Structure](#project-structure)
- [License](#license)

---

## Quick Start

```bash
git clone https://github.com/luoleiyong/esp32-s3-smart-camera.git
```

1. Open `ESP32S/ESP32S.ino` in Arduino IDE.
2. Apply the [board settings](#board-settings) (16 MB flash, OPI PSRAM, OTA-capable partition).
3. Install the [libraries](#software-setup).
4. Flash at 921600 baud and open the serial monitor at 115200.
5. On first boot, join `ESP32-S3-CAM-Setup` / `12345678` and open `http://192.168.4.1`
   to provision WiFi (see [First Boot and WiFi Provisioning](#first-boot-and-wifi-provisioning)).

---

## Features

### Imaging

| Feature | Details |
| --- | --- |
| Live preview | OV2640 to 128×96 preview area on the TFT, ~15 fps (66 ms interval) |
| Photo capture | JPEG, up to UXGA 1600×1200, stored on microSD |
| Resolutions | QVGA / VGA / SVGA / XGA / SXGA / UXGA (TFT menu omits VGA) |
| Quality | JPEG quality 4–15 (web), presets Q5/Q8/Q10/Q15/Q20 (TFT) |
| Digital zoom | 1× / 2× / 3× / 4×, applied to both preview and saved photos |
| Manual exposure | Auto or manual; shutter `aec_value` 0–1200, gain 0–30 |
| Anti-shake | 3-shot burst, keeps the frame with the largest JPEG bitstream |
| Scene presets | Standard / Night boost / Backlight compensation |
| Self-timer | Off / 2 s / 5 s / 10 s; preview keeps refreshing during the countdown |
| Timestamp watermark | Optional `YYYY-MM-DD HH:MM:SS` (+ zoom factor), white on black outline |
| Browser recording | WebM recorded in the browser from the live stream (`MediaRecorder`) |
| On-device recording | MJPEG frames muxed into AVI on the SD card, 5/10/15 fps |

### Device and connectivity

| Feature | Details |
| --- | --- |
| TFT UI | 1.44" ST7735S 128×128, white theme, Chinese menu and status text |
| Controls | 5-way joystick + IO0 button gestures |
| LED status | Onboard WS2812: idle breath, capture flash, AP blink, error blink |
| WiFi | Station mode + AP captive portal for first-time provisioning (2.4 GHz only) |
| mDNS | `http://esp32s3cam.local` |
| Time | NTP with UTC+8 offset |
| Web dashboard | Stream, remote capture, gallery, settings, runtime status |
| Photo upload | Multipart HTTP POST, optional auto-upload; Telegram `sendPhoto` compatible |
| OTA | ArduinoOTA and browser upload at `/update` |
| Self-healing | Camera re-init after repeated frame failures; SD remount + retry |
| Dual core | Preview on Core 1, UI/web/OTA on Core 0 |

### Optional advanced switches (default off)

| Switch | Effect |
| --- | --- |
| `JPEG preview` | Sensor outputs JPEG, decoded by TJpgDec. Much less PSRAM traffic and heat; digital zoom still works |
| `Hardware window zoom` | OV2640 `set_res_raw()` crop for photos and MJPEG stream: full-resolution zoom with no re-encode. Experimental; 4:3 presets only |

---

## Hardware Requirements

### Bill of Materials

| Item | Specification | Notes |
| --- | --- | --- |
| MCU board | ESP32-S3 **N16R8** | 16 MB flash, 8 MB **OPI** PSRAM required |
| Camera | OV2640 module | 8-bit parallel + SCCB; ESP32-S3-EYE style pinout |
| Display | ST7735S 1.44" TFT | 128×128, SPI, 3.3 V logic |
| Joystick | 2-axis analog + push button | e.g. KY-023 / PS2 thumbstick |
| Storage | microSD card + adapter | FAT32, SD_MMC 1-bit mode |
| Button | IO0 (BOOT) button | Usually onboard; external button to GND works |
| RGB LED | WS2812 on GPIO48 | Usually onboard; optional |
| Power | 5 V / 1 A USB supply | Camera + SD current spikes; avoid weak ports |

> The firmware is configured for **16 MB flash + 8 MB OPI PSRAM**. A board with less
> PSRAM or QSPI PSRAM will fail camera initialization or run out of memory.

---

## Wiring

### Complete Pin Map

#### OV2640 camera (8-bit parallel + SCCB)

| Camera signal | ESP32-S3 GPIO | Notes |
| --- | --- | --- |
| XCLK | 15 | 20 MHz master clock |
| SIOD / SDA | 4 | SCCB data |
| SIOC / SCL | 5 | SCCB clock |
| Y9 / D7 | 16 | |
| Y8 / D6 | 17 | |
| Y7 / D5 | 18 | |
| Y6 / D4 | 12 | |
| Y5 / D3 | 10 | |
| Y4 / D2 | 8 | |
| Y3 / D1 | 9 | |
| Y2 / D0 | 11 | |
| VSYNC | 6 | |
| HREF | 7 | |
| PCLK | 13 | |
| PWDN | — | Not connected (`-1`) |
| RESET | — | Not connected (`-1`) |
| VCC / GND | 3.3 V / GND | Decouple close to the module |

> Different OV2640 breakout boards label the data bus differently. Match **Y2…Y9**
> in the table above, not the silkscreen `D0…D7` order of another board.

#### ST7735S TFT (SPI)

| TFT signal | ESP32-S3 GPIO | Notes |
| --- | --- | --- |
| CS | 1 | Chip select |
| DC (A0) | 19 | Data / command |
| RST | 20 | Reset |
| SDA / MOSI | 21 | SPI data in |
| SCL / SCLK | 47 | SPI clock, 20 MHz |
| BL / LED | 45 | Backlight, driven high |
| VCC | 3.3 V | Do not use 5 V logic |
| GND | GND | |

> This project does **not** use MISO: the display is write-only.
> SPI runs at **20 MHz**. 27 MHz was unstable on the tested module (the panel stayed white);
> raise it only if your wiring is short and your panel is known good.

#### Joystick

| Joystick pin | ESP32-S3 GPIO | Notes |
| --- | --- | --- |
| VRX | 14 | ADC input |
| VRY | 3 | ADC input |
| SW | 46 | Active low, internal pull-up |
| +5V / VCC | 3.3 V | Use 3.3 V, not 5 V |
| GND | GND | |

> GPIO46 is a strapping pin. Do not hold the joystick pressed while resetting or powering up.

#### microSD card (SD_MMC, 1-bit)

| SD signal | ESP32-S3 GPIO | Notes |
| --- | --- | --- |
| CLK | 39 | |
| CMD | 38 | |
| D0 / DAT0 | 40 | 1-bit mode; DAT1–DAT3 unused |
| VCC | 3.3 V | |
| GND | GND | |

> Use a FAT32-formatted card. Keep the SD wires short; long flying leads cause mount
> failures and write errors at 40 MHz.

#### Button and RGB LED

| Signal | ESP32-S3 GPIO | Notes |
| --- | --- | --- |
| Button | 0 (IO0) | To GND; internal pull-up; BOOT strapping pin |
| WS2812 | 48 | Onboard RGB LED, optional |

### Connection Overview

```
                    ┌─────────────────────────────────┐
                    │        ESP32-S3 (N16R8)         │
                    │                                 │
  OV2640  ──────────┤ 8-bit parallel (Y2..Y9)         │
  (camera)          │ + XCLK / VSYNC / HREF / PCLK     │
                    │ + SCCB (SDA 4, SCL 5)            │
                    │                                 │
  ST7735S ──────────┤ SPI: CS 1, DC 19, RST 20,       │
  (TFT)             │      MOSI 21, SCLK 47, BL 45     │
                    │                                 │
  microSD ──────────┤ SD_MMC 1-bit: CLK 39, CMD 38,   │
  (SD_MMC)          │               D0 40             │
                    │                                 │
  Joystick ─────────┤ VRX 14 (ADC), VRY 3 (ADC),      │
                    │ SW 46 (GPIO)                    │
                    │                                 │
  Button  ──────────┤ IO0 (to GND)                    │
  WS2812  ──────────┤ GPIO48 (onboard)                │
                    └─────────────────────────────────┘
```

### Wiring Notes

1. **Common ground**: board, camera, TFT, joystick and SD adapter must share GND.
2. **3.3 V logic only**: all peripherals are 3.3 V. Do not feed 5 V into GPIOs.
3. **Keep buses short**: the camera parallel bus and the 20 MHz SPI bus are the most
   noise-sensitive. Use short, equal-length leads; avoid breadboard jumpers longer than ~10 cm.
4. **Power**: use a stable 5 V / 1 A supply. Camera start-up and SD writes draw current
   spikes; a weak USB port causes brown-out resets and "Camera FAIL" on the serial log.
5. **Pin usage**: the firmware uses GPIO 0, 1, 3–21, 38–40, 45–48. Avoid reusing these
   for other peripherals.
6. **Strapping pins**: IO0 (button) and IO46 (joystick SW) affect boot mode. Do not hold
   them low during reset/power-up.

---

## Software Setup

### Arduino IDE

1. Install **Arduino IDE 2.x**.
2. Add the ESP32 board index in *Preferences → Additional boards manager URLs*:

   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

3. Install **esp32 by Espressif Systems** (tested with **3.3.8**; 3.x is required).
4. Install the following libraries from *Library Manager*:

   | Library | Tested version | Purpose |
   | --- | --- | --- |
   | Adafruit GFX Library | 1.12.6 | Graphics primitives |
   | Adafruit ST7735 and ST7789 Library | 1.11.0 | ST7735S driver |
   | U8g2_for_Adafruit_GFX | 1.8.0 | Chinese font rendering on TFT |
   | TJpg_Decoder | 1.1.0 | JPEG decode (preview, gallery, zoom, watermark) |

   The following come with the ESP32 core and need no separate install:
   `WiFi`, `WebServer`, `SD_MMC`, `SPI`, `Preferences`, `ArduinoOTA`, `Update`,
   `ESPmDNS`, `HTTPClient`, `NetworkClientSecure`.

### Board Settings

Open `ESP32S/ESP32S.ino`, then select **ESP32S3 Dev Module** and set:

| Setting | Value |
| --- | --- |
| Board | ESP32S3 Dev Module |
| Flash Size | **16 MB (128 Mb)** |
| PSRAM | **OPI PSRAM** |
| Partition Scheme | **16M Flash (3MB APP / 9.9MB FATFS)** |
| CPU Frequency | **240 MHz** |
| Flash Mode | **QIO 80 MHz** |
| Upload Speed | 921600 (drop to 460800 if uploads fail) |
| USB CDC On Boot | Disabled |
| Erase All Flash Before Sketch Upload | Disabled (enable only to clear WiFi settings) |

> **OTA requires two app slots.** Do not use *Huge APP (3MB No OTA/1MB SPIFFS)*:
> it leaves no OTA partition and both ArduinoOTA and the web updater will fail.
> The recommended scheme is `app3M_fat9M_16MB`.

The project ships `ESP32S/build_opt.h` containing `-O2`, which overrides the core's
default `-Os` for better runtime performance.

`USB CDC On Boot` is **Disabled** so `Serial` is routed to the USB-UART bridge. If your
board only exposes the native USB port (no USB-UART chip), set it to **Enabled** to see
the serial log.

---

## Build and Flash

1. Open `ESP32S/ESP32S.ino` in Arduino IDE.
2. Select the board and settings from the table above.
3. Connect the board over USB.
4. Click **Upload**.
5. Open **Serial Monitor** at **115200 baud**. A healthy boot prints:

   ```
   === ESP32-S3 Smart Camera ===
   CPU: 240 MHz, PSRAM: 8.0 MB
   WiFi connected: 192.168.1.102
   mDNS: http://esp32s3cam.local
   Web:80 Core0
   OTA Ready
   Ready!
   ```

6. If camera initialization fails, the board's RGB LED turns red and the sketch stops;
   see [Troubleshooting](#troubleshooting).

---

## First Boot and WiFi Provisioning

### Normal boot (station mode)

On boot the device tries the WiFi credentials saved in NVS for about 10 seconds.
If successful:

- the TFT status bar shows **WiFi已连接** and the NTP time;
- the IP address is printed on the serial log;
- the web dashboard is available at `http://<device-ip>` or `http://esp32s3cam.local`;
- the AP captive portal is **not** started.

The ESP32 supports **2.4 GHz WiFi only**. A 5 GHz-only SSID will never connect.

### First-time provisioning (AP mode)

If no credentials are saved, or the saved network cannot be reached, the device starts
an access point:

| Item | Value |
| --- | --- |
| SSID | `ESP32-S3-CAM-Setup` |
| Password | `12345678` |
| Portal URL | `http://192.168.4.1` |

Steps:

1. Connect your phone or computer to `ESP32-S3-CAM-Setup`.
2. Open `http://192.168.4.1`.
3. Pick a network from the scanned list (or type the SSID) and enter the password.
4. Tap **Connect**. The device stores the credentials and restarts automatically on success.
5. Reconnect your phone/computer to the same router and open the new IP (or
   `http://esp32s3cam.local`).

If the connection fails, the device falls back to AP mode so you can try again.
In AP mode only the provisioning page is served; the camera dashboard becomes available
after a successful station connection.

### Re-provisioning / changing WiFi

There is no runtime "reset WiFi" button. To point the camera at a different network:

1. In Arduino IDE enable **Erase All Flash Before Sketch Upload → Enabled**, upload once,
   then disable it again; **or** run `esptool.py erase_flash` and reflash.
2. On the next boot the device starts in AP mode, and you can provision the new network.

Erasing flash also clears camera settings saved in NVS (brightness, zoom, etc.).

### Time synchronization

After connecting, NTP (`pool.ntp.org`) sets the clock to **UTC+8**. Time is used by the
status bar, photo filenames and the timestamp watermark. Without internet access the clock
stays unset; the watermark is skipped rather than stamping a wrong time, and filenames fall
back to a numeric counter.

---

## Using the Camera

### TFT layout

```
┌──────────────────────────────┐
│ 智能相机            title bar│  16 px
├──────────────────────────────┤
│                              │
│        live preview          │  128 × 96
│        (128 px wide)         │
│                              │
├──────────────────────────────┤
│ WiFi已连接        12:34:56   │  16 px status bar
└──────────────────────────────┘
```

The IP address is intentionally not shown on the status bar (it does not fit on 128 px);
open **网络信息** in the menu to see IP, RSSI and streaming state.

### Button gestures (IO0)

| Gesture | Action |
| --- | --- |
| Single click | Capture a photo (honors the self-timer) |
| Double click (within 400 ms) | Toggle digital zoom 1× ↔ 2×, saved to NVS |
| Long press (> 600 ms) | Enter the menu from preview / exit the menu |

### Joystick

| Input | Preview / menu |
| --- | --- |
| Up / Down | Move the selection |
| Left / Right | Change the value of the selected row; browse the gallery |
| Short press | Enter a submenu; in a submenu it returns to the main menu |
| Press ≥ 1 s (gallery only) | Delete the current photo and its thumbnail |

### Menu map

| Menu item | What you can change |
| --- | --- |
| 相机设置 | Brightness −2…+2, contrast −2…+2, saturation −2…+2, white balance 0–4 |
| 分辨率 | UXGA 1600×1200, SXGA 1280×1024, XGA 1024×768, SVGA 800×600, QVGA 320×240 |
| 照片质量 | Ultra Q5, High Q8, Medium Q10, Low Q15, Very Low Q20 |
| 图库 | Browse photos; long-press to delete |
| 变焦防抖 | Zoom 1×–4×, scene Standard/Night/Backlight, anti-shake on/off |
| 曝光设置 | Auto/manual, shutter 0–1200, gain 0–30, self-timer, watermark |
| 网络信息 | IP, RSSI, streaming state, browser-recording hint |
| 返回 | Back to preview |

Changes are written to NVS immediately. Camera settings are re-applied when the menu is
closed and the camera is re-initialized.

### Capture pipeline

1. Optional self-timer countdown (title bar shows the remaining seconds; preview keeps running).
2. Camera switches to JPEG photo mode and applies the saved sensor settings.
3. Optional anti-shake burst: 3 frames are captured and the largest JPEG is kept.
4. Optional post-processing: digital zoom crop and/or timestamp watermark in one
   decode/re-encode pass. With zoom 1× and no watermark the original JPEG is written
   directly, with no re-encode.
5. The photo is saved to `/photo_*.jpg`; a 128×96 thumbnail is written to `/thumb/`.
6. Optional automatic upload if `uploadAuto` is enabled.
7. The camera returns to preview and re-applies all settings.

Photos are named `photo_YYYYMMDD_HHMMSS.jpg` when NTP time is available, otherwise
`photo_<counter>.jpg`.

### LED status (onboard WS2812, GPIO48)

| State | Indication |
| --- | --- |
| Idle, WiFi connected | Dim green breathing |
| Successful capture | Short white flash (only after exposure, never during it) |
| AP provisioning mode | Blue blink, 0.5 s on / 0.5 s off |
| No WiFi | Orange blink, 0.5 s on / 0.5 s off |
| Error (e.g. camera init failed) | Red fast blink for 3 s |

---

## Web Interface

Open `http://<device-ip>` or `http://esp32s3cam.local` (station mode only).

| Button / page | Route | Description |
| --- | --- | --- |
| 实时画面 | `/stream` | MJPEG live stream |
| 远程拍照 | `POST /capture` | Capture a photo on the device (honors the self-timer) |
| 设备录像 | `POST /record?action=start\|stop` | Record AVI on the SD card |
| 图库 | `/list` | Photo cards with preview, download, delete, upload |
| 设置 | `/config` | All camera and feature settings |
| 状态 | `/status` | Runtime metrics and recorded-video list |
| 固件升级 | `/update` | Browser firmware upload |

### Live stream and browser recording

- Click **实时画面** to open the MJPEG stream.
- While streaming, the **录像** button records the stream in the browser with
  `MediaRecorder` and saves a **WebM** file to your computer. The tab must stay open.
- The stream uses SVGA JPEG at quality 12 for a balance of frame rate and bandwidth.
- While streaming, the on-device TFT preview pauses because the camera is dedicated
  to the stream.

### On-device recording (AVI)

- Click **设备录像** (or `POST /record?action=start`) to start. The camera switches to
  240×240 JPEG and muxes frames into `/video_*.avi` on the SD card.
- Frame rate: `videoFps` (5 / 10 / 15). Each clip is limited by the index table
  (about 10 minutes) and recording stops automatically when free SD space drops below 2 MB.
- Recording is mutually exclusive with streaming and photo capture.
- Stop with the same button; the AVI index and all RIFF/avih/strh/movi/idx1 lengths are
  patched before the file is closed.
- Recorded files are listed on `/status` with download links.

### Settings page

| Group | Settings |
| --- | --- |
| Image | Brightness, contrast, saturation, white balance, photo resolution, JPEG quality, mirror/flip |
| Lens | Digital zoom 1×–4×, scene preset, anti-shake, self-timer, timestamp watermark |
| Exposure | Auto/manual, shutter 0–1200, gain 0–30 |
| Advanced | JPEG preview, hardware window zoom, upload URL, auto-upload, recording fps |

Some settings (preview mode, hardware zoom) take effect the next time the camera enters
preview; the page tells you when a preview restart is needed.

### Photo upload

- Fill in **照片上传地址** and optionally enable **拍照后自动上传**.
- The request is `multipart/form-data` with the field name **`photo`**, so any endpoint
  that accepts a photo field works.
- Telegram example:

  ```
  https://api.telegram.org/bot<BOT_TOKEN>/sendPhoto?chat_id=<CHAT_ID>
  ```

  HTTPS URLs are handled automatically (certificate validation is skipped on-device).
- Upload has a 10 s timeout and does not retry, so a failed upload never blocks the
  capture flow. The gallery card also has a manual **upload** button.

### Firmware update

Two paths:

1. **Browser**: `/update` → choose the compiled `.bin` → upload. The image is streamed
   into the inactive app partition and the device reboots.
2. **ArduinoOTA**: the device appears as `esp32s3cam` in the IDE's network port list
   (same network, OTA-capable partition scheme).

Both require a partition scheme with **two app slots**.

---

## Configuration Reference

### Runtime settings (stored in NVS)

| Setting | Default | Range |
| --- | --- | --- |
| Brightness / contrast / saturation | 0 | −2…+2 |
| White balance | 0 (auto) | 0 auto, 1 sunny, 2 cloudy, 3 office, 4 home |
| Photo resolution | UXGA | QVGA…UXGA |
| JPEG quality | 10 | 4–15 |
| Mirror / flip | off | on/off |
| Digital zoom | 1× | 1×–4× |
| Exposure mode | auto | auto / manual |
| Shutter (`aec_value`) | 300 | 0–1200 |
| Gain | 0 | 0–30 |
| Anti-shake | off | on/off |
| Scene | Standard | Standard / Night / Backlight |
| Self-timer | off | 0 / 2 / 5 / 10 s |
| Timestamp watermark | off | on/off |
| JPEG preview | off | on/off |
| Hardware window zoom | off | on/off (experimental) |
| Upload URL | empty | up to 190 chars |
| Auto upload | off | on/off |
| Recording fps | 10 | 5 / 10 / 15 |

### Compile-time constants (`ESP32S/camera_config.h`)

| Constant | Default | Meaning |
| --- | --- | --- |
| `PREVIEW_FRAME_SIZE` | `FRAMESIZE_128X128` | Preview sensor size |
| `PREVIEW_QUALITY` | 12 | Preview JPEG quality |
| `PREVIEW_INTERVAL_MS` | 66 | Preview interval (~15 fps); raise to reduce heat |
| `PHOTO_FRAME_SIZE` | `FRAMESIZE_UXGA` | Default photo size |
| `PHOTO_QUALITY` | 10 | Default photo quality |
| `TFT_SPI_FREQ_HZ` | 20 000 000 | TFT SPI clock; 27 MHz was unstable on the tested module |
| `NTP_SERVER` | `pool.ntp.org` | Time server |
| `GMT_OFFSET_SEC` | `8 * 3600` | UTC+8 |
| `AP_SSID` / `AP_PASS` | `ESP32-S3-CAM-Setup` / `12345678` | Provisioning AP |
| `MDNS_HOSTNAME` | `esp32s3cam` | `http://esp32s3cam.local` |

### Tuning tips

- **Too hot / battery powered**: increase `PREVIEW_INTERVAL_MS` (e.g. 100–150 ms) to lower
  the frame rate and power draw; disable JPEG preview if you need the last bit of CPU.
- **Want smoother preview**: lower `PREVIEW_INTERVAL_MS` (e.g. 50 ms) and/or enable JPEG preview.
- **Dark scenes**: try scene *Night* first, then manual exposure with a larger shutter value
  and moderate gain, or raise brightness.
- **Motion blur**: use a smaller shutter value (shorter exposure) plus anti-shake.
- **TFT artifacts**: keep `TFT_SPI_FREQ_HZ` at 20 MHz and shorten the SPI leads.

---

## SD Card Layout

```
/
├── photo_20261006_143012.jpg      # full-size photo
├── photo_20261006_143512.jpg
├── photo_17.jpg                   # fallback name when NTP time is unavailable
├── video_20261006_144000.avi      # on-device recording
└── thumb/
    ├── photo_20261006_143012.jpg  # 128×96 thumbnail, same name
    └── photo_20261006_143512.jpg
```

- Photos and videos live in the card root; thumbnails live in `/thumb`.
- The TFT gallery and web gallery only list root-level JPEG photos, never thumbnails.
- Deleting a photo also deletes its thumbnail.

---

## Design Notes

### Task layout

| Core | Task | Responsibility |
| --- | --- | --- |
| Core 1 | `cameraTask` | Capture frames, render preview (zoom + byte order), feed the watchdog |
| Core 0 | Arduino `loop` | Menu, joystick/button gestures, WiFi, web server, OTA, LED, recording tick |

### Preview paths

- **RGB565 (default)**: the sensor writes RGB565 to PSRAM; the preview task byte-swaps and
  blits 128×96 pixels to the TFT.
- **JPEG (optional)**: the sensor outputs 128×128 JPEG; TJpgDec decodes only the visible
  128×96 window (or the zoom crop) into a PSRAM buffer, then blits it. PSRAM traffic per
  frame drops from 32 KB to a few KB, which reduces heat and leaves headroom for a higher
  frame rate.

### Photo zoom

- **Software (default)**: TJpgDec decodes only the crop window, the RGB888 result is
  re-encoded with `fmt2jpg`. Memory stays bounded even at UXGA.
- **Hardware (optional)**: the OV2640 `set_res_raw()` window crop is applied for photos
  and the MJPEG stream, so zoom costs no CPU and no re-encode. Only 4:3 presets
  (UXGA / SVGA / QVGA) are supported; other sizes fall back to software.

### Watermark and thumbnails

Zoom and watermark share a single decode/re-encode pass. Thumbnails are decoded at
1/8 scale to keep memory small and are generated after the main photo is safely saved;
a thumbnail failure never loses the photo.

### Reliability

- Three consecutive frame failures trigger a camera de-init/re-init with a 10 s throttle.
- A failed SD write triggers one remount and one retry.
- Preview, capture and recording tasks feed the task watchdog around long operations.
- Camera settings are replayed after every mode switch, because `esp_camera_init()`
  resets the sensor.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| Screen stays white | TFT SPI too fast, loose wiring, or backlight off | Keep `TFT_SPI_FREQ_HZ` at 20 MHz; check CS/DC/RST/MOSI/SCLK; verify BL is high |
| Serial prints `Camera FAIL` | PSRAM not set to OPI, camera FPC loose, weak power | Set PSRAM = OPI PSRAM; reseat the camera connector; use a 5 V/1 A supply |
| `SD Err!` on screen | Card not FAT32, wiring, or card too slow | Format FAT32; check CLK 39 / CMD 38 / D0 40; reseat the card |
| No `ESP32-S3-CAM-Setup` AP | Board not booted, or router credentials succeeded | Check the serial log; 5 GHz-only networks are not supported |
| Web page unreachable | Different subnet or mDNS not supported | Use the IP printed on serial; put phone and camera on the same router |
| Settings seem not applied | Not saved, or preview-mode change pending | Press **保存**; re-enter preview after changing preview/hardware zoom |
| Upload fails | URL unreachable, token wrong, server rejects field | Test the URL from a browser/curl; field name must be `photo`; check serial log |
| OTA fails | Partition scheme without OTA slot, or image too large | Use `16M Flash (3MB APP/9.9MB FATFS)`; keep the app under 3 MB |
| Recording stops early | SD nearly full (< 2 MB) or index limit reached | Free space; start a new clip |
| AVI does not play | Unfinalized file from a power loss | Stop recording before power-off; play with VLC |
| Purple/green edges with zoom | Hardware zoom window misaligned | Disable `hwZoom`; software zoom has no such artifact |
| Photo too dark | Low light / short exposure / low brightness | Try scene Night, raise brightness or shutter, enable anti-shake |
| Watermark time wrong / missing | No NTP (no internet) | Connect to a router with internet; watermark is skipped when time is invalid |
| Device reboots during capture | Power brown-out from camera/SD current spikes | Use a stronger 5 V supply and shorter power leads |

### First checks when something fails

1. Open the serial monitor at **115200** and read the boot banner.
2. Confirm PSRAM reports **8.0 MB**.
3. Confirm the partition scheme has two app slots if you plan to use OTA.
4. Confirm every peripheral shares ground and uses 3.3 V logic.

---

## FAQ and Limitations

**Does the TFT preview keep running while streaming?**
No. Streaming switches the camera to SVGA JPEG and pauses the preview task; the TFT shows
the last frame until the stream stops.

**Can I stream and record AVI at the same time?**
No. Streaming, on-device recording and photo capture are mutually exclusive because they
need the camera in different modes.

**Does digital zoom affect the MJPEG stream?**
Only with `hwZoom` enabled. Software zoom applies to preview and photos; the stream stays
at full field of view otherwise.

**How many clients can watch the stream?**
One. The web server is single-threaded; a second client will wait or fail.

**What is the maximum photo size?**
UXGA 1600×1200 (2 MP) on OV2640.

**Why is WiFi 5 GHz not listed?**
ESP32-S3 supports 2.4 GHz only.

**Does the camera need internet?**
Only for NTP time, Telegram/HTTP uploads and browser OTA downloads. Local streaming and
capture work on a LAN without internet.

**Can I power it from a battery?**
Yes, with a stable 5 V source. Camera and SD current spikes are the main risk; add bulk
capacitance near the board.

---

## Project Structure

```
ESP32S/
├── ESP32S.ino              # Entry point: setup, loop, dual-core preview task, gestures
├── build_opt.h             # -O2 override
├── camera_config.h         # Pin map and compile-time constants
├── camera_driver.cpp/h     # Camera init/modes/sensor settings/hardware zoom
├── config_manager.cpp/h    # NVS persistence
├── html_pages.cpp/h        # Web dashboard, gallery, settings, status, update pages
├── joystick.cpp/h          # Joystick reading and edge detection
├── led_status.cpp/h        # Non-blocking WS2812 status machine
├── menu_system.cpp/h       # TFT menu state machine
├── ota_update.cpp/h        # ArduinoOTA
├── photo_capture.cpp/h     # Capture, burst, zoom/watermark, thumbnails, SD save
├── photo_upload.cpp/h      # Multipart HTTP upload
├── system_stats.cpp/h      # FPS / photo count / uptime
├── tft_display.cpp/h       # TFT UI, preview paths, gallery rendering
├── video_record.cpp/h      # MJPEG-to-AVI recorder
├── web_server.cpp/h        # HTTP routes and handlers
└── wifi_manager.cpp/h      # STA/AP provisioning, mDNS, NTP
```

---

## License

MIT License. See [LICENSE](LICENSE).

## Acknowledgements

- [espressif/esp32-camera](https://github.com/espressif/esp32-camera) — camera driver
- [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library) and
  [Adafruit ST7735/ST7789](https://github.com/adafruit/Adafruit-ST7735-Library)
- [TJpg_Decoder](https://github.com/Bodmer/TJpg_Decoder) — JPEG decoding
- [U8g2_for_Adafruit_GFX](https://github.com/olikraus/U8g2_for_Adafruit_GFX) — Chinese fonts
- MJPEG-to-AVI muxing structure informed by
  [s60sc/ESP32-CAM_MJPEG2SD](https://github.com/s60sc/ESP32-CAM_MJPEG2SD)
