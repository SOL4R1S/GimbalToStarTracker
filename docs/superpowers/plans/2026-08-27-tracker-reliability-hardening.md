# Tracker Reliability Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DJI RS 천체 촬영 경로에서 셔터 충돌·통신 실패·잘못된 설정을 안전하게 차단하고 추적 및 DJI 응답 검증을 강화한다.

**Architecture:** `Tracker`가 촬영·추적·셔터의 단일 소유자로서 명령 실패를 `Fault`로 승격한다. HTTP와 호스트 시뮬레이터는 공통 config validator와 동일한 충돌 응답을 사용한다. DJI 응답 해석은 `djiprotocol`의 순수 함수로 두어 native golden frame으로 검증한다.

**Tech Stack:** ESP32 Arduino, PlatformIO, C++17 native tests, existing `WebServer`, existing `httplib` simulator.

## Global Constraints

- 새 외부 의존성을 추가하지 않는다.
- ZHIYUN은 실기 검증 전 런타임 활성화하지 않는다.
- API 오류는 숨기지 않고 HTTP 상태와 JSON으로 반환한다.
- 기존 HTTP 성공 계약(`/status`, `/start`, `/stop`, `/testshot`)은 유지하되 충돌만 4xx로 명시한다.
- `include/webui.h`는 `web/index.html`에서 생성 스크립트로 재생성한다.
- 각 코드 변경은 먼저 실패하는 native 또는 simulator 테스트를 추가한 뒤 구현한다.

---

### Task 1: Harden Tracker state and command failures

**Files:**
- Modify: `include/tracker.h`
- Test: `test/test_tracker_native/test_tracker_native.cpp`

**Interfaces:**
- Produces `astro::Phase::Fault`, `astro::FaultCode`, `astro::faultName`, `Tracker::isActive()`, `Status::fault`.
- `Tracker::start` remains callable by existing callers; it returns `bool` so callers can reject a non-idle start.

- [ ] **Step 1: Write failing tests**

Add tests for:

```cpp
// failed track command enters Fault and attempts shutter close
// failed shutter open enters Fault and attempts cleanup close
// one delayed tick emits at most one track command and reanchors
// dither offsets are 0 -> +amp -> -amp -> +amp
```

Use a `CountingDriver` flag for the next operation's return value and record dither increments and close calls.

- [ ] **Step 2: Run Tracker test and verify expected failures**

Run:

```bash
g++ -std=c++17 -I include test/test_tracker_native/test_tracker_native.cpp -o /tmp/tracker_test && /tmp/tracker_test
```

Expected: new Fault/catch-up/dither assertions fail because the current Tracker has no Fault state, unbounded catch-up, and incremental `+amp/-amp` dither.

- [ ] **Step 3: Implement minimal Tracker changes**

Add a `Fault` phase and fixed fault codes. Make `start` return false unless Idle or Done and reset fault state on a valid start. Check every driver operation; on failure call a private fault helper that attempts `shutterClose`, stores the fault code, and enters Fault. Add `isActive()` for all phases except Idle, Done, and Fault.

Replace the tracking `while` with one due-check. After a due command, set `next_track_ms_ = now + kTrackStepMs` so a delayed loop cannot burst commands. Track current dither offset and send the delta needed to alternate between `+amp` and `-amp`.

- [ ] **Step 4: Run Tracker tests and verify green**

Run the same native command. Expected: all existing and new assertions pass.

- [ ] **Step 5: Commit**

```bash
git add include/tracker.h test/test_tracker_native/test_tracker_native.cpp
git commit -m "fix(tracker): fail safe on command errors and delayed ticks"
```

---

### Task 2: Share config validation and protect HTTP state transitions

**Files:**
- Modify: `include/tracker.h`
- Modify: `src/main.cpp`
- Modify: `tools/simulator.cpp`
- Test: `test/test_tracker_native/test_tracker_native.cpp`
- Test: `tools/simulator.cpp` behavioral smoke commands

**Interfaces:**
- Produces `astro::validateConfig(const Config&)` and `astro::configError(const Config&)`.
- Firmware and simulator return `{"ok":false,"error":"..."}` with HTTP 400 for invalid config and HTTP 409 for active-state conflicts.

- [ ] **Step 1: Write failing validation/state tests**

Add native assertions for zero frames, negative exposure, non-finite values, and an accepted boundary config. Extend the simulator's driver with observable shutter state if needed for the smoke scenario.

- [ ] **Step 2: Run tests and simulator smoke to verify failures**

Run the native test and start the simulator, then issue invalid `/config`, repeated `/start`, and `/testshot` while active. Expected: current code accepts invalid config and returns 200 for conflicts.

- [ ] **Step 3: Implement common validation and guards**

In `tracker.h`, validate finite values and these ranges: delay/exposure/gap `0..86400`, frames `1..65535`, ditherEvery `0..255`, dither amplitude `0..10`, settle `0.5..60`. Keep the existing tracking checkbox semantics.

In firmware and simulator, parse into a candidate config, reject invalid candidates before saving, and return the same JSON error shape. Reject config and start while `tracker.isActive()`. Reject testshot while active or already open. Stop must close an outstanding testshot and then stop the Tracker. Do not let a testshot timer close a Tracker-owned shutter.

- [ ] **Step 4: Update web UI for response errors**

