# AstroTrack 웹앱 검증·하드웨어 브링업 구현 플랜

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** ESP32 적도의 컨트롤러(AstroTrack)의 웹앱을 하드웨어 없이 브라우저로 검증 가능한 상태까지 완성하고, 이어서 실기 CAN/셔터/추적 브링업 체크리스트를 통과시킨다.

**Architecture:** 기존에 검증된 프로토콜 코어(`djiprotocol`, 호스트 단위테스트 통과) 위에서, ①웹 HTML을 단일 소스(`web/index.html`)로 추출하고 ②`cpp-httplib` 기반 호스트 시뮬레이터가 실제 `Tracker` 상태머신 + Mock 드라이버를 구동해 펌웨어와 동일한 HTTP 계약을 제공한다. 시뮬레이터를 브라우저 E2E로 검증한 뒤 동일 코드를 실기에 업로드한다.

**Tech Stack:** PlatformIO(espressif32/arduino), ESP32 TWAI(CAN), 순수 C++17 코어(호스트 테스트 가능), cpp-httplib(개발전용, 펌웨어 비의존), vanilla JS 단일 페이지

## 현재 상태 (플랜 수립 시점)

| 영역 | 상태 | 근거 |
|---|---|---|
| 프로토콜 코어(CRC·프레임·파서) | ✅ 검증 완료 | 공식 샘플 프레임 바이트 일치, `ALL TESTS PASSED` |
| 추적 수치 | ✅ 검증 완료 | +0.1°/23,934ms, 오차 +19.6ppm |
| `dji_can_driver.cpp` / `main.cpp` / `tracker.h` / `webui.h` | ⚠️ 작성만 함, **ESP32 컴파일 미검증** | pio 빌드 실행 전 |
| 웹앱 | ❌ 미검증 (HTML 존재, 동작 확인 없음) | 사용자 판단과 일치 — 본 플랜 Task 3~5로 해소 |
| 하드웨어 브링업 | ❌ 미착수 | 장비 연결 필요 |
| Zhiyun BLE | 스텁(조사 결과 주석 포함) | Task 7 |

## Global Constraints

- 펌웨어 런타임 의존: Arduino core + `driver/twai.h` **만** (외부 라이브러리 금지)
- 웹 UI 단일 소스 = `web/index.html`. `include/webui.h`는 **생성물**이므로 직접 수정 금지
- 프로토콜 상수 불변: CAN 송신 `0x223` / 수신 `0x222`, 1Mbps standard frame,
  CRC16 init `0x3AA3`, CRC32 init `0x00003AA3`
- 추적 불변식: yaw +0.1°(deci=1) @ `time_for_action=239틱`, 스케줄 주기 23,934ms
- 새 로직은 가능한 한 호스트(g++)에서 먼저 검증하고 펌웨어에 반영
- 커밋은 태스크 단위. 메시지 접두사 `feat:`/`fix:`/`test:`/`chore:`

## 파일 구조

```
web/index.html                  # 신규 — 웹 UI 단일 소스
tools/gen_webui.py              # 신규 — index.html → include/webui.h 생성기
tools/simulator.cpp             # 신규 — 호스트 시뮬레이터 (MockDriver+Tracker+HTTP)
third_party/httplib.h           # 신규 — cpp-httplib 단일 헤더 (개발전용)
include/webui.h                 # 재생성됨 (수동 편집 금지)
include/gimbal_driver.h         # 수정 — probe() 인터페이스 추가
src/dji_can_driver.cpp          # 수정 — probe() 구현
src/main.cpp                    # 수정 — /status v2, /probe 라우트
test/test_protocol_native/...   # 기존 유지 (회귀 게이트)
docs/superpowers/plans/2026-08-26-astrotrack-webapp-and-hw.md  # 본 문서
```

---

### Task 1: 웹 단일 소스화

**Files:**
- Create: `web/index.html`
- Create: `tools/gen_webui.py`
- Modify(생성물): `include/webui.h`

**Interfaces:**
- Produces: `python3 tools/gen_webui.py <in.html> <out.h>` — out.h는 `static const char INDEX_HTML[] PROGMEM R"HTML(...)HTML";` 형태
- Consumes: 현재 `include/webui.h` 안의 HTML 본문 (내용 변경 없이 이동)

- [ ] **Step 1: HTML 파일 생성**

현재 `include/webui.h`의 `R"HTML(...)HTML"` 내부 내용을 그대로 `web/index.html`로 저장한다. (이 시점에서 내용 개선 금지 — Task 5에서 한다.)

