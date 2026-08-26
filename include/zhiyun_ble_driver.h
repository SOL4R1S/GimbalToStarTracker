#pragma once
//
// zhiyun_ble_driver.h — ZHIYUN Weebill-S / Crane M3 세대 BLE 드라이버 (NimBLE)
//
// 프로토콜 계층(zhiyun_frame.h)은 petermaguire HCI 캡처로 바이트 단위 검증 완료.
// BLE 세션 계층(이 파일)은 커뮤니티 역설계(VictorEscribano/zhiyun-gimbal-ble,
// bleebil) 이식이며 실기 검증 없음 — HARDWARE-PENDING 표시 준수.
//
// 조사 요약:
//  * 서비스 0xFEE9, write char d44bc439-abfd-45a2-b575-925416129600
//    notify char ...9601(...129601). 페어링(보딩) 필수 — 미페어 시 0.5s 내 해제.
//  * pan 위치 cmd 0x08 (`10`+위치 i16LE), pan 위치 읽기 cmd 0x24.
//    각도 스케일 문서 불안정(degrees/65535 가설) → kZyUnitMilliDeg 보정계수 분리.
//  * 속도 명령(cmd 0x02)은 LSB≈0.11°/s로 시디리얼에 너무 느림 ⇒ 위치 증분 연쇄 전략.
//

#include "gimbal_driver.h"
#include "zhiyun_frame.h"

#ifdef ASTRO_ZHIYUN_BLE

#include <cstdint>
#include <string>

#include <NimBLEDevice.h>

class ZhiyunBleDriver : public GimbalDriver {
 public:
  const char* name() const override { return "ZHIYUN (BLE)"; }

  bool begin() override;
  void poll() override;
  bool trackYawStep(int16_t deci_deg) override;
  bool ditherPitch(int16_t deci_deg) override;
  bool shutterOpen() override;
  bool shutterClose() override;
  bool getYawDeg(float& deg) override;
  std::string probe() override;

  // notify 콜백(zyNotifyCb)이 호출하는 수신 바이트 스트림 진입점.
  void onWire(const uint8_t* data, size_t len);

 private:
  // ---- 프로토콜 상수 ----
  static constexpr uint16_t kSvcUuid = 0xFEE9;
  static const NimBLEUUID kWriteUuid;   // ...9600 (앱→짐벌)
  static const NimBLEUUID kNotifyUuid;  // ...9601 (짐벌→앱)
  static constexpr uint8_t kPosModeByte = 0x10;   // 위치명령 프리픽스
  static constexpr int32_t kMaxUnits = 32767;     // i16LE 클램프

  // TODO(hardware-calibration): pan 위치(cmd 0x08) 유닛→각도 스케일 미확정.
  // 문서 가설 degrees/65535가 실측과 불일치한다는 보고가 있어 1유닛=0.01°(milli 10)
  // 를 잠정 가정. Weebill-S/Crane M3 실측으로 반드시 보정할 것.
  static constexpr int32_t kZyUnitMilliDeg = 10;

  // yaw 텔레메트리 폴링 주기(ms) — cmd 0x24 read
  static constexpr uint32_t kYawPollMs = 200;
  // pitch 디더링 펄스 길이(ms) 후 정지 명령
  static constexpr uint32_t kPitchPulseMs = 150;

  bool sendCmd(uint8_t cmd, const std::vector<uint8_t>& payload);

  NimBLEClient* client_ = nullptr;
  NimBLERemoteCharacteristic* write_chr_ = nullptr;
  NimBLERemoteCharacteristic* notify_chr_ = nullptr;
  bool connected_ = false;
  zhiyun::ZyPacketParser parser_;
  uint8_t inc_ = 0;              // 명령마다 증가 (중복 inc 무시됨)
  uint32_t tx_count_ = 0, rx_count_ = 0, bad_frames_ = 0;

  int32_t target_millideg_ = 0;  // trackYawStep 누적 목표각
  float last_yaw_units_ = 0.f;
  bool yaw_valid_ = false;

  uint32_t pitch_stop_ms_ = 0;   // 0 = 펄스 비활성
};

#else
// ASTRO_ZHIYUN_BLE 미정의(기본 esp32dev 경로) 시 컴파일만 유지하는 폴백 스텁.
// main.cpp는 이 헤더를 참조하지 않지만, 실수 포함 시에도 빌드가 깨지지 않게 한다.
class ZhiyunBleDriver : public GimbalDriver {
 public:
  const char* name() const override { return "ZHIYUN (BLE, 미빌드)"; }
  bool begin() override { return false; }
  bool trackYawStep(int16_t) override { return false; }
  bool ditherPitch(int16_t) override { return false; }
  bool shutterOpen() override { return false; }
  bool shutterClose() override { return false; }
};
#endif  // ASTRO_ZHIYUN_BLE

