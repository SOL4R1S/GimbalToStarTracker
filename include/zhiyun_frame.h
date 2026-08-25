#pragma once
//
// zhiyun_frame.h — ZHIYUN Weebill-S / Crane BLE 프레임 계층 (호스트 테스트 가능)
//
// 와이어 형식 (petermaguire HCI 캡처로 바이트 단위 검증됨 — test_zhiyun_native 참조):
//   24 3C/3E | len u16LE | sync(18 12) | inc | dir | cmd | data... | CRC16-XMODEM u16LE
//   * len = body 길이(sync부터 data까지). CRC 2바이트는 len에 불포함.
//   * CRC는 length 필드 "뒤"부터 data까지 계산하여 LE로 부착.
//   * magic: 앱→짐벌 0x243C, 짐벌→앱 0x243E. 파서는 양방향 모두 수용.
//   * dir: 요청=0x01, 응답=0x10. 하트비트(0x1815)는 dir 대신 mode 바이트.
//   * 한 BLE write에 패킷 0/1/2개가 붙어 오거나 하나가 여러 write로 분할될 수 있음.
//

#include <cstddef>
#include <cstdint>
#include <vector>

namespace zhiyun {

constexpr uint8_t kMagicHi = 0x24;
constexpr uint8_t kMagicLoReq = 0x3C;
constexpr uint8_t kMagicLoRsp = 0x3E;

// dir 바이트
constexpr uint8_t kDirRequest = 0x01;
constexpr uint8_t kDirResponse = 0x10;

// 명령 ID (petermaguire 문서 표)
constexpr uint8_t kCmdTiltSpeed = 0x01;   // data: 속도 페이로드 (펄스식)
constexpr uint8_t kCmdPanSpeed = 0x02;    // data: 속도 페이로드
constexpr uint8_t kCmdRollSpeed = 0x03;
constexpr uint8_t kCmdGetVersion = 0x04;
constexpr uint8_t kCmdTiltPosSet = 0x06;  // 배터리 조회와 충돌 — 미확인
constexpr uint8_t kCmdRollPosSet = 0x07;
constexpr uint8_t kCmdPanPosSet = 0x08;   // data: `10` + 위치 i16LE
constexpr uint8_t kCmdButtonPress = 0x20; // data: `c0 3c 00` = 셔터 press
constexpr uint8_t kCmdReadPanPos = 0x24;  // 응답 data: `00` + 위치

// CRC16-XMODEM (poly 0x1021, init 0x0000). 표준 벡터 "123456789" → 0x31C3.
uint16_t crc16_xmodem(const uint8_t* data, size_t len);

// 요청 프레임 빌더. 반환: [24 3C][len LE][18 12][inc][01][cmd][payload][CRC LE]
std::vector<uint8_t> zyFrame(uint8_t inc, uint8_t cmd,
                             const std::vector<uint8_t>& payload);

// 파싱된 완성 프레임 1개
struct ZyFrame {
  uint8_t magic_lo = 0;              // 0x3C 또는 0x3E
  uint16_t sync = 0;                 // 보통 0x1812, 하트비트 0x1815
  uint8_t inc = 0;
  uint8_t dir = 0;                   // 0x01 요청 / 0x10 응답 / 하트비트=mode
  uint8_t cmd = 0;
  std::vector<uint8_t> data;         // cmd 뒤 가변 데이터
};

// 스트림 파서: 임의 분할/다중 패킷/노이즈 프리픽스 내성.
// CRC 불량 프레임은 폐기하고 다음 마직 후보부터 재동기화.
class ZyPacketParser {
 public:
  // bytes 중 소비 가능한 만큼 누적하고, 완성된 프레임을 out 뒤에 추가.
  void feed(const uint8_t* bytes, size_t n, std::vector<ZyFrame>& out);
  void reset() { buf_.clear(); }

 private:
  // buf_에서 프레임 1개 추출 시도. 성공 시 frame 채우고 true.
  bool tryExtract(ZyFrame& frame);

  std::vector<uint8_t> buf_;
};

}  // namespace zhiyun
