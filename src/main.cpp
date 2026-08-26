//
// main.cpp — AstroTrack: ESP32 적도의 컨트롤러
//
//  * 짐벌: DJI RS (CAN SDK, TWAI) — 다른 브랜드는 include/의 스텁 참조
//  * UI  : SoftAP "AstroTrack" → http://192.168.4.1
//  * 버튼: BOOT(GPIO0) = 시작/정지 토글
//  * 설정: NVS(Preferences)에 저장 — 재부팅 후 유지
//

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

#include "djiprotocol.h"
#include "gimbal_driver.h"
#include "dji_can_driver.h"
#include "tracker.h"
#include "webui.h"

#ifndef AP_PASS
#define AP_PASS "astro1234"   // 야외 자체 AP라 최소 8자만 만족하면 충분
#endif

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
  cfg.startDelayS = prefs.getFloat("delay", 120.f);
  cfg.exposureS   = prefs.getFloat("exp", 30.f);
  cfg.gapS        = prefs.getFloat("gap", 5.f);
  cfg.frames      = prefs.getUShort("frames", 60);
  cfg.tracking    = prefs.getBool("track", true);
  cfg.ditherEvery = prefs.getUChar("dithN", 5);
  cfg.ditherAmpDeg= prefs.getFloat("dithA", 0.5f);
  cfg.settleS     = prefs.getFloat("settle", 2.f);
  prefs.end();
}

static void handleStatus() {
  const auto& s = tracker.status();
  float yaw; const bool haveYaw = driver.getYawDeg(yaw);
  char buf[320];
  snprintf(buf, sizeof(buf),
    "{\"phase\":\"%s\",\"remainS\":%u,\"frame\":%u,\"frames\":%u,\"trackSteps\":%lu,"
    "\"yaw\":%s,\"driver\":\"%s\",\"testshot\":%s,"
    "\"cfg\":{\"delay\":%.1f,\"exposure\":%.1f,\"gap\":%.1f,\"frames\":%u,"
    "\"ditherEvery\":%u,\"ditherAmp\":%.2f,\"settle\":%.1f,\"tracking\":%s}}",
    astro::phaseName(s.phase),
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
  auto f = [](const char* k, float def) {
    return server.hasArg(k) ? server.arg(k).toFloat() : def;
  };
  cfg.startDelayS = f("delay", cfg.startDelayS);
  cfg.exposureS   = f("exposure", cfg.exposureS);
  cfg.gapS        = f("gap", cfg.gapS);
  if (server.hasArg("frames"))    cfg.frames      = server.arg("frames").toInt();
  cfg.tracking    = server.hasArg("tracking");
  cfg.ditherEvery = static_cast<uint8_t>(f("ditherEvery", cfg.ditherEvery));
  cfg.ditherAmpDeg= f("ditherAmp", cfg.ditherAmpDeg);
  cfg.settleS     = f("settle", cfg.settleS);
  saveConfig();
  server.send(200, "application/json", "{\"ok\":true}");
}

void setup() {
  Serial.begin(115200);
  pinMode(0, INPUT_PULLUP);
  pinMode(2, OUTPUT);

  loadConfig();

  if (!driver.begin()) {
    Serial.println("[FATAL] CAN(TWAI) init failed — 배선/종단 확인");
    while (true) { digitalWrite(2, !digitalRead(2)); delay(120); }
  }
  tracker.bind(driver);

  WiFi.softAP("AstroTrack", AP_PASS);
  Serial.printf("[NET] AP up: http://%s\n", WiFi.softAPIP().toString().c_str());

  server.on("/", [] { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/status", handleStatus);
  server.on("/probe", [] { server.send(200, "application/json", driver.probe().c_str()); });
  server.on("/config", HTTP_POST, handleConfig);
  server.on("/start", [] { tracker.start(cfg, millis()); server.send(200, "text/plain", "started"); });
  server.on("/stop",  [] { tracker.stop(millis());  server.send(200, "text/plain", "stopped"); });
  server.on("/testshot", [] {
    driver.shutterOpen();
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
  int b = digitalRead(0);
  if (b == LOW && last_btn_ == HIGH) {
    delay(30);
    if (digitalRead(0) == LOW) {
      if (tracker.status().phase == astro::Phase::Idle ||
          tracker.status().phase == astro::Phase::Done)
        tracker.start(cfg, millis());
      else
        tracker.stop(millis());
    }
  }
  last_btn_ = b;

  digitalWrite(2, tracker.status().phase != astro::Phase::Idle ? HIGH : LOW);
}