#ifdef ASTRO_ZHIYUN_BLE

// ---- 구현 (단일 env 전용이라 헤더 인라인) ----

inline const NimBLEUUID ZhiyunBleDriver::kWriteUuid(
    "d44bc439-abfd-45a2-b575-925416129600");
inline const NimBLEUUID ZhiyunBleDriver::kNotifyUuid(
    "d44bc439-abfd-45a2-b575-925416129601");

namespace {
// notify 콜백 → 싱글 인스턴스 디스패치 (본 드라이버는 프로세스당 1개 가정)
ZhiyunBleDriver* g_zyInstance = nullptr;

void zyNotifyCb(NimBLERemoteCharacteristic*, uint8_t* data, size_t len,
                bool isNotify) {
  if (g_zyInstance == nullptr || !isNotify || data == nullptr || len == 0)
    return;
  g_zyInstance->onWire(data, len);
}
}  // namespace

inline bool ZhiyunBleDriver::begin() {
  // HARDWARE-PENDING: 실기 스캔/보딩/구독 흐름 — Weebill-S·Crane M3에서
  // 페어링 상태 확인 후 검증 필요. 미페어 단말은 접속이 즉시 해제된다.
  if (connected_) return true;
  if (g_zyInstance != nullptr && g_zyInstance != this) return false;

  NimBLEDevice::init("GimbalToStarTracker");
  // 보딩 필수: bond + Secure Connections. MITM은 IO가 NoInputNoOutput라 성립 안 함.
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);


  NimBLEScan* scan = NimBLEDevice::getScan();
  NimBLEScanResults results = scan->start(/*duration=*/5, /*blocking=*/true);
  // NimBLEScanResults::getDevice(i)는 값을 반환 — 인덱스 보관 후 복사해 연결.
  int target_idx = -1;
  for (int i = 0; i < results.getCount(); ++i) {
    if (results.getDevice(i).isAdvertisingService(NimBLEUUID(kSvcUuid))) {
      target_idx = i;
      break;
    }
  }
  if (target_idx < 0) return false;
  NimBLEAdvertisedDevice target = results.getDevice(target_idx);

  client_ = NimBLEDevice::createClient();
  if (!client_->connect(&target)) {
    NimBLEDevice::deleteClient(client_);
    client_ = nullptr;
    return false;
  }

  NimBLERemoteService* svc = client_->getService(NimBLEUUID(kSvcUuid));
  if (svc == nullptr) goto fail;
  write_chr_ = svc->getCharacteristic(kWriteUuid);
  notify_chr_ = svc->getCharacteristic(kNotifyUuid);
  if (write_chr_ == nullptr || !write_chr_->canWrite() || notify_chr_ == nullptr)
    goto fail;
  if (!notify_chr_->subscribe(true, zyNotifyCb)) goto fail;

  g_zyInstance = this;
  connected_ = true;
  return true;

fail:
  NimBLEDevice::deleteClient(client_);
  client_ = nullptr;
  return false;
}

inline void ZhiyunBleDriver::poll() {
  if (!connected_) return;

  // pitch 펄스 정지: 짧은 속도 펄스 후 반드시 0속도로 브레이크
  if (pitch_stop_ms_ != 0 && millis() >= pitch_stop_ms_) {
    pitch_stop_ms_ = 0;
    // 정지: 크기 0 속도 페이로드
    sendCmd(zhiyun::kCmdTiltSpeed,
            {kPosModeByte, 0x00, 0x00});  // HARDWARE-PENDING: 정지 페이로드 형식
  }

  // yaw 위치 폴링 캐시
  static uint32_t last_poll_ms = 0;
  if (millis() - last_poll_ms >= kYawPollMs) {
    last_poll_ms = millis();
    sendCmd(zhiyun::kCmdReadPanPos, {0x00, 0x00, 0x00});
  }
}

inline bool ZhiyunBleDriver::sendCmd(uint8_t cmd,
                                     const std::vector<uint8_t>& payload) {
  if (write_chr_ == nullptr || !connected_) return false;
  std::vector<uint8_t> frame = zhiyun::zyFrame(++inc_, cmd, payload);
  // write-without-response: 캡처상 앱이 해당 방식 사용
  if (!write_chr_->writeValue(frame.data(), frame.size(),
                              /*response=*/false)) {
    bad_frames_++;
    return false;
  }
  tx_count_++;
  return true;
}

