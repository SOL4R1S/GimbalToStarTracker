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
    if (auto it = req.params.find("frames"); it != req.params.end())
      cfg.frames = std::atoi(it->second.c_str());
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
