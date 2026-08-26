#include "djiprotocol.h"

namespace dji {
namespace {

inline void putLe32(std::vector<uint8_t>& v, size_t pos, uint32_t x) {
  v[pos]     = static_cast<uint8_t>(x & 0xFF);
  v[pos + 1] = static_cast<uint8_t>((x >> 8) & 0xFF);
  v[pos + 2] = static_cast<uint8_t>((x >> 16) & 0xFF);
  v[pos + 3] = static_cast<uint8_t>((x >> 24) & 0xFF);
}

inline uint16_t seqNext() {
  // ConstantRobotics CmdCombine::seq_num() 동일 정책: 시작 0x2211, 0xFFFD 넘으면 0x0002로 감김
  static uint16_t s = 0x2210;
  if (s >= 0xFFFD) s = 0x0002;
  return ++s;
}

inline void putLe16(std::vector<uint8_t>& v, size_t pos, uint16_t x) {
  v[pos]     = static_cast<uint8_t>(x & 0xFF);
  v[pos + 1] = static_cast<uint8_t>(x >> 8);
}

}  // namespace

// ---- CRC16 : DJI custom_crc16.c 와 동일 (pycrc table-driven, 반사형) ----
uint16_t crc16(const uint8_t* data, size_t len) {
  static uint16_t tbl[256];
  static bool ready = false;
  if (!ready) {
    for (uint16_t i = 0; i < 256; ++i) {
      uint16_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? static_cast<uint16_t>((c >> 1) ^ 0xA001u)
                                              : static_cast<uint16_t>(c >> 1);
      tbl[i] = c;
    }
    ready = true;
  }
  uint16_t c = 0x3AA3;  // rev16(0xC55C) — 공식 소스 crc_init() 값
  while (len--) {
    c = static_cast<uint16_t>((c >> 8) ^ tbl[(c ^ *data++) & 0xFF]);
  }
  return c;
}

// ---- CRC32 : 반사형, init 0x00003AA3 (= rev32(0xC55C0000)) — 샘플 프레임으로 검증 ----
uint32_t crc32(const uint8_t* data, size_t len) {
  uint32_t c = 0x00003AA3u;
  while (len--) {
    c ^= *data++;
    for (int k = 0; k < 8; ++k) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
  }
  return c;
}

std::vector<uint8_t> makeFrame(uint8_t cmd_type, uint8_t cmd_set, uint8_t cmd_id,
                               const std::vector<uint8_t>& data, uint16_t seq) {
  const size_t total = 10 /*헤더*/ + 2 /*CRC16*/ + 2 /*Set,ID*/ + data.size() + 4 /*CRC32*/;
  std::vector<uint8_t> f(total, 0);

  f[0] = 0xAA;                                   // SOF
  f[1] = static_cast<uint8_t>(total & 0xFF);     // Ver/Len — 버전 0, LSB first
  f[2] = static_cast<uint8_t>(total >> 8);
  f[3] = cmd_type;
  f[4] = 0x00;                                   // ENC: 미암호화
  f[5] = f[6] = f[7] = 0x00;                     // RES
  f[8] = static_cast<uint8_t>(seq >> 8);         // SEQ (MSB first — 참조구현과 동일)
  f[9] = static_cast<uint8_t>(seq & 0xFF);

  putLe16(f, 10, crc16(f.data(), 10));           // CRC16: 헤더 10바이트

  f[12] = cmd_set;
  f[13] = cmd_id;
  for (size_t j = 0; j < data.size(); ++j) f[14 + j] = data[j];

  putLe32(f, 14 + data.size(), crc32(f.data(), 14 + data.size()));
  return f;
}

std::vector<uint8_t> makeFrame(uint8_t cmd_type, uint8_t cmd_set, uint8_t cmd_id,
                               const std::vector<uint8_t>& data) {
  return makeFrame(cmd_type, cmd_set, cmd_id, data, seqNext());
}

std::vector<uint8_t> positionCommand(int16_t yaw_deci, int16_t roll_deci, int16_t pitch_deci,
                                     uint8_t ctrl_byte, uint8_t time_tenths) {
  auto le = [](int16_t v) {
    return std::vector<uint8_t>{static_cast<uint8_t>(v & 0xFF),
                                static_cast<uint8_t>((v >> 8) & 0xFF)};
  };
  std::vector<uint8_t> d;
  auto ap = [&d](const std::vector<uint8_t>& x) { d.insert(d.end(), x.begin(), x.end()); };
  ap(le(yaw_deci)); ap(le(roll_deci)); ap(le(pitch_deci));
  d.push_back(ctrl_byte);
  d.push_back(time_tenths);
  return makeFrame(kCmdTypeReplyRequired, kCmdSetGimbal, 0x00, d);
}

std::vector<uint8_t> getAttitudeCommand() {
  return makeFrame(kCmdTypeReplyRequired, kCmdSetGimbal, 0x02, {0x01});
}

bool parseAttitudeResponse(const std::vector<uint8_t>& frame, float& yaw_deg) {
  // 12-byte header + CmdSet/CmdID + rc/type + yaw/roll/pitch + CRC32.
  if (frame.size() < 26 || frame[12] != kCmdSetGimbal || frame[13] != 0x02 ||
      frame[14] != 0x00) return false;
  const int16_t yaw = static_cast<int16_t>(
      static_cast<uint16_t>(frame[16]) | (static_cast<uint16_t>(frame[17]) << 8));
  yaw_deg = static_cast<float>(yaw) * 0.1f;
  return true;
}

std::vector<uint8_t> cameraShutterCommand(bool open) {
  uint16_t v = open ? 0x0001 : 0x0002;
  return makeFrame(kCmdTypeReplyRequired, kCmdSetCamera, 0x00,
                   {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8)});
}

// ---- PacketParser : 스트림 재조립 (ConstantRobotics Handle.cpp 수신경로 방식) ----
void PacketParser::feed(const uint8_t* bytes, size_t n,
                        std::vector<std::vector<uint8_t>>& completed) {
  while (n--) {
    uint8_t b = *bytes++;
    buf_.push_back(b);

    if (buf_.size() == 1) {
      if (b != 0xAA) { buf_.clear(); }          // SOF 대기
      continue;
    }
    if (buf_.size() == 2) {                      // 길이 하위바이트
      need_ = b;
      continue;
    }
    if (buf_.size() == 3) {                      // 길이 상위 2비트 + 버전 마스크
      need_ |= static_cast<size_t>(b & 0x03) << 8;
      if (need_ < 16 || need_ > 64) {            // 비정상 길이 → 재동기화
        buf_.clear(); need_ = 0;
      }
      continue;
    }
    if (need_ == 0 || buf_.size() < need_) continue;

    // 완성: CRC 이중 검증
    const std::vector<uint8_t>& f = buf_;
    bool ok = crc16(f.data(), 10) == static_cast<uint16_t>(f[10] | (f[11] << 8));
    if (ok) {
      size_t body = f.size() - 4;
      uint32_t want = static_cast<uint32_t>(f[body]) |
                      (static_cast<uint32_t>(f[body + 1]) << 8) |
                      (static_cast<uint32_t>(f[body + 2]) << 16) |
                      (static_cast<uint32_t>(f[body + 3]) << 24);
      ok = (crc32(f.data(), body) == want);
    }
    if (ok) completed.push_back(f);
    buf_.clear();
    need_ = 0;
  }
}

}  // namespace dji
