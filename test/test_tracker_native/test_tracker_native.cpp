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
  bool shutterOpen() override { ++opens_; return true; }
  bool shutterClose() override { ++closes_; return true; }
  uint32_t steps_ = 0, opens_ = 0, closes_ = 0;
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


  // 케이스 D: Opening 페이즈 중 stop() 시 셔터 개방 방치 회귀
  // start(delay=0) → tick으로 Delay 소진 → openShutter → Opening 진입 상태에서
  // stop하면 shutterClose가 호출되어야 한다 (버그 재현 시 close 없이 Idle).
  {
    CountingDriver drv;
    astro::Tracker t;
    t.bind(drv);
    astro::Config c2 = cfg; c2.startDelayS = 0.f;
    const uint32_t now = 86400000u;
    t.start(c2, now);
    t.tick(now + 1);                        // Delay 소진 → openShutter → Opening
    CHECK(t.status().phase == astro::Phase::Opening, "D1: entered Opening");
    t.stop(now + 2);                        // Opening 윈도우 중 정지
    CHECK(drv.closes_ == 1, "D2: shutter closed on stop during Opening");
    CHECK(t.status().phase == astro::Phase::Idle, "D3: stopped to Idle");
    // 재시작 시 정상 동작: 다시 Opening → Exposing 진행
    t.start(c2, now + 100);
    t.tick(now + 101);                      // Delay 소진 → openShutter
    CHECK(drv.opens_ == 2, "D4: reopen after restart");
    t.tick(now + 101 + 250 + 1);            // Opening 경과 → Exposing
    CHECK(t.status().phase == astro::Phase::Exposing, "D5: restart reaches Exposing");
  }

  // 케이스 E: uint32 롤오버 회귀 — 데드라인 비교가 부호 있게 이뤄져야 함
  // 2^32 직전 시각에 시작해 랩을 넘길 때까지 tick; 프레임이 조기 Done/스킵 없이
  // 순서대로 완료되어야 한다 (버그 재현 시 now(작아짐) < deadline(커짐)로 영구 정체).
  {
    CountingDriver drv;
    astro::Tracker t;
    t.bind(drv);
    astro::Config c2 = cfg;
    c2.startDelayS = 0.f; c2.exposureS = 0.01f; c2.gapS = 0.01f;
    c2.frames = 2; c2.ditherEvery = 0;
    const uint32_t now = 4294967000u;       // 2^32 - 296
    t.start(c2, now);
    uint32_t doneAt = 0;
    for (uint32_t off = 1; off <= 1600 && !doneAt; ++off) {
      t.tick(now + off);                    // uint32 산술로 자동 랩
      if (t.status().phase == astro::Phase::Done) doneAt = off;
      else if (t.status().phase == astro::Phase::Idle)
        { CHECK(false, "E: premature Idle"); break; }
    }
    CHECK(doneAt != 0, "E1: reached Done across rollover");
    CHECK(doneAt > 296u, "E2: no premature Done before wrap");
    CHECK(t.status().frame == 2, "E3: both frames counted");
    CHECK(drv.closes_ == 2, "E4: two exposures closed");
  }

  if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
  printf("ALL TESTS PASSED\n");
  return 0;
}
