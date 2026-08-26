# GimbalToStarTracker

Turn a handheld camera gimbal (DJI RS series) into a **working equatorial mount** for astrophotography — the same trick as the "equatorial mode" on HONOR's Robot Phone, built from an ESP32, one CAN transceiver, and ~₩30k of parts.

```
A7M3 + DJI RS 4  +  ESP32(SN65HVD230)  →  sidereal tracking + bulb shutter control
                                        →  30s+ pinpoint star exposures on a wedge
```

[한국어 안내는 아래](#한국어-요약)

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

| Part | Notes |
|---|---|
| ESP32 DevKit (WROOM-32 or S3) | TWAI (CAN) controller is built in |
| SN65HVD230 breakout | 3.3 V CAN transceiver — do **not** use 5 V chips (MCP2551/TJA1050) |
| DJI RS 3 Pro / RS 4 / RS 4 Pro / RS 5 | RSA/NATO port exposes CAN (left side of touchscreen on RS 4) |
| Camera control cable | RS 4 ships with a USB-C cable; A7M3 also works over Sony Multi |
| Wedge tilted to your latitude | pan axis points at Polaris; wide-angle tolerates a few degrees of error |
| Optional: USB power bank, 120 Ω termination check | measure CANH–CANL resistance before first power-on |

Wiring (transceiver side):

```
ESP32 GPIO21(TX) → SN65HVD230 D          SN65HVD230 CANH/CANL → RS4 RSA/NATO CANH/CANL
ESP32 GPIO22(RX) ← SN65HVD230 R          SN65HVD230 GND       → RS4 GND   (common ground required)
SN65HVD230 RS(8) → GND (high-speed mode for 1 Mbps)
ESP32 powered from its own USB power bank
```

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

## Gimbal support status

| Brand | Status | Path |
|---|---|---|
| DJI RS 3 Pro / RS 4 / RS 4 Pro / RS 5 | ✅ implemented (hardware bring-up pending) | official RS SDK over CAN |
| ZHIYUN Weebill-S / Crane M3 generation | 🟡 code complete, compile-gated only | reverse-engineered BLE (`0xFEE9`, bonding required); speed LSB ≈ 0.11 °/s so it uses chained micro-increments of pan-position cmd `0x08` — needs on-device calibration (`HARDWARE-PENDING` markers in code) |
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

---

# 한국어 요약

**이것이 뭔가**: 손들질 카메라 짐벌(DJI RS 시리즈)을 천체사진용 적도의 마운트로 바꾸는 ESP32 펌웨어. HONOR 로봇폰의 '적도의 모드'와 동일한 원리를 DIY로 재현.

**원리**: 극축에 정렬된 pan축을 시디리얼 레이트(15.04″/s)로 회전. DJI RS SDK의 위치 제어 해상도(0.1° 이동 × 최대 25.5초 실행)로 정확히 구현 가능 — 23,934ms마다 yaw +0.1° 증분 명령 연쇄(오차 +19.6ppm). 셔터는 짐벌 유선 경유로 벌브 무제한.

**빠른 시작**:
```bash
pip3 install platformio
python3 -m platformio run -e esp32dev          # 빌드
python3 -m platformio run -e esp32dev -t upload # 업로드
# 전화기로 WiFi 'GimbalToStarTracker' 접속 → http://192.168.4.1
```
하드웨어 없이 웹앱부터 체험: 위의 호스트 시뮬레이터 명령(영문 섹션 Build & flash 참조).

**짐벌 지원**: DJI RS ✅ / ZHIYUN(Weebill-S·Crane M3 세대) 🟡 코드 완성·실기 캘리브레이션 대기 / FeiyuTech ❌ 조사 필요.

**주의**: 기본 AP 비밀번호(`astro1234`)는 공개 저장소 값이니 `-DAP_PASS`로 변경하세요. 촬영 중인 노출 보호를 위해 stop은 언제든 누를 수 있지만(Opening/Exposing 중 정지 시 셔터 자동 닫힘), 장렌즈 무인 촬영 전 밸런스·웨지 잠금을 반드시 확인하세요.
