#pragma once
#include "gimbal_driver.h"
#include "djiprotocol.h"

#include <vector>

// DJI RS 시리즈 (RS 3 Pro / RS 4 / RS 4 Pro / RS 5) — 공식 RS SDK(CAN) 경로.
// 물리: 짐벌 RSA/NATO 포트의 CANH/CANL ↔ SN65HVD230 트랜시버 ↔ ESP32 TWAI.
// TWAI 핀은 platformio.ini build_flags의 TRACK_TX_PIN/TRACK_RX_PIN.
class DjiCanDriver : public GimbalDriver {
 public:
  const char* name() const override { return "DJI RS (CAN SDK)"; }
  bool begin() override;
  void poll() override;

  bool trackYawStep(int16_t deci_deg) override;
  bool ditherPitch(int16_t deci_deg) override;
  bool shutterOpen() override;
  bool shutterClose() override;
  bool getYawDeg(float& deg) override;
  std::string probe() override;

 private:
  bool send(const std::vector<uint8_t>& frame);

  dji::PacketParser parser_;
  float    last_yaw_ = 0.f;
  bool     yaw_valid_ = false;
  uint32_t tx_count_ = 0, rx_count_ = 0, attitude_rx_count_ = 0;
};
