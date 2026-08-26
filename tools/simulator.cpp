// 호스트 시뮬레이터 — 펌웨어와 동일한 HTTP 계약(/ , /status v2, /config, /start,
// /stop, /testshot, /probe)을 제공하고 실제 astro::Tracker를 구동한다.
// 빌드: g++ -std=c++17 -I include -I third_party tools/simulator.cpp \
//          src/djiprotocol.cpp -o build/simulator
// 실행: ./build/simulator [port]  (기본 8080, 웹루트 = web/)
#include "gimbal_driver.h"
#include "tracker.h"
#include "httplib.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
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

static bool readFloat(const httplib::Params& params, const char* key, float& value) {
  auto it = params.find(key);
  if (it == params.end()) return true;
  char* end = nullptr;
  const float parsed = std::strtof(it->second.c_str(), &end);
  if (end == it->second.c_str() || *end != '\0' || !std::isfinite(parsed)) return false;
  value = parsed;
  return true;
}

static void sendError(httplib::Response& res, int status, const char* error) {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", error);
  res.status = status;
  res.set_content(buf, "application/json");
}

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
  std::mutex state_mutex;

  httplib::Server svr;
  svr.Get("/", [&](const httplib::Request&, httplib::Response& res) {
    res.set_content(page, "text/html; charset=utf-8");
  });
  svr.Get("/status", [&](const httplib::Request&, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(state_mutex);
    const auto& s = tracker.status();
    const uint32_t now = nowMs();
    if (testshot_open && static_cast<int32_t>(now - testshot_deadline) >= 0) {
      drv.shutterClose(); testshot_open = false;
    }
    char buf[448];
    float yaw; drv.getYawDeg(yaw);
    std::snprintf(buf, sizeof(buf),
      "{\"phase\":\"%s\",\"fault\":\"%s\",\"remainS\":%u,\"frame\":%u,\"frames\":%u,\"trackSteps\":%lu,"
      "\"yaw\":%.2f,\"driver\":\"%s\",\"testshot\":%s,"
      "\"cfg\":{\"delay\":%.1f,\"exposure\":%.1f,\"gap\":%.1f,\"frames\":%u,"
      "\"ditherEvery\":%u,\"ditherAmp\":%.2f,\"settle\":%.1f,\"tracking\":%s}}",
      astro::phaseName(s.phase), astro::faultName(s.fault),
      static_cast<unsigned>(tracker.remainingMs(now) / 1000u),
      s.frame, s.frames,
      static_cast<unsigned long>(s.trackSteps), yaw, drv.name(),
      testshot_open ? "true" : "false",
      cfg.startDelayS, cfg.exposureS, cfg.gapS, cfg.frames,
      cfg.ditherEvery, cfg.ditherAmpDeg, cfg.settleS,
      cfg.tracking ? "true" : "false");
    res.set_content(buf, "application/json");
  });
  svr.Post("/config", [&](const httplib::Request& req, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (tracker.isActive() || tracker.status().phase == astro::Phase::Fault) {
      sendError(res, 409, "sequence-active"); return;
    }
    astro::Config next = cfg;
    if (!readFloat(req.params, "delay", next.startDelayS) ||
        !readFloat(req.params, "exposure", next.exposureS) ||
        !readFloat(req.params, "gap", next.gapS) ||
        !readFloat(req.params, "ditherAmp", next.ditherAmpDeg) ||
        !readFloat(req.params, "settle", next.settleS)) {
      sendError(res, 400, "number-format"); return;
    }
    if (auto it = req.params.find("frames"); it != req.params.end()) {
      char* end = nullptr;
      const long frames = std::strtol(it->second.c_str(), &end, 10);
      if (end == it->second.c_str() || *end != '\0' || frames < 1 || frames > 65535) {
        sendError(res, 400, "frames-range"); return;
      }
      next.frames = static_cast<uint16_t>(frames);
    }
    if (req.params.find("ditherEvery") != req.params.end()) {
      float every;
      if (!readFloat(req.params, "ditherEvery", every) || every < 0.f || every > 255.f ||
          std::floor(every) != every) {
        sendError(res, 400, "dither-every-range"); return;
      }
      next.ditherEvery = static_cast<uint8_t>(every);
    }
    next.tracking = req.params.find("tracking") != req.params.end();
    if (const char* error = astro::configError(next)) {
      sendError(res, 400, error); return;
    }
    cfg = next;
    res.set_content("{\"ok\":true}", "application/json");
  });
  svr.Get("/start", [&](const httplib::Request&, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (testshot_open) { sendError(res, 409, "testshot-active"); return; }
    if (tracker.status().phase == astro::Phase::Fault) {
      sendError(res, 409, "fault-reset-required"); return;
    }
    if (tracker.isActive() || !tracker.start(cfg, nowMs())) {
      sendError(res, 409, "sequence-active"); return;
    }
    res.set_content("started", "text/plain");
  });
  svr.Get("/stop", [&](const httplib::Request&, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (testshot_open) { drv.shutterClose(); testshot_open = false; }
    tracker.stop(nowMs());
    res.set_content("stopped", "text/plain");
  });
  svr.Get("/testshot", [&](const httplib::Request&, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (testshot_open || tracker.isActive() ||
        tracker.status().phase == astro::Phase::Fault) {
      sendError(res, 409, "sequence-active"); return;
    }
    if (!drv.shutterOpen()) { sendError(res, 503, "shutter-open"); return; }
    testshot_open = true;
    testshot_deadline = nowMs() + 8000;
    res.set_content("testshot open 8s", "text/plain");
  });
  svr.Get("/probe", [&](const httplib::Request&, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(state_mutex);
    res.set_content(drv.probe(), "application/json");
  });

  std::thread tick([&] {
    for (;;) {
      {
        std::lock_guard<std::mutex> lock(state_mutex);
        tracker.tick(nowMs());
        if (testshot_open && static_cast<int32_t>(nowMs() - testshot_deadline) >= 0) {
          drv.shutterClose(); testshot_open = false;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  });
  tick.detach();
  std::fprintf(stderr, "simulator on http://127.0.0.1:%d\n", port);
  svr.listen("127.0.0.1", port);
}