Change start/stop/testshot handlers to inspect `response.ok` and show the returned error in the existing status element; keep the current controls and layout.

- [ ] **Step 5: Run native and simulator tests**

Run the Tracker native test, build/run the simulator, and exercise invalid config, active conflict, stop cleanup, and valid idle start. Expected: 400/409 responses and no cross-owner shutter close.

- [ ] **Step 6: Regenerate embedded UI and commit**

```bash
python3 tools/gen_webui.py web/index.html include/webui.h
git add include/tracker.h src/main.cpp tools/simulator.cpp web/index.html include/webui.h test/test_tracker_native/test_tracker_native.cpp
git commit -m "fix(api): validate configs and serialize shutter ownership"
```

---

### Task 3: Correct DJI response parsing and probe correlation

**Files:**
- Modify: `include/djiprotocol.h`
- Modify: `src/djiprotocol.cpp`
- Modify: `src/dji_can_driver.h`
- Modify: `src/dji_can_driver.cpp`
- Test: `test/test_protocol_native/test_protocol_native.cpp`

**Interfaces:**
- Produces `dji::parseAttitudeResponse(const std::vector<uint8_t>&, float&)`.
- `DjiCanDriver::getYawDeg` returns true only after a valid attitude response.
- `probe` correlates success to a valid attitude response after the request.

- [ ] **Step 1: Write failing response tests**

Construct a valid response frame with CmdSet `0x0E`, CmdID `0x02`, response data `{rc, data_type, yaw_i16, roll_i16, pitch_i16}`, then assert yaw equals the encoded deci-degree value. Add rejection assertions for wrong CmdSet/CmdID and truncated data.

- [ ] **Step 2: Run protocol test and verify expected failures**

```bash
g++ -std=c++17 -I include src/djiprotocol.cpp test/test_protocol_native/test_protocol_native.cpp -o /tmp/proto_test && /tmp/proto_test
```

Expected: the new parser assertions fail because the helper does not exist.

- [ ] **Step 3: Implement pure parser and driver counters**

Parse command fields at frame indices 12 and 13, validate the minimum response payload, and decode yaw from the first attitude `int16` after `rc` and `data_type`. Add a valid-attitude counter and yaw-valid flag to the DJI driver. Increment them only when the pure parser accepts a response. Make `probe` compare that counter before and after the request instead of accepting arbitrary received frames.

- [ ] **Step 4: Run protocol tests and commit**

Run the protocol native test and commit:

```bash
git add include/djiprotocol.h src/djiprotocol.cpp src/dji_can_driver.h src/dji_can_driver.cpp test/test_protocol_native/test_protocol_native.cpp
git commit -m "fix(dji): validate attitude responses before reporting yaw"
```

---

### Task 4: Synchronize verification, simulator contract, and documentation

**Files:**
- Modify: `tools/simulator.cpp`
- Modify: `web/index.html`
- Modify: `include/webui.h`
- Modify: `README.md`
- Modify: `ko.md`
- Modify: `platformio.ini` or support text only if runtime status is still compile-only

- [ ] **Step 1: Add simulator behavior checks**

Exercise the actual running simulator with curl: valid config returns 200, invalid config returns 400, active config/start/testshot return 409, stop returns 200 and returns to idle. Capture `/status` before and after to confirm no testshot flag remains.

- [ ] **Step 2: Update UI and docs**

Document Fault behavior, rejected active-state operations, valid config ranges, and that ZHIYUN remains compile-only until hardware bring-up. Keep English and Korean guides aligned.

- [ ] **Step 3: Regenerate and inspect embedded UI**

```bash
python3 tools/gen_webui.py web/index.html include/webui.h
grep -n "response.ok\|error" web/index.html include/webui.h
```

- [ ] **Step 4: Commit documentation and simulator contract**

```bash
git add tools/simulator.cpp web/index.html include/webui.h README.md ko.md platformio.ini
git commit -m "docs: document hardened runtime and simulator contract"
```

---

### Task 5: Run complete verification and smoke test

**Files:** None beyond prior tasks.

- [ ] **Step 1: Run all native tests**

```bash
g++ -std=c++17 -I include src/djiprotocol.cpp test/test_protocol_native/test_protocol_native.cpp -o /tmp/proto_test && /tmp/proto_test
g++ -std=c++17 -I include test/test_tracker_native/test_tracker_native.cpp -o /tmp/tracker_test && /tmp/tracker_test
g++ -std=c++17 -I include src/zhiyun_frame.cpp test/test_zhiyun_native/test_zhiyun_native.cpp -o /tmp/zy_test && /tmp/zy_test
```

Expected: each reports `ALL TESTS PASSED`.

- [ ] **Step 2: Build firmware environments**

```bash
python3 -m platformio run -e esp32dev
python3 -m platformio run -e zhiyunble
```

Expected: both environments succeed; ZHIYUN remains compile-gated.

- [ ] **Step 3: Run simulator end-to-end smoke**

Build/start the simulator and verify valid idle start, invalid config rejection, active operation conflicts, testshot rejection during tracking, stop cleanup, and Fault status representation where available.

- [ ] **Step 4: Review diff and final status**

Inspect only changed files, confirm generated `include/webui.h` matches `web/index.html`, confirm no unrelated files changed, and report exact command outputs and remaining real-hardware risks.