- [ ] **Step 2: 생성기 작성**

```python
#!/usr/bin/env python3
"""web/index.html → include/webui.h 변환기 (단일 소스 유지용)"""
import sys, pathlib

def main() -> None:
    if len(sys.argv) != 3:
        sys.exit("usage: gen_webui.py <index.html> <webui.h>")
    html = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
    assert "]HTML]" not in html, "HTML 본문에 종결자 ]HTML] 포함 불가"
    out = (
        "#pragma once\n// 자동 생성 파일 — 수정 금지. 원본: web/index.html\n"
        "// 재생성: python3 tools/gen_webui.py web/index.html include/webui.h\n"
        'static const char INDEX_HTML[] PROGMEM R"HTML(' + html + ')HTML";\n'
    )
    pathlib.Path(sys.argv[2]).write_text(out, encoding="utf-8")
    print(f"wrote {sys.argv[2]} ({len(html)} bytes)")

if __name__ == "__main__":
    main()
```

- [ ] **Step 3: 재생성 및 컴파일 확인**

Run: `python3 tools/gen_webui.py web/index.html include/webui.html.h && g++ -std=c++17 -fsyntax-only -x c++ - <<< '#include "webui.h"' -I include`
Expected: `wrote ...` 출력 후 syntax-only 통과(에러 없음).
(주의: include 경로 충돌 방지를 위해 임시 이름으로 검증 후 기존 `include/webui.h`를 덮어쓴다.)

```bash
python3 tools/gen_webui.py web/index.html include/webui.h
```

- [ ] **Step 4: 회귀 게이트**

Run: `g++ -std=c++17 -I include src/djiprotocol.cpp test/test_protocol_native/test_protocol_native.cpp -o /tmp/proto_test && /tmp/proto_test`
Expected: `ALL TESTS PASSED`

- [ ] **Step 5: Commit**

```bash
git add web/index.html tools/gen_webui.py include/webui.h
git commit -m "chore: extract web UI to single source web/index.html"
```

---

### Task 2: ESP32 타깃 컴파일 게이트

**Files:**
- Modify: `platformio.ini` (필요 시), `src/*.cpp`, `include/*.h` (컴파일 오류 수정)

**Interfaces:**
- Produces: `.pio/build/esp32dev/firmware.bin` — 이후 모든 하드웨어 태스크의 선행 조건

- [ ] **Step 1: PlatformIO 설치 확인**

Run: `pio --version || pip3 install -U platformio`
Expected: 버전 문자열 출력

- [ ] **Step 2: 빌드**

Run: `pio run -e esp32dev`
Expected: 첫 실행은 툴체인 다운로드 후 성공. 실패 시 오류 유형별 수정 가이드:
- `TWAI_GENERAL_CONFIG_DEFAULT` 관련 → `(gpio_num_t)` 캐스트 유지 확인 (`src/dji_can_driver.cpp`)
- `Preferences::putUShort` 미정의 → `putUShort`은 있음. 오탈자 여부만 확인
- PROGMEM/R"HTML" → Task 1 산출물 사용 확인

- [ ] **Step 3: 산출물 확인**

Run: `ls -la .pio/build/esp32dev/firmware.bin`
Expected: 바이너리 존재

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "fix: pass esp32dev build gate"
```
(수정이 전혀 없었다면 커밋 생략)

---

### Task 3: 호스트 시뮬레이터 + `/status` v2 (펌웨어·시뮬 동시 적용)

**Files:**
- Create: `third_party/httplib.h` (curl로 다운로드)
- Create: `tools/simulator.cpp`
- Modify: `include/gimbal_driver.h` — `probe()` 기본 구현 추가
- Modify: `src/dji_can_driver.cpp` — `probe()` 구현
- Modify: `src/main.cpp` — `/status` v2, `/probe` 라우트

**Interfaces:**
- `/status` JSON v2 (펌웨어·시뮬레이터 동일 계약):

```json
{"phase":"exposing","frame":3,"frames":60,"trackSteps":12,"yaw":123.45,
 "driver":"DJI RS (CAN SDK)","testshot":false,
 "cfg":{"delay":120,"exposure":30,"gap":5,"frames":60,
        "ditherEvery":5,"ditherAmp":0.5,"settle":2,"tracking":true}}
