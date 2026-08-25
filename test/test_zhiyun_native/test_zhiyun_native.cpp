//
// 네이티브(호스트) 단위테스트 — ZHIYUN 프레임 계층 검증
// 실행: g++ -std=c++17 -I include src/zhiyun_frame.cpp \
//          test/test_zhiyun_native/test_zhiyun_native.cpp -o /tmp/zy_test && /tmp/zy_test
//
// 골든벡터 출처: petermaguire.xyz/posts/zhiyun-weebil-s-ble-protocol 의 실제
// HCI snoop 캡처 바이트. ⚠ 컨텍스트 전사본의 recv2(inc=01)는 CRC가 맞지 않는
// 전사 오류로 확인됨 — 원문 캡처는 `Sent: ...1812 02 0104... ↔ Recv: ...
// 1812 02 1004...` 쌍(inc=02)이며, 아래 recv2_fixed가 원문 그대로다.
//

#include "zhiyun_frame.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace zhiyun;

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("PASS %s\n", msg); } \
    else { printf("FAIL %s\n", msg); ++fails; } } while (0)

static std::vector<uint8_t> hex(const char* s) {
  std::vector<uint8_t> v;
  for (size_t i = 0; s[i] && s[i + 1]; i += 2)
    v.push_back(static_cast<uint8_t>(std::stoi(std::string(s + i, 2), nullptr, 16)));
  return v;
}

