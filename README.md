# GimbalToStarTracker

Turn a handheld camera gimbal (DJI RS series) into a **working equatorial mount** for astrophotography — the same trick as the "equatorial mode" on HONOR's Robot Phone, built from an ESP32, one CAN transceiver, and ~₩30k of parts.

```
A7M3 + DJI RS 4  +  ESP32(SN65HVD230)  →  sidereal tracking + bulb shutter control
                                        →  30s+ pinpoint star exposures on a wedge
```

[English](README.md) | [한국어](ko.md)

## Why this works

An equatorial mount does exactly one thing: rotate **one axis, aligned with Earth's rotation axis, at exactly one rate**:

$$\omega_{sidereal} = \frac{360°}{86164.09\,\text{s}} = 0.004178°/\text{s} = 15.04″/\text{s}$$

The DJI RS SDK exposes position control over CAN with **0.1° angle granularity and up to 25.5 s execution time** per command. That's all we need:

| | value |
|---|---|
| Tracking command | incremental yaw `+0.1°` spread over `time_for_action = 239` ticks (23.9 s) |
| Schedule period | `23,934 ms`, wall-clock anchored (no drift accumulation) |
| Rate error | **+19.6 ppm** vs true sidereal |
| Shutter | SDK camera command (`0x0D`) through the gimbal's wired camera-control channel → true BULB (no 30 s cap) |

Verified against DJI's official protocol document and sample frame: our frame builder reproduces the reference bytes **byte-for-byte**, including CRC16 (`init 0x3AA3`) and CRC32 (`init 0x00003AA3`).

## Hardware

### Bill of materials

| # | Item | Spec / notes | Est. price |
|---|---|---|---|
| 1 | ESP32 DevKit | WROOM-32, S3 or **C3** all work — the TWAI (CAN) controller is built into the chip, so no external CAN controller is needed. Any board exposing two free GPIOs works (defaults: GPIO21=TX, GPIO22=RX, changeable via `-DTRACK_TX_PIN/-DTRACK_RX_PIN`). **The C3 has no GPIO22 — use `env:esp32c3` (TX=4, RX=5)**. | ₩5k–12k |
| 2 | CAN transceiver breakout | **SN65HVD230** (TI, 3.3 V). Pins you'll use: `D`(1), `GND`(2), `VCC`(3), `R`(4), `RS`(8), `CANH`(7), `CANL`(6). Do **not** buy 5 V chips (MCP2551, TJA1050) — logic levels mismatch. Most breakouts have a 120 Ω jumper on board. | ₩2k–4k |
| 3 | Gimbal-side connector | The RSA/NATO accessory port uses a proprietary 6-pin connector. Easiest sources: (a) a cheap **"DJI RS Focus Wheel cable"** cut open and tapped, or (b) an RSA pigtail from the aftermarket. You only need CANH, CANL, GND. | ₩5k–15k |
| 4 | Hook-up wire & misc | 26–30 AWG silicone wire ×4 (or dupont jumpers for bench testing), heatshrink, solder. | ₩3k |
| 5 | USB power bank + cable | Field power for the DevKit. A 5,000 mAh brick runs the ESP32 for days. | usually owned |
| 6 | Wedge tilted to your latitude | Pan axis must point at Polaris. A geared/ball head locked hard works for wide-angle; dedicated equatorial wedges are nicer. | usually owned |
| 7 | Tools | Multimeter (termination + continuity checks), soldering iron, side cutters. | — |

