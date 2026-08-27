//
// main.cpp — GimbalToStarTracker: ESP32 적도의 컨트롤러
//
//  * 짐벌: DJI RS (CAN SDK, TWAI) — 다른 브랜드는 include/의 스텁 참조
//  * UI  : SoftAP "GimbalToStarTracker" → http://192.168.4.1
//  * 버튼: BOOT 버튼(핀은 -DTRACK_BTN_PIN, 기본 GPIO0) = 시작/정지 토글
//  * LED : 상태 LED(-DTRACK_LED_PIN, 기본 GPIO2)
//  * 설정: NVS(Preferences)에 저장 — 재부팅 후 유지
//

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <cmath>
#include <cstdlib>

#include "djiprotocol.h"
#include "gimbal_driver.h"
#include "dji_can_driver.h"
#include "tracker.h"
#include "webui.h"

#ifndef AP_PASS
#define AP_PASS "astro1234"   // 야외 자체 AP라 최소 8자만 만족하면 충분
#endif

// 버튼/LED 핀은 platformio.ini에서 -DTRACK_BTN_PIN/-DTRACK_LED_PIN으로 지정.
// 미지정 시 클래식 보드 기본값(부팅 버튼 GPIO0, 상태 LED GPIO2).
#ifndef TRACK_BTN_PIN
#define TRACK_BTN_PIN 0
#endif
#ifndef TRACK_LED_PIN
#define TRACK_LED_PIN 2
#endif
#ifndef TRACK_LED_ACTIVE_LOW
#define TRACK_LED_ACTIVE_LOW 0
#endif

static inline void ledSet(bool on) {
  digitalWrite(TRACK_LED_PIN, TRACK_LED_ACTIVE_LOW ? !on : on);
}

static DjiCanDriver driver;
static astro::Tracker tracker;
static astro::Config cfg;
static WebServer server(80);
static Preferences prefs;

static bool testshot_open_ = false;
static uint32_t testshot_deadline_ = 0;
static int last_btn_ = HIGH;

static void saveConfig() {
  prefs.begin("astro", false);
  prefs.putFloat("delay", cfg.startDelayS);  prefs.putFloat("exp", cfg.exposureS);
  prefs.putFloat("gap", cfg.gapS);           prefs.putUShort("frames", cfg.frames);
  prefs.putBool("track", cfg.tracking);      prefs.putUChar("dithN", cfg.ditherEvery);
  prefs.putFloat("dithA", cfg.ditherAmpDeg); prefs.putFloat("settle", cfg.settleS);
  prefs.end();
}

static void loadConfig() {
  prefs.begin("astro", true);
  cfg.startDelayS = prefs.getFloat("delay", 5.f);
  cfg.exposureS   = prefs.getFloat("exp", 30.f);
  cfg.gapS        = prefs.getFloat("gap", 5.f);
  cfg.frames      = prefs.getUShort("frames", 60);
  cfg.tracking    = prefs.getBool("track", true);
  cfg.ditherEvery = prefs.getUChar("dithN", 5);
  cfg.ditherAmpDeg= prefs.getFloat("dithA", 0.5f);
  cfg.settleS     = prefs.getFloat("settle", 2.f);
  prefs.end();
  if (!astro::validateConfig(cfg)) cfg = astro::Config{};
}

static void sendError(int status, const char* error) {
  char buf[96];
  snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", error);
  server.send(status, "application/json", buf);
}

static bool readFloatArg(const char* key, float& value) {
  if (!server.hasArg(key)) return true;
  String raw = server.arg(key);
  char* end = nullptr;
  const float parsed = std::strtof(raw.c_str(), &end);
  if (end == raw.c_str() || *end != '\0' || !std::isfinite(parsed)) return false;
  value = parsed;
  return true;
}

static void handleStatus() {
  const auto& s = tracker.status();
  float yaw; const bool haveYaw = driver.getYawDeg(yaw);
  char buf[448];
  snprintf(buf, sizeof(buf),
    "{\"phase\":\"%s\",\"fault\":\"%s\",\"remainS\":%u,\"frame\":%u,\"frames\":%u,\"trackSteps\":%lu,"
    "\"yaw\":%s,\"driver\":\"%s\",\"testshot\":%s,"
    "\"cfg\":{\"delay\":%.1f,\"exposure\":%.1f,\"gap\":%.1f,\"frames\":%u,"
    "\"ditherEvery\":%u,\"ditherAmp\":%.2f,\"settle\":%.1f,\"tracking\":%s}}",
    astro::phaseName(s.phase), astro::faultName(s.fault),
    static_cast<unsigned>(tracker.remainingMs(millis()) / 1000u),
    s.frame, s.frames,
    static_cast<unsigned long>(s.trackSteps),
    haveYaw ? String(yaw, 2).c_str() : "null", driver.name(),
    testshot_open_ ? "true" : "false",
    cfg.startDelayS, cfg.exposureS, cfg.gapS, cfg.frames,
    cfg.ditherEvery, cfg.ditherAmpDeg, cfg.settleS,
    cfg.tracking ? "true" : "false");
  server.send(200, "application/json", buf);
}