```

- Produces: `GimbalDriver::virtual std::string probe()` — CAN 응답 수신 대기 진단.
  기본 구현 `{"ok":false,"reason":"unsupported"}`, DJI는 getAttitude 전송 후 rx 증가 대기.

- [ ] **Step 1: cpp-httplib 확보**

Run: `mkdir -p third_party && curl -sL -o third_party/httplib.h https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h && wc -l third_party/httplib.h`
Expected: 수천 줄 단일 헤더 다운로드

- [ ] **Step 2: `gimbal_driver.h`에 probe 추가**

```cpp
#include <string>
// ...
  // 통신 진단: 짐벌 응답 수신 가능 여부. JSON 한 줄 반환.
  virtual std::string probe() { return "{\"ok\":false,\"reason\":\"unsupported\"}"; }
```

- [ ] **Step 3: DjiCanDriver::probe 구현 (`src/dji_can_driver.cpp`)**

```cpp
std::string DjiCanDriver::probe() {
  uint32_t before = rx_count_;
  if (!send(dji::getAttitudeCommand())) return "{\"ok\":false,\"reason\":\"tx-failed\"}";
  const uint32_t t0 = millis();
  while (millis() - t0 < 500) { poll(); delay(10); }
  char buf[64];
  snprintf(buf, sizeof(buf), "{\"ok\":%s,\"rxDelta\":%u}",
           rx_count_ > before ? "true" : "false",
           static_cast<unsigned>(rx_count_ - before));
  return buf;
}
```
헤더(`src/dji_can_driver.h`)에 오버라이드 선언 추가.

- [ ] **Step 4: `main.cpp` handleStatus v2 교체**

```cpp
static bool testshot_open_ = false;   // 파일 스코프 (기존 위치 유지)

static void handleStatus() {
  const auto& s = tracker.status();
  float yaw; const bool haveYaw = driver.getYawDeg(yaw);
  char buf[320];
  snprintf(buf, sizeof(buf),
    "{\"phase\":\"%s\",\"frame\":%u,\"frames\":%u,\"trackSteps\":%lu,"
    "\"yaw\":%s,\"driver\":\"%s\",\"testshot\":%s,"
    "\"cfg\":{\"delay\":%.1f,\"exposure\":%.1f,\"gap\":%.1f,\"frames\":%u,"
    "\"ditherEvery\":%u,\"ditherAmp\":%.2f,\"settle\":%.1f,\"tracking\":%s}}",
    astro::phaseName(s.phase), s.frame, s.frames,
    static_cast<unsigned long>(s.trackSteps),
    haveYaw ? String(yaw, 2).c_str() : "null", driver.name(),
    testshot_open_ ? "true" : "false",
    cfg.startDelayS, cfg.exposureS, cfg.gapS, cfg.frames,
    cfg.ditherEvery, cfg.ditherAmpDeg, cfg.settleS,
    cfg.tracking ? "true" : "false");
  server.send(200, "application/json", buf);
}
```

`setup()`에 라우트 추가:

```cpp
server.on("/probe", [] { server.send(200, "application/json", driver.probe()); });
```

- [ ] **Step 5: 시뮬레이터 작성 (`tools/simulator.cpp`)**

```cpp
// 호스트 시뮬레이터 — 펌웨어와 동일한 HTTP 계약(/ , /status v2, /config, /start,
// /stop, /testshot, /probe)을 제공하고 실제 astro::Tracker를 구동한다.
// 빌드: g++ -std=c++17 -I include -I third_party tools/simulator.cpp \
//          src/djiprotocol.cpp -o build/simulator
// 실행: ./build/simulator [port]   (기본 8080, 웹루트 = web/)
#include "gimbal_driver.h"
#include "tracker.h"
#include "httplib.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>