inline void ZhiyunBleDriver::onWire(const uint8_t* data, size_t len) {
  std::vector<zhiyun::ZyFrame> frames;
  parser_.feed(data, len, frames);
  rx_count_ += frames.size();
  for (const zhiyun::ZyFrame& f : frames) {
    // pan 위치 읽기(cmd 0x24) 응답: `00` + 위치 i16LE → 캐시
    if (f.cmd == zhiyun::kCmdReadPanPos && f.data.size() >= 3 &&
        f.data[0] == 0x00) {
      const int16_t units = static_cast<int16_t>(
          static_cast<uint16_t>(f.data[1]) | (static_cast<uint16_t>(f.data[2])
                                              << 8));
      last_yaw_units_ = static_cast<float>(units);
      yaw_valid_ = true;
    }
  }
}

inline bool ZhiyunBleDriver::trackYawStep(int16_t deci_deg) {
  // 증분 연쇄 전략: deci(0.1°)를 milli-deg 누적 → 유닛 환산 → cmd 0x08 절대위치.
  // 속도 명령(cmd 0x02, LSB≈0.11°/s)은 시디리얼 레이트 표현 불가라 사용하지 않음.
  target_millideg_ += static_cast<int32_t>(deci_deg) * 100;
  int32_t units = target_millideg_ / kZyUnitMilliDeg;
  if (units > kMaxUnits) units = kMaxUnits;
  if (units < -kMaxUnits) units = -kMaxUnits;
  return sendCmd(zhiyun::kCmdPanPosSet,
                 {kPosModeByte, static_cast<uint8_t>(units & 0xFF),
                  static_cast<uint8_t>((units >> 8) & 0xFF)});
}

inline bool ZhiyunBleDriver::ditherPitch(int16_t deci_deg) {
  // HARDWARE-PENDING: tilt 위치 직접제어(cmd 0x06)는 배터리 조회와 충돌하며
  // 문서상 불명확 → 속도식(cmd 0x01) 짧은 펄스로 대체. 정확한 이동량은
  // 펄스 길이·속도 값 실측 캘리브레이션 필요. 호출자는 settle 대기 가정.
  bool rev = deci_deg < 0;
  int32_t mag = rev ? -deci_deg : deci_deg;          // deci → 속도 LSB 스케일 가정
  int32_t speed = mag * 8;                           // TODO(hardware-calibration): 게인
  if (speed > 2047) speed = 2047;
  if (speed <= 0) return false;
  uint16_t v = static_cast<uint16_t>(speed) | (rev ? 0x800 : 0);
  bool ok = sendCmd(zhiyun::kCmdTiltSpeed,
                    {kPosModeByte, static_cast<uint8_t>(v & 0xFF),
                     static_cast<uint8_t>((v >> 8) & 0xFF)});
  if (ok) pitch_stop_ms_ = millis() + kPitchPulseMs;
  return ok;
}

inline bool ZhiyunBleDriver::shutterOpen() {
  // 셔터 press — 문서에서 확실한 유일 서브커맨드(`c0 3c 00`).
  return sendCmd(zhiyun::kCmdButtonPress, {0xC0, 0x3C, 0x00});
}

inline bool ZhiyunBleDriver::shutterClose() {
  // HARDWARE-PENDING: 별도 release 서브커맨드 문서 미확인.
  // press가 photo/녹화 토글로 동작한다는 관측에 근거해 재press로 벌브 종료 가정.
  return sendCmd(zhiyun::kCmdButtonPress, {0xC0, 0x3C, 0x00});
}

inline bool ZhiyunBleDriver::getYawDeg(float& deg) {
  if (!yaw_valid_) return false;
  deg = last_yaw_units_ * kZyUnitMilliDeg / 1000.0f;
  return true;
}

inline std::string ZhiyunBleDriver::probe() {
  char buf[160];
  snprintf(buf, sizeof(buf),
           "{\"ok\":%s,\"tx\":%u,\"rx\":%u,\"bad\":%u,\"yaw\":%.3f}",
           connected_ ? "true" : "false", tx_count_, rx_count_, bad_frames_,
           yaw_valid_ ? last_yaw_units_ * kZyUnitMilliDeg / 1000.0f : 0.0f);
  return std::string(buf);
}

#endif  // ASTRO_ZHIYUN_BLE
