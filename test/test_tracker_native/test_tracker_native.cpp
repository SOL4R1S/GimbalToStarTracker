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
#include <limits>
#include <vector>

class CountingDriver : public GimbalDriver {
 public:
  const char* name() const override { return "COUNTING"; }
  bool begin() override { return true; }
  bool trackYawStep(int16_t) override { ++steps_; return track_ok_; }
  bool ditherPitch(int16_t d) override { dither_.push_back(d); return dither_ok_; }
  bool shutterOpen() override { ++opens_; return open_ok_; }
  bool shutterClose() override { ++closes_; return close_ok_; }
  bool track_ok_ = true, dither_ok_ = true, open_ok_ = true, close_ok_ = true;
  uint32_t steps_ = 0, opens_ = 0, closes_ = 0;
  std::vector<int16_t> dither_;
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
  // (추적 주기만 격리 검증 — 딜레이 창을 스텝 주기보다 길게 명시 고정.
  //  전역 기본값에 의존하면 기본 딜레이 변경 시 위상 전이가 개입해 깨진다)
  {
    astro::Config c; c.startDelayS = 120.f;
    CountingDriver drv;
    astro::Tracker t;
    t.bind(drv);
    const uint32_t now = 86400000u;
    t.start(c, now);
    t.tick(now);                            // 앵커 시점: 스텝 없음
    t.tick(now + kStep - 1);                // 직전: 아래야 함
    CHECK(drv.steps_ == 0, "B1: no step just before schedule");
    t.tick(now + kStep);                    // 정각: 정확히 1스텝
    CHECK(drv.steps_ == 1, "B2: exactly one step at kTrackStepMs");
  }

  // 케이스 G: 추적 전송 실패는 Fault로 전환하고 셔터 닫기를 시도
  {
    CountingDriver drv; drv.track_ok_ = false;
    astro::Tracker t; t.bind(drv);
    astro::Config c = cfg; c.startDelayS = 120.f;
    const uint32_t now = 100000u;
    t.start(c, now);
    t.tick(now + kStep);
    CHECK(t.status().phase == astro::Phase::Fault, "G1: track failure enters Fault");
    CHECK(drv.closes_ == 1, "G2: track failure attempts shutter close");
  }

  // 케이스 H: 셔터 열기 실패도 Fault로 전환하고 정리 닫기를 시도
  {
    CountingDriver drv; drv.open_ok_ = false;
    astro::Tracker t; t.bind(drv);
    astro::Config c = cfg; c.startDelayS = 0.f;
    const uint32_t now = 200000u;
    t.start(c, now);
    t.tick(now + 1);
    CHECK(t.status().phase == astro::Phase::Fault, "H1: shutter-open failure enters Fault");
    CHECK(drv.closes_ == 1, "H2: shutter-open failure attempts shutter close");
  }

  // 케이스 I: 긴 loop 지연에도 추적 명령은 한 번만 보내고 재앵커
  {
    CountingDriver drv;
    astro::Tracker t; t.bind(drv);
    astro::Config c = cfg; c.startDelayS = 120.f;
    const uint32_t now = 300000u;
    t.start(c, now);
    t.tick(now + 10 * kStep);
    CHECK(drv.steps_ == 1, "I1: delayed tick emits one track command");
    t.tick(now + 10 * kStep + 1);
    CHECK(drv.steps_ == 1, "I2: delayed tick reanchors next command");
  }

  // 케이스 J: 증분 디더링 위치가 0 → +amp → -amp → +amp가 됨
  {
    CountingDriver drv;
    astro::Tracker t; t.bind(drv);
    astro::Config c = cfg;
    c.startDelayS = 0.f; c.exposureS = 0.01f; c.gapS = 0.f;
    c.frames = 4; c.ditherEvery = 1; c.ditherAmpDeg = 0.5f; c.settleS = 0.5f;
    const uint32_t T = 400000u;
    t.start(c, T);
    t.tick(T + 1);       // open #1
    t.tick(T + 251);     // Opening → Exposing
    t.tick(T + 262);     // Exposing → Closing
    t.tick(T + 512);     // +0.5°
    t.tick(T + 1013);    // Settling → Gap
    t.tick(T + 1014);    // open #2
    t.tick(T + 1265);    // Opening → Exposing
    t.tick(T + 1276);    // Exposing → Closing
    t.tick(T + 1526);    // -0.5°
    t.tick(T + 2027);    // Settling → Gap
    t.tick(T + 2028);    // open #3
    t.tick(T + 2279);    // Opening → Exposing
    t.tick(T + 2290);    // Exposing → Closing
    t.tick(T + 2540);    // +0.5°
    CHECK(drv.dither_.size() == 3 && drv.dither_[0] == 5 &&
          drv.dither_[1] == -10 && drv.dither_[2] == 10,
          "J1: dither alternates around baseline");
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

  // 케이스 F: remainingMs() — 위상 잔여 시간 카운트다운 계약 (웹 UI "남음 Ns")
  {
    astro::Config c; c.startDelayS = 120.f;
    CountingDriver drv;
    astro::Tracker tr; tr.bind(drv);
    const uint32_t T = 86400000u;                 // 부팅 24h 경과 시나리오
    tr.start(c, T);
    CHECK(tr.remainingMs(T) == 120000u, "F1 full delay reported");
    tr.tick(T + 1);
    CHECK(tr.remainingMs(T + 1) == 119999u, "F2 countdown decreases");
    tr.tick(T + 120000u);
    CHECK(tr.status().phase == astro::Phase::Opening, "F3 delay consumed -> Opening");
    CHECK(tr.remainingMs(T + 120000u) == 250u, "F4 opening window remainder");
    tr.stop(T + 120001u);
    CHECK(tr.remainingMs(T + 120001u) == 0, "F5 idle remainder is zero");
  }


  // 케이스 K: 공유 설정 검증은 유효 범위만 허용
  {
    astro::Config valid;
    CHECK(astro::validateConfig(valid), "K1: default config is valid");
    astro::Config zeroFrames = valid; zeroFrames.frames = 0;
    CHECK(!astro::validateConfig(zeroFrames), "K2: zero frames rejected");
    astro::Config negativeExposure = valid; negativeExposure.exposureS = -1.f;
    CHECK(!astro::validateConfig(negativeExposure), "K3: negative exposure rejected");
    astro::Config nonFinite = valid;
    nonFinite.gapS = std::numeric_limits<float>::quiet_NaN();
    CHECK(!astro::validateConfig(nonFinite), "K4: non-finite gap rejected");
    astro::Config tooMuchDither = valid; tooMuchDither.ditherAmpDeg = 10.1f;
    CHECK(!astro::validateConfig(tooMuchDither), "K5: excessive dither rejected");
    astro::Config boundary = valid;
    boundary.startDelayS = boundary.exposureS = boundary.gapS = 86400.f;
    boundary.frames = 65535; boundary.ditherAmpDeg = 10.f; boundary.settleS = 60.f;
    CHECK(astro::validateConfig(boundary), "K6: upper boundaries accepted");
  }
  if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
  printf("ALL TESTS PASSED\n");
  return 0;
}
