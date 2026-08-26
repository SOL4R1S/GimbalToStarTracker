#pragma once
//
// tracker.h — 적도의 추적 + 촬영 시퀀스 비차단 상태머신 (header-only)
//
// 두 개의 독립 타이머로 동작:
//   1) 추적 타이머 : kTrackStepMs(23.934s)마다 yaw +0.1° 증분 명령
//   2) 촬영 타이머 : 딜레이 → [셔터열림 → 노출 → 셔터닫힘 → (디더링) → 갭] × N
//

#include <cstdint>
#include <cmath>
#include "gimbal_driver.h"

namespace astro {

// 시디리얼 레이트: ω = 360° / 86164.09s = 0.00417807°/s (15.04″/s)
constexpr double   kSiderealDaySec  = 86164.0905;
constexpr double   kSiderealDegPerS = 360.0 / kSiderealDaySec;
constexpr int16_t  kTrackStepDeciDeg = 1;       // 스텝당 0.1°
constexpr uint32_t kTrackStepMs      = 23934;   // 0.1° ÷ ω × 1000 (오차 ≈ +20ppm)

struct Config {
  float    startDelayS = 120.f;   // 시작 전 대기 (극축정렬·프레이밍 후 이탈용)
  float    exposureS   = 30.f;
  float    gapS        = 5.f;
  uint16_t frames      = 60;
  bool     tracking    = true;
  uint8_t  ditherEvery = 5;       // 0 = 디더링 끔
  float    ditherAmpDeg = 0.5f;  // ± 교대
  float    settleS     = 2.f;
};

enum class Phase : uint8_t {
  Idle, Delay, Opening, Exposing, Closing, Dithering, Settling, Gap, Done
};
inline const char* phaseName(Phase p) {
  switch (p) {
    case Phase::Idle: return "idle";       case Phase::Delay: return "delay";
    case Phase::Opening: return "opening"; case Phase::Exposing: return "exposing";
    case Phase::Closing: return "closing"; case Phase::Dithering: return "dither";
    case Phase::Settling: return "settle"; case Phase::Gap: return "gap";
    case Phase::Done: return "done";
  }
  return "?";
}

struct Status {
  Phase    phase = Phase::Idle;
  uint16_t frame = 0, frames = 0;
  uint32_t trackSteps = 0;
};

class Tracker {
 public:
  void start(const Config& c, uint32_t now) {
    cfg_ = c; st_.frame = 0; st_.frames = c.frames; st_.trackSteps = 0;
    dither_dir_ = 1;
    next_track_ms_ = now + kTrackStepMs;   // 앵커: 부팅 후 경과 무관, start 한 주기 뒤부터 정상 주기 추적
    enter(now + static_cast<uint32_t>(c.startDelayS * 1000.f), Phase::Delay);
  }
  void stop(uint32_t now) {
    if (st_.phase != Phase::Idle && st_.phase != Phase::Done) {
      // 열린 셔터 방지: Opening/Exposing 중 정지면 닫아준다
      if (st_.phase == Phase::Exposing || st_.phase == Phase::Opening)
        drv_->shutterClose();
    }
    enter(now, Phase::Idle);
  }

  void bind(GimbalDriver& d) { drv_ = &d; }

  void tick(uint32_t now) {
    if (!drv_) return;
    // --- 추적 타이머 (Idle/Done 제외하고 항상 가동 — 촬영 중에도 함께 회전) ---
    if (st_.phase != Phase::Idle && st_.phase != Phase::Done && cfg_.tracking) {
      while (static_cast<int32_t>(now - next_track_ms_) >= 0) {
        drv_->trackYawStep(kTrackStepDeciDeg);           // +0.1° @ 23.9s
        ++st_.trackSteps;
        next_track_ms_ += kTrackStepMs;                  // 누적 드리프트 방지
      }
    }
    // --- 촬영 시퀀스 ---
    if (st_.phase != Phase::Done && static_cast<int32_t>(now - deadline_ms_) >= 0)
      step(now);
  }

  const Status& status() const { return st_; }

  // 현재 위상의 남은 시간(ms). Idle/Done에서는 항상 0.
  uint32_t remainingMs(uint32_t now) const {
    if (st_.phase == Phase::Idle || st_.phase == Phase::Done) return 0;
    const uint32_t d = deadline_ms_ - now;                 // 롤오버 안전 비교
    return static_cast<int32_t>(d) > 0 ? d : 0;
  }

 private:
  void enter(uint32_t deadline, Phase p) { deadline_ms_ = deadline; st_.phase = p; }

  void step(uint32_t now) {
    switch (st_.phase) {
      case Phase::Delay:
        next_track_ms_ = now + kTrackStepMs;
        openShutter(now);
        break;

      case Phase::Opening:
        enter(now + static_cast<uint32_t>(cfg_.exposureS * 1000.f), Phase::Exposing);
        break;

      case Phase::Exposing:
        drv_->shutterClose();
        enter(now + 250, Phase::Closing);                // 닫힘 명령 처리 여유
        break;

      case Phase::Closing:
        ++st_.frame;
        if (st_.frame >= cfg_.frames) { enter(now, Phase::Done); break; }
        if (cfg_.ditherEvery && (st_.frame % cfg_.ditherEvery) == 0) {
          int16_t amp = static_cast<int16_t>(
              dither_dir_ * std::lround(cfg_.ditherAmpDeg * 10.f));
          dither_dir_ = -dither_dir_;
          if (drv_->ditherPitch(amp))
            { enter(now + static_cast<uint32_t>(cfg_.settleS * 1000.f), Phase::Settling); break; }
        }
        enter(now + static_cast<uint32_t>(cfg_.gapS * 1000.f), Phase::Gap);
        break;

      case Phase::Settling:
        enter(now + static_cast<uint32_t>(cfg_.gapS * 1000.f), Phase::Gap);
        break;

      case Phase::Gap:
        openShutter(now);
        break;

      default: enter(now, Phase::Idle); break;
    }
  }

  void openShutter(uint32_t now) {
    drv_->shutterOpen();
    enter(now + 250, Phase::Opening);                    // 열림 명령 처리 여유
  }

  GimbalDriver* drv_ = nullptr;
  Config cfg_{};
  Status st_{};
  uint32_t deadline_ms_ = 0, next_track_ms_ = 0;
  int8_t dither_dir_ = 1;
};

}  // namespace astro