static void handleConfig() {
  if (tracker.isActive() || tracker.status().phase == astro::Phase::Fault) {
    sendError(409, "sequence-active");
    return;
  }

  astro::Config next = cfg;
  if (!readFloatArg("delay", next.startDelayS) ||
      !readFloatArg("exposure", next.exposureS) ||
      !readFloatArg("gap", next.gapS) ||
      !readFloatArg("ditherAmp", next.ditherAmpDeg) ||
      !readFloatArg("settle", next.settleS)) {
    sendError(400, "number-format");
    return;
  }
  if (server.hasArg("frames")) {
    const long frames = server.arg("frames").toInt();
    if (frames < 1 || frames > 65535) { sendError(400, "frames-range"); return; }
    next.frames = static_cast<uint16_t>(frames);
  }
  if (server.hasArg("ditherEvery")) {
    float every;
    if (!readFloatArg("ditherEvery", every) || every < 0.f || every > 255.f ||
        std::floor(every) != every) {
      sendError(400, "dither-every-range");
      return;
    }
    next.ditherEvery = static_cast<uint8_t>(every);
  }
  next.tracking = server.hasArg("tracking");
  if (const char* error = astro::configError(next)) {
    sendError(400, error);
    return;
  }
  cfg = next;
  saveConfig();
  server.send(200, "application/json", "{\"ok\":true}");
}

static void stopAll(uint32_t now) {
  if (testshot_open_) {
    driver.shutterClose();
    testshot_open_ = false;
  }
  tracker.stop(now);
}

void setup() {
  Serial.begin(115200);
  pinMode(TRACK_BTN_PIN, INPUT_PULLUP);
  pinMode(TRACK_LED_PIN, OUTPUT);

  loadConfig();

  if (!driver.begin()) {
    Serial.println("[FATAL] CAN(TWAI) init failed — 배선/종단 확인");
    bool blink = false;
    while (true) { blink = !blink; ledSet(blink); delay(120); }
  }
  tracker.bind(driver);

  WiFi.softAP("GimbalToStarTracker", AP_PASS);
  Serial.printf("[NET] AP up: http://%s\n", WiFi.softAPIP().toString().c_str());

  server.on("/", [] { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/status", handleStatus);
  server.on("/probe", [] { server.send(200, "application/json", driver.probe().c_str()); });
  server.on("/config", HTTP_POST, handleConfig);
  server.on("/start", [] {
    if (testshot_open_) { sendError(409, "testshot-active"); return; }
    if (tracker.status().phase == astro::Phase::Fault) {
      sendError(409, "fault-reset-required"); return;
    }
    if (tracker.isActive() || !tracker.start(cfg, millis())) {
      sendError(409, "sequence-active"); return;
    }
    server.send(200, "text/plain", "started");
  });
  server.on("/stop", [] { stopAll(millis()); server.send(200, "text/plain", "stopped"); });
  server.on("/testshot", [] {
    if (testshot_open_ || tracker.isActive() ||
        tracker.status().phase == astro::Phase::Fault) {
      sendError(409, "sequence-active");
      return;
    }
    if (!driver.shutterOpen()) { sendError(503, "shutter-open"); return; }
    testshot_open_ = true;
    testshot_deadline_ = millis() + 8000;
    server.send(200, "text/plain", "testshot open 8s");
  });
  server.begin();
}

void loop() {
  server.handleClient();
  driver.poll();
  tracker.tick(millis());

  // 테스트컷 자동 닫힘
  if (testshot_open_ && static_cast<int32_t>(millis() - testshot_deadline_) >= 0) {
    driver.shutterClose();
    testshot_open_ = false;
  }

  // BOOT 버튼: 시작/정지 토글 (디바운스)
  int b = digitalRead(TRACK_BTN_PIN);
  if (b == LOW && last_btn_ == HIGH) {
    delay(30);
    if (digitalRead(TRACK_BTN_PIN) == LOW) {
      if (tracker.status().phase == astro::Phase::Idle ||
          tracker.status().phase == astro::Phase::Done)
        tracker.start(cfg, millis());
      else
        stopAll(millis());
    }
  }
  last_btn_ = b;

  ledSet(tracker.status().phase != astro::Phase::Idle);
}
