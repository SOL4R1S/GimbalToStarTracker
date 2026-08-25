#pragma once
//
// gimbal_driver.h — 짐벌 브랜드 독립 추적/촬영 인터페이스
//
// 상위 계층(Tracker, WebUI)은 이 인터페이스만 본다. 새 브랜드 지원 =
// GimbalDriver 구현 하나 추가 + main.cpp에서 선택.
//

#include <cstdint>
#include <string>

class GimbalDriver {
 public:
  virtual ~GimbalDriver() = default;

  virtual const char* name() const = 0;

  // 통신 채널 초기화. 성공 시 true.
  virtual bool begin() = 0;

  // 주기 폴링 (BLE/시리얼 수신 펌프 등). CAN은 내부 큐 drain용.
  virtual void poll() {}

  // 적경(RA) 추적 스텝 1회: yaw를 deci_deg(0.1° 단위)만큼 증분 이동.
  // DJI: 23.9초에 걸쳐 실행하는 증분 위치 명령 = 시디리얼 레이트.
  virtual bool trackYawStep(int16_t deci_deg) = 0;

  // 디더링: pitch 소폭 이동 (빠르게 움직이고 호출자가 settle 대기).
  virtual bool ditherPitch(int16_t deci_deg) = 0;

  // 카메라 셔터 (유선 컨트롤 케이블 또는 BT 경유). 벌브 = open→대기→close.
  virtual bool shutterOpen() = 0;
  virtual bool shutterClose() = 0;

  // 마지막으로 수신한 yaw 각도(도). 텔레메트리 없는 브랜드는 false.
  virtual bool getYawDeg(float& deg) { (void)deg; return false; }

  // 통신 진단: 짐벌 응답 수신 가능 여부. JSON 한 줄 반환.
  virtual std::string probe() { return "{\"ok\":false,\"reason\":\"unsupported\"}"; }
};