int main() {
  // ---- 1. CRC16-XMODEM 표준 벡터 ----
  CHECK(crc16_xmodem((const uint8_t*)"123456789", 9) == 0x31C3,
        "crc16_xmodem standard vector 123456789 -> 0x31C3");

  // ---- 2. 골든벡터 파싱 + CRC (원문 HCI 캡처) ----
  struct Golden {
    const char* hexstr;
    uint8_t magic_lo, inc, dir, cmd;
    const char* data_hex;
    const char* label;
  };
  const Golden goldens[] = {
      {"243c080018120101020000006f76", 0x3C, 0x01, kDirRequest,  0x02, "000000",
       "sent pan-speed stop"},
      {"243e0800181201100200330507d8", 0x3E, 0x01, kDirResponse, 0x02, "003305",
       "recv echo of pan-speed"},
      {"243e0800181202100400cf007737", 0x3E, 0x02, kDirResponse, 0x04, "00cf00",
       "recv version reply v2.07"},
      {"243c08001812020104000000169f", 0x3C, 0x02, kDirRequest,  0x04, "000000",
       "sent get-version"},
      {"243c0800181203010500000002ac", 0x3C, 0x03, kDirRequest,  0x05, "000000",
       "sent cmd05"},
      {"243e08001812031005000800a08b", 0x3E, 0x03, kDirResponse, 0x05, "000800",
       "recv cmd05 reply"},
      {"243c080018120401020000006e35", 0x3C, 0x04, kDirRequest,  0x02, "000000",
       "sent pan-speed again"},
      {"243e08001812041002003305069b", 0x3E, 0x04, kDirResponse, 0x02, "003305",
       "recv pan-speed echo"},
  };
  ZyPacketParser p;
  std::vector<ZyFrame> done;
  for (const Golden& g : goldens) {
    std::vector<uint8_t> raw = hex(g.hexstr);
    p.feed(raw.data(), raw.size(), done);
  }
  CHECK(done.size() == 8, "all 8 golden frames parse");
  bool fields_ok = true, data_ok = true, crc_path_ok = true;
  if (done.size() == 8) {
    for (size_t i = 0; i < 8; ++i) {
      const Golden& g = goldens[i];
      if (done[i].magic_lo != g.magic_lo || done[i].inc != g.inc ||
          done[i].dir != g.dir || done[i].cmd != g.cmd) {
        fields_ok = false;
        printf("  mismatch at %zu: %s\n", i, g.label);
      }
      if (hex(g.data_hex) != done[i].data) data_ok = false;
    }
  } else {
    crc_path_ok = false;
  }
  // CRC 통과 경로로만 도달했는지: 파서는 CRC 불량을 폐기하므로 개수 일치 자체가 증명.
  CHECK(crc_path_ok && fields_ok, "golden magic/inc/dir/cmd fields byte-exact");
  CHECK(data_ok, "golden data payloads byte-exact");

  // ---- 3. 빌더 왕복: 골든벡터 sent1 바이트 단위 재현 ----
  {
    std::vector<uint8_t> f = zyFrame(0x01, 0x02, {0x00, 0x00, 0x00});
    CHECK(f == hex("243c080018120101020000006f76"),
          "builder reproduces golden sent frame byte-exact");
    ZyPacketParser rp;
    std::vector<ZyFrame> rd;
    rp.feed(f.data(), f.size(), rd);
    CHECK(rd.size() == 1 && rd[0].inc == 0x01 && rd[0].cmd == 0x02 &&
              rd[0].data == std::vector<uint8_t>({0x00, 0x00, 0x00}),
          "builder output passes own parser (roundtrip)");
  }

  // ---- 4. 한 write에 두 패킷 + 노이즈 프리픽스 ----
  {
    ZyPacketParser pp;
    std::vector<ZyFrame> pd;
    // 원문 캡처 그대로: 앞 패킷의 꼬리 b58d + 두 패킷이 한 write에
    std::vector<uint8_t> raw =
        hex("b58d243c08001812370101100008b58d243c080018123801021000086ad3");
    pp.feed(raw.data(), raw.size(), pd);
    CHECK(pd.size() == 2, "two packets in one write recovered");
    CHECK(pd.size() == 2 && pd[0].inc == 0x37 && pd[0].cmd == 0x01 &&
              pd[1].inc == 0x38 && pd[1].cmd == 0x02,
          "two-packet inc/cmd sequence correct");
  }

  // ---- 5. 분할 피드 재조립 (바이트 단위 스트리밍) ----
  {
    ZyPacketParser sp;
    std::vector<ZyFrame> sd;
    std::vector<uint8_t> raw = hex(
        "243c080018120101020000006f76"
        "243e0800181202100400cf007737");
    for (uint8_t b : raw) sp.feed(&b, 1, sd);   // 1바이트씩
    CHECK(sd.size() == 2 && sd[0].cmd == 0x02 &&
              sd[1].cmd == 0x04 && sd[1].data ==
                  std::vector<uint8_t>({0x00, 0xCF, 0x00}),
          "byte-by-byte split feed reassembles");
  }

  // ---- 6. 하트비트(0x1815) 프레임도 범용 파싱 ----
  {
    ZyPacketParser hp;
    std::vector<ZyFrame> hd;
    std::vector<uint8_t> raw =
        hex("243e0c001815080001805010c2010000984b");
    hp.feed(raw.data(), raw.size(), hd);
    CHECK(hd.size() == 1 && hd[0].sync == 0x1815 && hd[0].dir == 0x00,
          "heartbeat frame (sync 1815) parses");
  }

  // ---- 7. CRC 불량 폐기 후 재동기화 ----
  {
    ZyPacketParser cp;
    std::vector<ZyFrame> cd;
    // 정상 프레임 뒤에 CRC 1비트 깨진 동일 프레임 + 다음 정상 프레임 이어 붙임
    std::vector<uint8_t> good = hex("243c080018120101020000006f76");
    std::vector<uint8_t> corrupt = good;
    corrupt[corrupt.size() - 1] ^= 0x01;               // CRC LSB 비트 반전
    std::vector<uint8_t> next =
        hex("243e0800181202100400cf007737");
    std::vector<uint8_t> stream = good;
    stream.insert(stream.end(), corrupt.begin(), corrupt.end());
    stream.insert(stream.end(), next.begin(), next.end());
    cp.feed(stream.data(), stream.size(), cd);
    CHECK(cd.size() == 2 && cd[0].cmd == 0x02 && cd[1].cmd == 0x04,
          "corrupt-CRC frame dropped, parser resyncs to next packet");

    // 마직 오탐 내성: 임의 바이트 중 '24 3C' 등장 + 엉터리 길이 → 폐기 후 복구
    ZyPacketParser fp;
    std::vector<ZyFrame> fd;
    std::vector<uint8_t> junk = {0x01, 0x24, 0x3C, 0xFF, 0xFF, 0xAA};
    junk.insert(junk.end(), good.begin(), good.end());
    fp.feed(junk.data(), junk.size(), fd);
    CHECK(fd.size() == 1 && fd[0].cmd == 0x02,
          "false-magic garbage skipped, real frame recovered");
  }

  printf(fails ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", fails);
  return fails ? 1 : 0;
}
