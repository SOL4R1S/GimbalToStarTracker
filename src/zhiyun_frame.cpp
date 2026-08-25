//
// zhiyun_frame.cpp — ZHIYUN BLE 프레임 CRC/빌더/파서 구현
//

#include "zhiyun_frame.h"

namespace zhiyun {

// 헤더(마직+길이) 4바이트 + 최소 body(sync2+inc+dir+cmd) 5바이트 + CRC 2바이트
static constexpr size_t kMinFrameLen = 4 + 5 + 2;

uint16_t crc16_xmodem(const uint8_t* data, size_t len) {
  uint16_t crc = 0;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int b = 0; b < 8; ++b) {
      if (crc & 0x8000)
        crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
      else
        crc = static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

std::vector<uint8_t> zyFrame(uint8_t inc, uint8_t cmd,
                             const std::vector<uint8_t>& payload) {
  // body: sync(2) + inc + dir + cmd + payload
  std::vector<uint8_t> body;
  body.reserve(5 + payload.size());
  body.push_back(0x18);
  body.push_back(0x12);
  body.push_back(inc);
  body.push_back(kDirRequest);
  body.push_back(cmd);
  body.insert(body.end(), payload.begin(), payload.end());

  std::vector<uint8_t> frame;
  frame.reserve(kMinFrameLen + payload.size());
  frame.push_back(kMagicHi);
  frame.push_back(kMagicLoReq);
  frame.push_back(static_cast<uint8_t>(body.size() & 0xFF));        // len LE
  frame.push_back(static_cast<uint8_t>((body.size() >> 8) & 0xFF));
  frame.insert(frame.end(), body.begin(), body.end());
  const uint16_t crc = crc16_xmodem(body.data(), body.size());
  frame.push_back(static_cast<uint8_t>(crc & 0xFF));                // CRC LE
  frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
  return frame;
}

bool ZyPacketParser::tryExtract(ZyFrame& frame) {
  while (true) {
    // 마직 탐색
    size_t pos = 0;
    while (pos + 1 < buf_.size() &&
           !(buf_[pos] == kMagicHi &&
             (buf_[pos + 1] == kMagicLoReq || buf_[pos + 1] == kMagicLoRsp)))
      ++pos;
    if (pos > 0) buf_.erase(buf_.begin(), buf_.begin() + pos);  // 노이즈 폐기
    if (buf_.size() < 4) return false;

    const uint16_t len =
        static_cast<uint16_t>(buf_[2] | (buf_[3] << 8));            // LE
    const size_t total = 4 + len + 2;
    if (len < 5 || total > 512) {   // 비정상 길이 → 마직 오탐, 1바이트 전진
      buf_.erase(buf_.begin());
      continue;
    }
    if (buf_.size() < total) return false;                          // 분할 대기

    const uint8_t* body = buf_.data() + 4;
    const uint16_t wire = static_cast<uint16_t>(
        buf_[4 + len] | (buf_[4 + len + 1] << 8));
    if (crc16_xmodem(body, len) != wire) {                          // CRC 불량
      buf_.erase(buf_.begin());
      continue;
    }

    frame.magic_lo = buf_[1];
    frame.sync = static_cast<uint16_t>((body[0] << 8) | body[1]);   // 빅엔디안
    frame.inc = body[2];
    frame.dir = body[3];
    frame.cmd = body[4];
    frame.data.assign(body + 5, body + len);
    buf_.erase(buf_.begin(), buf_.begin() + total);
    return true;
  }
}

void ZyPacketParser::feed(const uint8_t* bytes, size_t n,
                          std::vector<ZyFrame>& out) {
  buf_.insert(buf_.end(), bytes, bytes + n);
  ZyFrame f;
  while (tryExtract(f)) out.push_back(std::move(f));
}

}  // namespace zhiyun
