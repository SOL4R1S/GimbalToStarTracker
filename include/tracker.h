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
  float    startDelayS = 5.f;     // 시작 전 대기. 벤치 테스트 친화 기본값 — 야외 워크플로에선 UI에서 늘려 쓰기
  float    exposureS   = 30.f;
  float    gapS        = 5.f;
  uint16_t frames      = 60;
  bool     tracking    = true;
  uint8_t  ditherEvery = 5;       // 0 = 디더링 끔
  float    ditherAmpDeg = 0.5f;  // ± 교대
  float    settleS     = 2.f;
};

enum class Phase : uint8_t {
  Idle, Delay, Opening, Exposing, Closing, Dithering, Settling, Gap, Done, Fault
};
inline const char* phaseName(Phase p) {
  switch (p) {
    case Phase::Idle: return "idle";       case Phase::Delay: return "delay";
    case Phase::Opening: return "opening"; case Phase::Exposing: return "exposing";
    case Phase::Closing: return "closing"; case Phase::Dithering: return "dither";
    case Phase::Settling: return "settle"; case Phase::Gap: return "gap";
    case Phase::Done: return "done";       case Phase::Fault: return "fault";
  }
  return "?";
}

enum class FaultCode : uint8_t {
  None, TrackCommand, ShutterOpen, ShutterClose, DitherCommand
};
inline const char* faultName(FaultCode f) {
  switch (f) {
    case FaultCode::None: return "none";
    case FaultCode::TrackCommand: return "track-command";
    case FaultCode::ShutterOpen: return "shutter-open";
    case FaultCode::ShutterClose: return "shutter-close";
    case FaultCode::DitherCommand: return "dither-command";
  }
  return "unknown";
}

struct Status {
  Phase phase = Phase::Idle;
  FaultCode fault = FaultCode::None;
  uint16_t frame = 0, frames = 0;
  uint32_t trackSteps = 0;
};

class Tracker {
 public:
  bool start(const Config& c, uint32_t now) {
    if (drv_ == nullptr || isActive() || st_.phase == Phase::Fault) return false;
    cfg_ = c; st_.frame = 0; st_.frames = c.frames; st_.trackSteps = 0;
    st_.fault = FaultCode::None;
    dither_dir_ = 1;
    dither_offset_deci_ = 0;
    next_track_ms_ = now + kTrackStepMs;   // 앵커: 부팅 후 경과 무관, start 한 주기 뒤부터 정상 주기 추적
    enter(now + static_cast<uint32_t>(c.startDelayS * 1000.f), Phase::Delay);
    return true;
  }

  void stop(uint32_t now) {
    if (st_.phase != Phase::Idle && st_.phase != Phase::Done) {
      // Fault 포함: 원인을 몰라도 열린 셔터를 닫는 최종 안전 시도
      if (drv_ != nullptr &&
          (st_.phase == Phase::Fault || st_.phase == Phase::Exposing ||
           st_.phase == Phase::Opening))
        drv_->shutterClose();
    }
    enter(now, Phase::Idle);
    st_.fault = FaultCode::None;
  }

  void bind(GimbalDriver& d) { drv_ = &d; }

  void tick(uint32_t now) {
    if (!drv_) return;
    // --- 추적 타이머: 지연 복귀 시에도 한 tick에 최대 한 번만 전송 ---
    if (isActive() && cfg_.tracking &&
        static_cast<int32_t>(now - next_track_ms_) >= 0) {
      if (!drv_->trackYawStep(kTrackStepDeciDeg)) {
        fail(FaultCode::TrackCommand);
        return;
      }
      ++st_.trackSteps;
      next_track_ms_ = now + kTrackStepMs;              // 지연 후 현재 시각에 재앵커
    }
    // --- 촬영 시퀀스 ---
    if (st_.phase != Phase::Done && st_.phase != Phase::Fault &&
        static_cast<int32_t>(now - deadline_ms_) >= 0)
      step(now);
  }

  const Status& status() const { return st_; }

  bool isActive() const {
    return st_.phase != Phase::Idle && st_.phase != Phase::Done &&
           st_.phase != Phase::Fault;
  }

  // 현재 위상의 남은 시간(ms). Idle/Done/Fault에서는 항상 0.
  uint32_t remainingMs(uint32_t now) const {
    if (st_.phase == Phase::Idle || st_.phase == Phase::Done ||
        st_.phase == Phase::Fault) return 0;
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
        if (!drv_->shutterClose()) { fail(FaultCode::ShutterClose); break; }
        enter(now + 250, Phase::Closing);                // 닫힘 명령 처리 여유
        break;

      case Phase::Closing:
        ++st_.frame;
        if (st_.frame >= cfg_.frames) { enter(now, Phase::Done); break; }
        if (cfg_.ditherEvery && (st_.frame % cfg_.ditherEvery) == 0) {
          const int16_t amp = static_cast<int16_t>(
              std::lround(cfg_.ditherAmpDeg * 10.f));
          const int16_t target = static_cast<int16_t>(dither_dir_ * amp);
          const int16_t delta = static_cast<int16_t>(target - dither_offset_deci_);
          if (delta != 0 && !drv_->ditherPitch(delta)) {
            fail(FaultCode::DitherCommand);
            break;
          }
          dither_offset_deci_ = target;
          dither_dir_ = -dither_dir_;
          if (delta != 0) {
            enter(now + static_cast<uint32_t>(cfg_.settleS * 1000.f), Phase::Settling);
            break;
          }
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
    if (!drv_->shutterOpen()) { fail(FaultCode::ShutterOpen); return; }
    enter(now + 250, Phase::Opening);                    // 열림 명령 처리 여유
  }

  void fail(FaultCode code) {
    if (st_.phase == Phase::Fault) return;
    if (drv_ != nullptr) drv_->shutterClose();           // 실패 종류와 무관한 안전 정리
    st_.fault = code;
    st_.phase = Phase::Fault;
  }

  GimbalDriver* drv_ = nullptr;
  Config cfg_{};
  Status st_{};
  uint32_t deadline_ms_ = 0, next_track_ms_ = 0;
  int8_t dither_dir_ = 1;
  int16_t dither_offset_deci_ = 0;
};

}  // namespace astro