Electronics total ≈ **₩15k–35k** excluding things you already own. No USB-CAN adapter needed on the ESP32 path — the TWAI peripheral speaks CAN natively. (If you'd rather prototype from a PC first, a USBCAN-II adapter plus [ConstantRobotics/DJIR_SDK](https://github.com/ConstantRobotics/DJIR_SDK) also works.)

### Where the gimbal exposes CAN

On the **RS 4**, the RSA/NATO ports are to the **left of the touchscreen** (the right-side NATO rail is mechanical only). Pinout per DJI's *R SDK Protocol* doc §3.1.2:

| Pin | Signal | Notes |
|---|---|---|
| 1 | VCC | 8 V ±0.4, 0.8 A rated — only enabled when AD_COM detects an accessory (10–100 kΩ pull-down) |
| 2 | CANL | ← we use this |
| 3 | SBUS_RX | unused here |
| 4 | CANH | ← we use this |
| 5 | AD_COM | accessory-detect pull-down if you want port power |
| 6 | GND | ← required (common ground) |

This table was published for RS 2-generation RSA ports; the RS 4 kept the same connector family and Focus-Wheel compatibility. **Verify with a multimeter before the first power-on anyway** — the transceiver module is your fuse.

Sourcing the connector: search marketplaces for *"DJI RS focus motor cable"* or *"Ronin RSA cable"*; cut the accessory end off and strip CANH/CANL/GND. Soldering to the gimbal body is not required and not recommended.

### Wiring

MCU ↔ transceiver:

| SN65HVD230 pin | connects to |
|---|---|
| D (1) | ESP32 GPIO21 (TWAI TX) |
| R (4) | ESP32 GPIO22 (TWAI RX) |
| VCC (3) | ESP32 3V3 |
| GND (2) | ESP32 GND |
| RS (8) | **GND** — high-speed mode; required at 1 Mbps (do not slope-limit) |
| CANH (7) | → gimbal CANH |
| CANL (6) | → gimbal CANL |

Transceiver ↔ gimbal: three wires total (CANH, CANL, GND). Common ground between the ESP32 circuit and the gimbal is mandatory even though CAN is differential.

### Bus termination (120 Ω)

With everything **powered off**, measure resistance between CANH and CANL at your cable:

- **≈60 Ω** → the gimbal already terminates its end; leave the module's 120 Ω jumper **open**.
- **Very high (>kΩ)** → close the module's 120 Ω jumper.
- Fluctuating mid-values mean a half-seated connector — reseat and re-measure.

### Powering the ESP32

- **Recommended**: independent USB power bank on the DevKit. Isolated from gimbal rails, runs all night, nothing to configure.
- Advanced: power from RSA pin 1 (8 V) through a buck converter to 5 V/VIN. Only works if AD_COM has the pull-down that makes the port enable its supply — and note the 0.8 A budget is shared with other accessories.

### Bring-up order

1. Everything unpowered: continuity-check the three wires end-to-end; measure termination as above.
2. Flash firmware first, power the ESP32 alone: phone joins the AP, `/probe` returning `{"ok":false,...}` is correct while CAN isn't connected yet.
3. Gimbal OFF → attach CANH/CANL/GND → power gimbal → `/probe` should return `{"ok":true}`.
4. Only now attach the camera control cable and run the shutter test shot.

## Build & flash

```bash
pip3 install platformio            # or: brew install platformio
pio run -e esp32dev                # build (firmware.bin)
python3 -m platformio run -e esp32dev -t upload   # flash (pio may not be on PATH)
```

Host-side unit tests need no hardware:

```bash
# protocol layer: official DJI sample frame reproduced byte-exact
g++ -std=c++17 -I include src/djiprotocol.cpp test/test_protocol_native/test_protocol_native.cpp -o /tmp/t && /tmp/t
# tracker state machine (catch-up burst, rollover-safe deadlines)
g++ -std=c++17 -I include test/test_tracker_native/test_tracker_native.cpp -o /tmp/t2 && /tmp/t2
# Zhiyun BLE framing vs real HCI captures
g++ -std=c++17 -I include src/zhiyun_frame.cpp test/test_zhiyun_native/test_zhiyun_native.cpp -o /tmp/t3 && /tmp/t3
```

Try it **without any hardware** using the host simulator (drives the real tracker state machine over the same HTTP contract):

```bash
mkdir -p build && g++ -std=c++17 -Wall -I include -I third_party \
  tools/simulator.cpp src/djiprotocol.cpp -o build/simulator
./build/simulator 8080             # then open http://127.0.0.1:8080
```

## Usage (field)

1. Balance camera → set gimbal mode switch to **FPV** → delete Bluetooth shutter pairing (wired only)
2. Connect RS 4 ↔ camera with the control cable; half-press the gimbal's camera button — AF twitch confirms the wired channel
3. Power ESP32 → join WiFi AP **GimbalToStarTracker** (default pass `astro1234`, override via `-DAP_PASS`) → open `http://192.168.4.1`
4. Configure delay/exposure/interval/frames/dithering → Start → put the phone away
5. BOOT button on the board = hardware start/stop toggle

Framing tip: faint targets won't show in live view. Use the **Test shot** button (ISO 12800 / 8 s) and review on the camera LCD.

## Runtime safety and API behavior

- `/start` and `/testshot` are rejected with HTTP **409** while a sequence is active; a second start cannot reset an exposure in progress.
- `/config` is rejected with HTTP **409** while active and HTTP **400** for invalid values. Accepted ranges: delay/exposure/gap `0–86400 s`, frames `1–65535`, dither amplitude `0–10°`, settle `0.5–60 s`, and dither interval `0–255`.
- A failed gimbal command enters `fault`; the firmware attempts a shutter close and exposes the reason in `/status` as `fault`.
- A delayed control loop sends at most one tracking step and re-anchors to the current time; it never bursts stale steps.
- **ZHIYUN is compile-only** until a physical Weebill-S/Crane M3 bring-up validates BLE pairing, position scale, dither, and shutter release. The default firmware selects DJI RS.


## Gimbal support status

| Brand | Status | Path |
|---|---|---|
| DJI RS 3 Pro / RS 4 / RS 4 Pro / RS 5 | ✅ implemented (hardware bring-up pending) | official RS SDK over CAN |
| ZHIYUN Weebill-S / Crane M3 generation | 🟡 compile-only; runtime selection and hardware validation pending | reverse-engineered BLE (`0xFEE9`, bonding required); chained pan-position cmd `0x08` is not enabled as a production path yet |
| FeiyuTech Weebill/AK/SCORP | ❌ research backlog | no public SDK; UART sniffing is the prerequisite |

Adding a brand = implement [`include/gimbal_driver.h`](include/gimbal_driver.h) (~8 methods). See the stubs in [`include/zhiyun_ble_driver.h`](include/zhiyun_ble_driver.h) and [`include/feiyu_serial_driver.h`](include/feiyu_serial_driver.h) for documented findings.

## Repository layout

```
include/djiprotocol.h     DJI R(S) SDK frames — builder/parser/CRC (pure C++, host-testable)
src/dji_can_driver.*      TWAI transport + GimbalDriver implementation (DJI)
include/tracker.h         non-blocking state machine: sidereal steps × capture sequence × dithering
web/index.html            web UI single source (include/webui.h is generated — run tools/gen_webui.py)
tools/simulator.cpp       host simulator: real Tracker + identical HTTP contract, zero hardware
tools/gen_webui.py        index.html → include/webui.h generator
third_party/httplib.h     cpp-httplib (simulator only, never compiled into firmware)
docs/superpowers/plans/   implementation plan used to build this
```

## Field-procedure notes

- The gimbal must be wedged so the **pan axis points at the celestial pole**. In FPV mode roll/pitch hold relative to the frame instead of gravity — otherwise the stabilization fights the tracking.
- Cable strain matters: the pan axis rotates slowly all night; leave slack and clip the cable.
- Keep Sony's wireless app out of the critical path by design — shutter goes through the gimbal cable, so nothing depends on Bluetooth/Wi-Fi staying alive.

## Safety & security

- Default AP password `astro1234` exists so you can boot first and configure later — override it in `platformio.ini` (`build_flags -DAP_PASS='"yourpass"'`).
- HTTP endpoints are unauthenticated by design (isolated field network, threat model = "nobody else is on this mountain"). Don't bridge the AP to your home network.
- Long lens + unattended bulb exposure: always verify balance and the wedge lock before walking away.

## Roadmap

- [ ] Hardware bring-up checklist (CAN response probe → shutter → 1° tracking verification)
- [ ] Outdoor validation: polar alignment, 30 s subs, star-trail inspection
- [ ] ZHIYUN on-device calibration & bonding validation
- [ ] FeiyuTech protocol investigation

## Acknowledgements

- [cpp-httplib](https://github.com/yhirose/cpp-httplib) (MIT) — vendored in `third_party/`, host simulator only
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) (Apache-2.0) — optional `zhiyunble` env only
- Protocol ground truth: DJI *R SDK Protocol and User Interface v2.5* + DJI-distributed CRC sources; [ConstantRobotics/DJIR_SDK](https://github.com/ConstantRobotics/DJIR_SDK)
- ZHIYUN protocol: [Peter Maguire's Weebill-S BLE writeup](https://petermaguire.xyz/posts/zhiyun-weebil-s-ble-protocol/) and [VictorEscribano/zhiyun-gimbal-ble](https://github.com/VictorEscribano/zhiyun-gimbal-ble)

## Trademarks & interoperability notice

This is an independent, community interoperability project. It contains **no code, firmware, or binaries from DJI or ZHIYUN**; protocol behavior was independently implemented from publicly available documentation and community research, and all brand names (DJI, Ronin/RS, ZHIYUN, Weebill, Sony, etc.) are used solely to describe compatibility. DJI, ZHIYUN, and Sony are trademarks of their respective owners; this project is not affiliated with, endorsed by, or sponsored by them. Interoperability-related laws differ by jurisdiction — verify what applies to you before using this with hardware you own.

## License

Apache-2.0 — see [LICENSE](LICENSE).