static uint32_t nowMs() {
  using namespace std::chrono;
  return static_cast<uint32_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

class SimulatedDriver : public GimbalDriver {
 public:
  const char* name() const override { return "SIMULATOR"; }
  bool begin() override { return true; }
  bool trackYawStep(int16_t d) override { yaw_ += d * 0.1f; ++steps_; return true; }
  bool ditherPitch(int16_t d) override { pitch_ += d * 0.1f; return true; }
  bool shutterOpen() override { shutter_ = true; return true; }
  bool shutterClose() override { shutter_ = false; return true; }
  bool getYawDeg(float& deg) override { deg = yaw_; return true; }
  std::string probe() override { return "{\"ok\":true,\"rxDelta\":1}"; }
 private:
  float yaw_ = 125.6f, pitch_ = 37.5f;   // 그럴듯한 초기값 (야간 관측지 가정)
  bool shutter_ = false; uint32_t steps_ = 0;
};

int main(int argc, char** argv) {
  const int port = argc > 1 ? std::atoi(argv[1]) : 8080;
  std::ifstream html("web/index.html");
  std::stringstream ss; ss << html.rdbuf();
  const std::string page = ss.str();

  SimulatedDriver drv;
  astro::Tracker tracker;
  astro::Config cfg;
  tracker.bind(drv);

  bool testshot_open = false;
  uint32_t testshot_deadline = 0;

  httplib::Server svr;
  svr.Get("/", [&](const httplib::Request&, httplib::Response& res) {
    res.set_content(page, "text/html; charset=utf-8");
  });
  svr.Get("/status", [&](const httplib::Request&, httplib::Response& res) {
    if (testshot_open && nowMs() >= testshot_deadline) { drv.shutterClose(); testshot_open = false; }
    const auto& s = tracker.status();
    char buf[320];
    float yaw; drv.getYawDeg(yaw);
    snprintf(buf, sizeof(buf),
      "{\"phase\":\"%s\",\"frame\":%u,\"frames\":%u,\"trackSteps\":%lu,"
      "\"yaw\":%.2f,\"driver\":\"%s\",\"testshot\":%s,"
      "\"cfg\":{\"delay\":%.1f,\"exposure\":%.1f,\"gap\":%.1f,\"frames\":%u,"
      "\"ditherEvery\":%u,\"ditherAmp\":%.2f,\"settle\":%.1f,\"tracking\":%s}}",
      astro::phaseName(s.phase), s.frame, s.frames,
      static_cast<unsigned long>(s.trackSteps), yaw, drv.name(),
      testshot_open ? "true" : "false",
      cfg.startDelayS, cfg.exposureS, cfg.gapS, cfg.frames,
      cfg.ditherEvery, cfg.ditherAmpDeg, cfg.settleS,
      cfg.tracking ? "true" : "false");
    res.set_content(buf, "application/json");
  });
  auto num = [](const httplib::Params& p, const char* k, float def) {
    auto it = p.find(k); return it != p.end() ? std::atof(it->second.c_str()) : def;
  };
  svr.Post("/config", [&](const httplib::Request& req, httplib::Response& res) {
    cfg.startDelayS = num(req.params, "delay", cfg.startDelayS);
    cfg.exposureS   = num(req.params, "exposure", cfg.exposureS);
    cfg.gapS        = num(req.params, "gap", cfg.gapS);
    if (req.params.count("frames"))    cfg.frames      = std::atoi(req.params.at("frames").c_str());
    cfg.tracking    = req.params.count("tracking") > 0;
    cfg.ditherEvery = static_cast<uint8_t>(num(req.params, "ditherEvery", cfg.ditherEvery));
    cfg.ditherAmpDeg= num(req.params, "ditherAmp", cfg.ditherAmpDeg);
    cfg.settleS     = num(req.params, "settle", cfg.settleS);
    res.set_content("{\"ok\":true}", "application/json");
  });
  svr.Get("/start", [&](const httplib::Request&, httplib::Response& res) {
    tracker.start(cfg, nowMs()); res.set_content("started", "text/plain"); });
  svr.Get("/stop", [&](const httplib::Request&, httplib::Response& res) {
    tracker.stop(nowMs()); res.set_content("stopped", "text/plain"); });
  svr.Get("/testshot", [&](const httplib::Request&, httplib::Response& res) {
    drv.shutterOpen(); testshot_open = true;
    testshot_deadline = nowMs() + 8000;
    res.set_content("testshot open 8s", "text/plain"); });
  svr.Get("/probe", [&](const httplib::Request&, httplib::Response& res) {
    res.set_content(drv.probe(), "application/json"); });

  std::thread tick([&] { for (;;) { tracker.tick(nowMs()); std::this_thread::sleep_for(std::chrono::milliseconds(20)); } });
  fprintf(stderr, "simulator on http://127.0.0.1:%d\n", port);
  svr.listen("127.0.0.1", port);
}
```

- [ ] **Step 6: 빌드 + curl 스모크**

Run:
```bash
mkdir -p build && g++ -std=c++17 -Wall -I include -I third_party \
  tools/simulator.cpp src/djiprotocol.cpp -o build/simulator
./build/simulator 8080 & sleep 0.5
curl -s localhost:8080/status | head -c 300; echo
curl -s "localhost:8080/config?delay=2&exposure=3&gap=1&frames=4&ditherEvery=2&ditherAmp=0.5&settle=1&tracking=1"
curl -s localhost:8080/start; sleep 6; curl -s localhost:8080/status; echo
curl -s localhost:8080/stop; kill %1
```
Expected: status에 `"phase":"delay"` → 6초 후 `"phase":"exposing"`, `"frame":1`, `trackSteps>0`(추적 on), cfg가 저장한 값 반영

- [ ] **Step 7: Commit**

```bash
git add third_party/httplib.h tools/simulator.cpp include/gimbal_driver.h \
        src/dji_can_driver.h src/dji_can_driver.cpp src/main.cpp
git commit -m "feat: host simulator + /status v2 contract (fw parity)"
```

---

### Task 4: 웹앱 브라우저 E2E 검증

**Files:**
- Test: 브라우저(xd://browser) — 코드 변경보다 검증 게이트. 발견된 결함은 `web/index.html` 수정.

**Interfaces:**
- Consumes: Task 3의 `/status` v2 계약

- [ ] **Step 1: 시뮬레이터 백그라운드 기동**

Run: `./build/simulator 8080 &` (세션 유지)

- [ ] **Step 2: 페이지 로드 렌더 확인**

browser open `http://127.0.0.1:8080` → run:
```js
await wait(() => status.textContent.includes('phase'));
return {title: document.title, status: status.textContent};
```
Expected: `phase=idle`, `frame=0/…`, `driver=SIMULATOR`

- [ ] **Step 3: 설정 저장 E2E**

폼에 delay=2, exposure=3, gap=1, frames=4, ditherEvery=2 입력 후 저장 버튼 클릭 →
run: `await (await fetch('/status')).json()` → `cfg`가 입력값과 일치하는지 assert

- [ ] **Step 4: 시퀀스 라이프사이클 E2E**

시작 클릭 → 3초마다 status 폴링 5회:
assert 순서 `delay→opening→exposing→closing→(settle|gap)→…`, `frame` 최종 4 도달, `trackSteps` 증가, `yaw` 소폭 증가(+0.1°/23.9s라 폴링 사이 ±0.0 허용 — steps 카운터로 대신 검증). 정지 클릭 → `phase=idle`.

- [ ] **Step 5: 테스트컷 E2E**

테스트컷 클릭 → 직후 status `testshot=true` → 9초 후 `false`

- [ ] **Step 6: 발견 결함 수정 후 재검증**

JS 오류·상태 갱신 누락 등은 `web/index.html` 수정 → Step 1 재생성(gen_webui) → 재실행

- [ ] **Step 7: Commit**

```bash
git add -A && git commit -m "fix(web): browser-e2e verified against simulator"
```
(결함 없었으면 생략)

---

### Task 5: 웹앱 UX 보강

**Files:**
- Modify: `web/index.html`

**Interfaces:**
- Consumes: `/status` v2 (`phase`, `cfg`, `frame/frames`)

- [ ] **Step 1: phase 한글 라벨 + 진행률 바**

JS에 매핑 추가 및 렌더 변경:

```js
const PH_KO={idle:'대기',delay:'대기 딜레이',opening:'셔터 열림',exposing:'노출중',
closing:'셔터 닫힘',dither:'디더링',settle:'정착',gap:'갭',done:'완료'};
status.innerHTML =
 `<b>${PH_KO[j.phase]??j.phase}</b> ${j.phase==='done'?'✅':''}<br>`+
 `프레임 ${j.frame}/${j.frames}<br>트랙 ${j.trackSteps}스텝 · yaw ${j.yaw??'-'}°<br>`+
 `<progress max="${j.frames}" value="${j.frame}" style="width:100%">`;
```

- [ ] **Step 2: 연결 끊김 배지**

fetch 실패 시 `status.innerHTML='<span style="color:#f87171">연결 끊김 — AP 확인</span>'` (기존 try/catch에 1줄)

- [ ] **Step 3: 재생성·재검증**

```bash
python3 tools/gen_webui.py web/index.html include/webui.h
./build/simulator 8080 &   # 브라우저 재확인 후 kill
```
Expected: Task 4 전 항목 재통과 + 한글 라벨·진행률 표시

- [ ] **Step 4: Commit**

```bash
git add web/index.html include/webui.h
git commit -m "feat(web): korean phase labels, progress bar, offline badge"
```

---

### Task 6: 하드웨어 브링업

**Files:** 없음(절차). 필요 시 결함 수정 커밋.

**Interfaces:** Task 2의 firmware.bin, Task 3의 `/probe`

- [ ] **Step 1: 업로드·기동**

배선(HVD230↔RSA/NATO CANH/CANL/GND, 종단 저항 측정 포함) 후:
Run: `pio run -e esp32dev -t upload && pio device monitor`
Expected: `[NET] AP up: http://192.168.4.1`, LED idle

- [ ] **Step 2: CAN 응답 probe**

폰이 AstroTrack AP 접속 → `http://192.168.4.1/probe`
Expected: `{"ok":true,...}` — `false`면 CANH/L 교차·종단·질문 우선순위로 점검

- [ ] **Step 3: 유선 셔터**

RS4↔A7M3 USB-C 컨트롤 케이블 연결(짐벌 OFF→케이블→부팅), BT 셔터 페어링 삭제, 반누름 AF 육안 확인 후 `/testshot`
Expected: 8초 노출 1장 촬영

- [ ] **Step 4: 증분 추적 눈금**

짐벌 FPV 모드, 시작 → 24s×38회 ≈ +3.8° — 웹 status trackSteps와 실제 pan 눈금 일치 확인
Expected: 스텝당 0.1°, 편차 없음(눈으로 미세 점프 허용)

---

### Task 7: 야외 검증 (게이트: Task 6 전 항목 통과)

- [ ] **Step 1:** 웨지 위도 세팅(35–38°), 북극성 정렬, A7M3 BULB+RAW+NR OFF
- [ ] **Step 2:** 웹에서 exposure=30, gap=5, frames=5, tracking ON 시작
- [ ] **Step 3:** 서브 5장 트레일 검사(100% 확대) — 별이 점이면 통과
- [ ] **Step 4:** 결과 사진·로그 커밋 또는 기록

---

### Task 8: Zhiyun BLE 드라이버 (하드웨어 게이트 존재)

**Files:**
- Create: `src/zhiyun_frame.cpp` + `include/zhiyun_frame.h` (호스트 테스트 가능 부분)
- Create: `test/test_zhiyun_frame/test_zhiyun_frame.cpp`
- Modify: `include/zhiyun_ble_driver.h` (실구현으로 교체 — NimBLE)

**Interfaces:**
- Produces: `std::vector<uint8_t> zyFrame(uint8_t inc, uint8_t cmd, const std::vector<uint8_t>& payload);`
  형식: `24 3C | len LE | 18 12 | inc | 01 | cmd | payload | CRC16-XMODEM(LE)`
  (CRC 범위: length 필드 뒤부터 payload까지 — petermaguire 문서)

- [ ] **Step 1: CRC16-XMODEM + 프레임빌더 호스트 테스트** — 골든벡터는
  bleebil/zhiyun-gimbal-ble 리포의 캡처 프레임을 실행 시점에 인용해 고정(문서 예시
  `24 3C 08 00 18 12 …` 형태). 로컬에서 자체 CRC-XMODEM 참조구현과 교차 검증.
  Run: `g++ -std=c++17 -I include test/test_zhiyun_frame/... -o /tmp/zy_test && /tmp/zy_test`
- [ ] **Step 2: NimBLE 클라이언트 + 보딩** — zhiyun-gimbal-ble esp32 스케치의
  bonding 절차 이식. 실기 Weebill-S/Crane M3 필요.
- [ ] **Step 3: pan 위치(cmd 0x08) 증분 연쇄 실측** — 0.1° 스텝 명령 반복 시 실제
  이동량 캘리브레이션(deg-per-unit 산출), DJI 경로와 동일 스케줄러 재사용.

### 비목표 (이 플랜에서 하지 않음)

- FeiyuTech 드라이버 — 프로토콜 스니핑 선행과제로 분리 (스텁 주석의 착수 경로 참조)
- PWA 아이콘/스토어 배포, OTA 업데이트, GoTo/플레이트솔브
- 소니 무선(Creators' App) 통합 — 의도적으로 격리 유지

## Self-Review 결과

- 범위: 사용자 요청(웹앱 미완 해소 + goal 기반 진행) → Task 3~5가 축, 6~8이 후속 ✓
- 플레이스홀더: Task 8 Step 1의 골든벡터만 실행 시점 인용(외부 캡처 의존, 명시적 표기) ✓
- 타입 정합: `/status` v2 키 이름·`probe()` 서명·`zyFrame()` 서명이 각 태스크 간 일치 ✓
