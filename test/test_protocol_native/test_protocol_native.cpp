//
// 네이티브(호스트) 단위테스트 — DJI 프로토콜 레이어 검증
// 실행: g++ -std=c++17 -I include src/djiprotocol.cpp \
//          test/test_protocol_native/test_protocol_native.cpp -o /tmp/proto_test && /tmp/proto_test
//
// 검증 기준:
//  * DJI 공식 문서 §3.3 샘플 프레임(AA 1A 00 03 ... 7B 40 97 BE)과 바이트 단위 일치
//    (와이어 SEQ "22 11" = 숫자 0x2211)
//  * CRC16/CRC32 공식 벡터
//  * 스트림 파서의 프레임 경계 분할 내성
//  * 시디리얼 스텝 수치 오차
//

#include "djiprotocol.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

using namespace dji;

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("PASS %s\n", msg); } \
    else { printf("FAIL %s\n", msg); ++fails; } } while (0)

int main() {
    // ---- 1. CRC 공식 벡터 ----
    // 헤더(SOF~SEQ): AA 1A 00 03 00 00 00 00 22 11 → CRC16 = A2 42 (LE)
    const uint8_t hdr[10] = {0xAA,0x1A,0x00,0x03,0x00,0x00,0x00,0x00,0x22,0x11};
    CHECK(crc16(hdr, 10) == 0x42A2, "crc16 official vector");

    // ---- 2. 공식 샘플 프레임 바이트 단위 재현 ----
    // §3.3: 위치제어(yaw+3.2° roll+4.8° pitch+6.4° 절대모드 2s), seq=0x2211
    const uint8_t sample[26] = {
        0xAA,0x1A,0x00,0x03,0x00,0x00,0x00,0x00,0x22,0x11,0xA2,0x42,
        0x0E,0x00, 0x20,0x00,0x30,0x00,0x40,0x00,0x01,0x14,
        0x7B,0x40,0x97,0xBE };
    std::vector<uint8_t> data = {0x20,0x00, 0x30,0x00, 0x40,0x00, 0x01, 0x14};
    std::vector<uint8_t> f = makeFrame(kCmdTypeReplyRequired, kCmdSetGimbal, 0x00, data, 0x2211);
    CHECK(f.size() == 26, "frame length 26");
    CHECK(std::memcmp(f.data(), sample, 26) == 0, "official sample byte-exact");

    // ---- 3. 스트림 파서: 임의 청크 분할 내성 ----
    {
        PacketParser p;
        std::vector<std::vector<uint8_t>> done;
        size_t off = 0;
        size_t chunks[] = {3, 1, 7, 5, 4, 6};   // 26바이트를 불규칙 분할
        for (size_t c : chunks) {
            if (off >= f.size()) break;
            size_t n = (off + c <= f.size()) ? c : f.size() - off;
            p.feed(f.data() + off, n, done);
            off += n;
        }
        CHECK(done.size() == 1 && done[0].size() == 26 &&
              std::memcmp(done[0].data(), sample, 26) == 0,
              "parser reassembles split frame");
    }

    // ---- 4. 셔터 명령 형태 ----
    {
        auto so = cameraShutterCommand(true);
        CHECK(so.size() == 20 && so[12] == 0x0D && so[13] == 0x00 &&
              so[14] == 0x01 && so[15] == 0x00, "camera shutter-open CmdSet/Data");
        PacketParser p; std::vector<std::vector<uint8_t>> done;
        p.feed(so.data(), so.size(), done);
        CHECK(done.size() == 1, "self-built frame passes own parser");
    }

    // ---- 5. 자세 응답 필드 검증 ----
    {
        auto response = makeFrame(0x03, kCmdSetGimbal, 0x02,
                                  {0x00, 0x00, 0xD2, 0x04, 0x00, 0x00, 0x00, 0x00},
                                  0x2211);
        float yaw = 0.f;
        CHECK(parseAttitudeResponse(response, yaw) && std::fabs(yaw - 123.4f) < 0.01f,
              "attitude response decodes yaw from payload");
        auto wrong = response;
        wrong[13] = 0x01;
        CHECK(!parseAttitudeResponse(wrong, yaw), "non-attitude response rejected");
        response.resize(18);
        CHECK(!parseAttitudeResponse(response, yaw), "truncated attitude response rejected");
    }

    // ---- 5. 시디리얼 스텝 수치 ----
    {
        double ours  = 0.1 * 1000.0 / 23934.0;                 // °/s (+0.1°/23934ms)
        double truth = 360.0 / 86164.0905;
        double ppm   = std::fabs(ours - truth) / truth * 1e6;
        printf("INFO sidereal=%.7f deg/s ours=%.7f deg/s err=%.1f ppm\n",
               truth, ours, ppm);
        CHECK(ppm < 500.0, "sidereal step error < 500ppm");

        // time_for_action=239틱(23.90s)이 주기 23.934s 안에 들어가는지
        // (남은 ~34ms는 홀드 — 호스트 클럭 스케줄링으로 평균 레이트는 정확)
        CHECK(239u * 100u <= 23934u, "move duration fits inside schedule period");
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
