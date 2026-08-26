#include "dji_can_driver.h"

#include <Arduino.h>
#include <driver/twai.h>
#include <cmath>

#ifndef TRACK_TX_PIN
#define TRACK_TX_PIN 21
#endif
#ifndef TRACK_RX_PIN
#define TRACK_RX_PIN 22
#endif

bool DjiCanDriver::begin() {
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
      static_cast<gpio_num_t>(TRACK_TX_PIN), static_cast<gpio_num_t>(TRACK_RX_PIN),
      TWAI_MODE_NORMAL);
  twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();  // RS SDK: 1Mbps standard frame
  twai_filter_config_t flt = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &flt) != ESP_OK) return false;
  return twai_start() == ESP_OK;
}

void DjiCanDriver::poll() {
  twai_message_t m;
  while (twai_receive(&m, 0) == ESP_OK) {
    if (!(m.identifier == dji::kCanIdGimbalToHost && !m.extd)) continue;
    ++rx_count_;
    std::vector<std::vector<uint8_t>> done;
    parser_.feed(m.data, m.data_length_code, done);
    for (auto& f : done) {
      float yaw;
      if (dji::parseAttitudeResponse(f, yaw)) {
        last_yaw_ = yaw;
        yaw_valid_ = true;
        ++attitude_rx_count_;
      }
    }
  }
}

bool DjiCanDriver::send(const std::vector<uint8_t>& frame) {
  // DJI 프레임은 8바이트 초과 → CAN 표준 프레임 여러 개로 분할 전송.
  // 수신측이 스트림 재조립 방식임(Handle.cpp)이 확인되어 순차 청크 전송이면 충분.
  for (size_t off = 0; off < frame.size(); off += 8) {
    twai_message_t m = {};
    m.identifier = dji::kCanIdHostToGimbal;
    m.extd = 0;
    m.data_length_code = static_cast<uint8_t>(
        (frame.size() - off) >= 8 ? 8 : (frame.size() - off));
    for (size_t i = 0; i < m.data_length_code; ++i) m.data[i] = frame[off + i];
    if (twai_transmit(&m, pdMS_TO_TICKS(20)) != ESP_OK) return false;
  }
  ++tx_count_;
  return true;
}

// 시디리얼 레이트 스텝: +0.1°를 239틱(23.9초)에 걸쳐 증분 이동.
//   ω_sidereal = 360° / 86164.09s = 0.00417807°/s → 0.1° 당 23934ms
// time_for_action은 0.1s 단위 uint8이라 최대 25.5초 → 239로 커버 가능.
bool DjiCanDriver::trackYawStep(int16_t deci_deg) {
  return send(dji::positionCommand(deci_deg, 0, 0,
                                   dji::kCtrlIncrementalAllAxes, /*time=*/239));
}

bool DjiCanDriver::ditherPitch(int16_t deci_deg) {
  return send(dji::positionCommand(0, 0, deci_deg,
                                   dji::kCtrlIncrementalAllAxes, /*time=*/20));  // 2초 이동
}

bool DjiCanDriver::shutterOpen()  { return send(dji::cameraShutterCommand(true)); }
bool DjiCanDriver::shutterClose() { return send(dji::cameraShutterCommand(false)); }

bool DjiCanDriver::getYawDeg(float& deg) {
  deg = last_yaw_;
  return yaw_valid_;
}

// 진단: getAttitude 전송 후 rx 증가를 500ms 동안 기다려 CAN 응답 확인.
std::string DjiCanDriver::probe() {
  const uint32_t before = attitude_rx_count_;
  if (!send(dji::getAttitudeCommand())) return "{\"ok\":false,\"reason\":\"tx-failed\"}";
  const uint32_t t0 = millis();
  while (millis() - t0 < 500) { poll(); delay(10); }
  char buf[96];
  snprintf(buf, sizeof(buf), "{\"ok\":%s,\"rxDelta\":%u}",
           attitude_rx_count_ > before ? "true" : "false",
           static_cast<unsigned>(attitude_rx_count_ - before));
  return buf;
}
