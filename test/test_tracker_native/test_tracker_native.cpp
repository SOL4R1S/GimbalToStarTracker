//
// test_tracker_native.cpp — Tracker catch-up 회귀 테스트 (호스트 네이티브)
// 빌드: g++ -std=c++17 -I include test/test_tracker_native/test_tracker_native.cpp \
//          -o /tmp/tracker_test && /tmp/tracker_test
//
// 회귀 대상: start()에서 next_track_ms_ 미앵커 시, 부팅 후 오래 경과한 뒤
// /start하면 Delay 첫 tick에서 floor(now/kTrackStepMs)회 trackYawStep 연발.
//

#include "tracker.h"

#include <cstdio>

class CountingDriver : public GimbalDriver {
 public:
  const char* name() const override { return "COUNTING"; }
  bool begin() override { return true; }
  bool trackYawStep(int16_t) override { ++steps_; return true; }
  bool ditherPitch(int16_t) override { return true; }
  bool shutterOpen() override { return true; }
  bool shutterClose() override { return true; }
  uint32_t steps_ = 0;
};

static int failures = 0;

#define CHECK(cond, msg)                                              \
  do {                                                                \
    if (cond) { printf("PASS %s\n", msg); }                           \
    else { printf("FAIL %s\n", msg); ++failures; }                    \
  } while (0)

int main() {
  astro::Config cfg;                       // 기본값: tracking=true
  constexpr uint32_t kStep = astro::kTrackStepMs;

  // 케이스 A: 부팅 24h 경과 후 start → 첫 tick에서 연발 없어야 함
  {
    CountingDriver drv;
    astro::Tracker t;
    t.bind(drv);
    const uint32_t now = 86400000u;
    t.start(cfg, now);
    t.tick(now + 1);
    CHECK(drv.steps_ <= 1, "A: no catch-up burst after late start");
    // 버그 재현 시: floor(86400001/23934) ≈ 3610회
  }

  // 케이스 B: kTrackStepMs 경과 시점 도달 시 정확히 1스텝 추가
  {
    CountingDriver drv;
    astro::Tracker t;
    t.bind(drv);
    const uint32_t now = 86400000u;
    t.start(cfg, now);
    t.tick(now);                            // 앵커 시점: 스텝 없음
    t.tick(now + kStep - 1);                // 직전: 아래야 함
    CHECK(drv.steps_ == 0, "B1: no step just before schedule");
    t.tick(now + kStep);                    // 정각: 정확히 1스텝
    CHECK(drv.steps_ == 1, "B2: exactly one step at kTrackStepMs");
  }

  // 케이스 C: tracking=false면 호출 없음
  {
    CountingDriver drv;
    astro::Tracker t;
    t.bind(drv);
    astro::Config c2 = cfg; c2.tracking = false;
    const uint32_t now = 86400000u;
    t.start(c2, now);
    t.tick(now + 1);
    t.tick(now + kStep);
    t.tick(now + 3 * kStep);
    CHECK(drv.steps_ == 0, "C: no steps when tracking disabled");
  }

  if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
  printf("ALL TESTS PASSED\n");
  return 0;
}
