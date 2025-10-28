# Raspberry Pi Global Shutter (IMX296) RAW10 Capture (C++/libcamera)

A production-style **userspace “firmware”** for the **Raspberry Pi Global Shutter Camera** (Sony **IMX296LQR-C**, RAW10).  
It configures RAW streaming through **libcamera**, exposes clean controls (exposure, gain, FPS), and saves frames as **DNG** (via a tiny built-in writer) or **.raw**.

---

## Hardware (from the module spec)

- **Sensor:** Sony **IMX296LQR-C** (Global Shutter, Color)
- **Resolution:** 1.58 MP
- **Sensor Diagonal:** 6.3 mm
- **Pixel Size:** 3.45 µm × 3.45 µm  
- **Output:** **RAW10**
- **Lens:** CS-Mount / C-Mount (C-CS adapter included); back-focus adjustable **12.5–22.4 mm**
- **IR Cut Filter:** Integrated
- **Form Factor:** 38 × 38 × 19.8 mm (29.5 mm with adapter + dust cap)
- **Weight:** 34 g (41 g with adapter + dust cap)
- **Ribbon Cable:** 150 mm
- **Tripod:** 1/4”-20

---

## Features

- RAW10 capture via **libcamera** (CSI-2 RAW packed)
- Deterministic controls (manual exposure, analogue gain, frame duration / FPS)
- Saves **DNG** (baseline, openable in RawTherapee/Darktable/etc.) or **16-bit .raw**
- Minimal dependencies (CMake + libcamera)
- Clean, extensible C++ code with human comments
- Simple CLI; headless-ready; easy to daemonize with systemd

---

## Repository Layout

```
gs_cam/
├─ CMakeLists.txt
├─ README.md
├─ include/
│  ├─ DngWriter.hpp
│  ├─ Imx296Defaults.hpp
│  └─ Util.hpp
└─ src/
   ├─ main.cpp
   ├─ DngWriter.cpp
   └─ Util.cpp
```

---

## Prerequisites

- Raspberry Pi OS (libcamera-enabled)  
- Working camera stack (`libcamera-hello` should run)
- Packages:
  ```bash
  sudo apt update
  sudo apt install -y build-essential cmake pkg-config libcamera-dev
  ```

---

## Build

```bash
git clone git@github.com:matek-dev/RPi_Global_Shutter_Camera_Driver.git
cd RPi_Global_Shutter_Camera_Driver
mkdir build && cd build
cmake ..
make -j4
```

This produces `./RPi_Global_Shutter_Camera_Driver`.

---

## Quick Start

```bash
# Save 100 frames as DNG, target 60 fps, 8 ms exposure, 1.0x gain (RGGB)
./RPi_Global_Shutter_Camera_Driver --frames 100 --fps 60 --exposure-us 8000 --gain 1.0 --bayer RGGB --outfmt DNG --outdir ./out
```

Check `./out/*.dng` in RawTherapee/Darktable/etc.

---

## CLI Usage

```
RPi_Global_Shutter_Camera_Driver [--camera <id|model-substr>] [--frames N]
                                 [--exposure-us US] [--gain X.Y] [--fps X.Y]
                                 [--bayer RGGB|BGGR|GRBG|GBRG]
                                 [--outdir DIR] [--outfmt DNG|RAW]

Defaults:
  frames        : 100
  exposure-us   : 8000
  gain          : 1.0
  fps           : 60.0
  bayer         : RGGB
  outdir        : ./out
  outfmt        : DNG
```

### Options (what they actually do)

- `--camera` – choose a specific camera by ID or model substring (e.g., `imx296`).
- `--frames` – number of frames to capture.
- `--exposure-us` – exposure time in **microseconds** (global shutter; applies to whole frame).
- `--gain` – analogue gain (driver-quantized as needed).
- `--fps` – target frames per second (programs `FrameDurationLimits`).
- `--bayer` – CFA layout used for **DNG** metadata (`RGGB|BGGR|GRBG|GBRG`).
- `--outfmt` – `DNG` (recommended) or `RAW` (16-bit LE, 10 LSBs valid).
- `--outdir` – directory for output files.

---

## Output Formats

### DNG
- 10-bit RAW stored in **16-bit** (whiteLevel=1023, blackLevel=0 by default).
- Includes CFA tags (`CFARepeatPatternDim`, `CFAPattern`, `CFAPlaneColor`) and a minimal `ColorMatrix1`.
- Openable in RawTherapee, Darktable, dcraw-family tools, etc.

### RAW (LE16)
- Little-endian 16-bit words, one per pixel (only 10 LSBs carry signal).
- Filename: `imx296_000000.raw`, `imx296_000001.raw`, …

---

## Notes on Bayer / Mosaic

The libcamera RAW10 **packing** is the same for all mosaics; we specify mosaic only for DNG readers.  
If preview looks checkerboarded/tinted, try another mosaic:

- Start with `--bayer RGGB`.
- If off, try: `BGGR`, `GRBG`, `GBRG`.

---

## Performance Tips

- Use a fast storage (USB SSD) if saving long bursts.
- Increase buffer count or reduce FPS/exposure if dropping frames.
- Avoid heavy concurrent I/O on the same disk while capturing.
- Headless: run from a TTY or service to avoid desktop contention.

---

## Troubleshooting

- **`CameraManager start failed`**  
  Ensure camera stack is enabled; verify with `libcamera-hello`.

- **All images tinted/weird pattern**  
  Wrong CFA order; switch `--bayer`.

- **DNG opens but looks clipped**  
  Adjust black/white levels in `DngWriter.hpp` after sensor characterization (dark frame + saturation test).

- **“Failed to allocate buffers”**  
  Reboot, close other camera apps, or lower resolution/FPS.

---

## Systemd Service (Optional)

Create `/etc/systemd/system/gs_cam.service`:

```ini
[Unit]
Description=IMX296 RAW10 capture (gs_cam)
After=multi-user.target

[Service]
ExecStart=/home/pi/gs_cam/build/gs_cam --frames 0 --fps 60 --exposure-us 8000 --gain 1.0 --bayer RGGB --outfmt DNG --outdir /home/pi/captures
WorkingDirectory=/home/pi/gs_cam
Restart=on-failure
User=pi
Group=pi

[Install]
WantedBy=multi-user.target
```

Enable & start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable gs_cam
sudo systemctl start gs_cam
```

> Tip: set `--frames 0` only if you later add a ring-buffer or continuous mode.  
> As-is, the example will run until stopped if you modify the code to ignore the frame limit.

---

## How It Works (Short Version)

- Configures a **Raw** stream with `libcamera`.
- Requests RAW10 (CSI-2 packed) format.
- Disables AE/AGC for deterministic capture.
- On each completed request:
  - Unpacks 10-bit → 16-bit buffer.
  - Writes **DNG** (with proper CFA tags) or **.raw**.
  - Re-queues the buffer for the next frame.

---

## Extending This

- **Ring buffer** + continuous capture with timed or signal-based flushing
- **PGM previews** (e.g., green channel) for quick sanity checks
- **libtiff** based DNG for richer tags, maker notes, better color matrices
- **Config file** (TOML/INI) for headless deployments
- **ROS 2** publisher: wrap frames in a `sensor_msgs/Image` node
- **System characterization**: measure black level, white clip, linearity; update DNG defaults
