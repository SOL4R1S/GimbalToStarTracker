#pragma once
//
// djiprotocol.h — DJI R(S) SDK 프로토콜 프레임 빌더/파서 (호스트·ESP32 공용, 순수 C++17)
//
// 근거 문서/소스:
//  * DJI_R_SDK_Protocol_and_User_Interface_EN_v2.5.pdf  §2 데이터포맷, §3.2 CRC, §3.3 샘플
//  * DJI 배포 custom_crc16.c / custom_crc32.c (pycrc 생성, 공식 구현)
//  * ConstantRobotics/DJIR_SDK (CAN 전송 참조구현)
//
// 프레임 레이아웃:
//   SOF(0xAA) | Ver/Len(2, LE) | CmdType | ENC | RES(3) | SEQ(2) | CRC16(2, LE)
//   | CmdSet | CmdID | DATA(n) | CRC32(4, LE)
//   * Ver/Len 상위 6비트 = 버전(0), 하위 10비트 = 전체 프레임 길이
//   * CRC16: 헤더 10바이트(SOF~SEQ)에 대해 계산
//   * CRC32: CRC16 포함 헤더 전체 + CmdSet/CmdID/DATA 에 대해 계산
//

#include <cstdint>
#include <cstddef>
#include <vector>

namespace dji {

// CAN 식별자 — 짐벌 수신 = 0x223, 짐벌 송신 = 0x222
// (문서 Fig.37/38, ConstantRobotics/DJIR_SDK connect() 와 일치)
constexpr uint16_t kCanIdHostToGimbal = 0x223;
constexpr uint16_t kCanIdGimbalToHost = 0x222;

constexpr uint8_t  kCmdTypeReplyRequired = 0x03;
constexpr uint8_t  kCmdSetGimbal  = 0x0E;
constexpr uint8_t  kCmdSetCamera  = 0x0D;

// 위치제어 ctrl_byte (§2.3.4.1)
//   bit0   : 0 = 증분(incremental), 1 = 절대(absolute)
//   bit1..3: 축 유효 플래그 (0 = 유효) — yaw/roll/pitch 순
constexpr uint8_t kCtrlIncrementalAllAxes = 0x00;
constexpr uint8_t kCtrlAbsoluteAllAxes    = 0x01;

// ---- CRC (공식 샘플 프레임으로 바이트 단위 검증: test/native_test.cpp) ----
// CRC16: 반사형, poly 0x8005(반사 0xA001), init 0x3AA3 (= rev16(0xC55C)), xorout 0
// CRC32: 반사형, poly 0x04C11DB7(반사 0xEDB88320), init 0x00003AA3 (= rev32(0xC55C0000)), xorout 0
uint16_t crc16(const uint8_t* data, size_t len);
uint32_t crc32(const uint8_t* data, size_t len);

// ---- 프레임 생성 ----
std::vector<uint8_t> makeFrame(uint8_t cmd_type, uint8_t cmd_set, uint8_t cmd_id,
                               const std::vector<uint8_t>& data,
                               uint16_t seq);            // seq 명시 (단위 테스트용)
std::vector<uint8_t> makeFrame(uint8_t cmd_type, uint8_t cmd_set, uint8_t cmd_id,
                               const std::vector<uint8_t>& data);

// §2.3.4.1 위치 제어 — 각도 단위 0.1°, 시간 단위 0.1s (최대 255 = 25.5s)
std::vector<uint8_t> positionCommand(int16_t yaw_deci, int16_t roll_deci, int16_t pitch_deci,
                                     uint8_t ctrl_byte, uint8_t time_tenths);

// §2.3.4.3 짐벌 자세각 요청 (type 0x01 = attitude)
std::vector<uint8_t> getAttitudeCommand();

// 자세 응답 프레임에서 yaw를 도 단위로 추출. frame은 CRC 검증을 통과한 값이어야 한다.
bool parseAttitudeResponse(const std::vector<uint8_t>& frame, float& yaw_deg);

// §2.3.5 카메라 제어 — 0x0001 셔터 열림 / 0x0002 해제 (유선 컨트롤 케이블 경로)
std::vector<uint8_t> cameraShutterCommand(bool open);

// ---- 수신 파서 ----
// CAN 프레임열은 투명한 바이트 스트림으로 취급된다(ConstantRobotics Handle.cpp 의
// 수신 경로와 동일 방식). SOF 동기화 → 길이 파싱 → CRC16/32 이중 검증 후 완성 패킷 반환.
class PacketParser {
 public:
  void feed(const uint8_t* bytes, size_t n,
            std::vector<std::vector<uint8_t>>& completed);
 private:
  std::vector<uint8_t> buf_;   // 진행 중 패킷 버퍼
  size_t need_ = 0;            // 목표 길이 (0 = SOF 대기 중)
};

}  // namespace dji
