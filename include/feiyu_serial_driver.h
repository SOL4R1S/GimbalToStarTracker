#pragma once
//
// feiyu_serial_driver.h — FeiyuTech 시리얼 드라이버 (스텁)
//
// 조사 결과 (2026-08):
//  * 구세대(G4/WG 등): fyproto.py — 프레이밍 0xAA55(long)/0x5AA5(short),
//    CRC-16-hqx(init 0xFFFF/0x0000), UART. 설정공간 중심 프로토콜.
//    참고: github.com/mudaltsov/fygimbal (sigrok 디코더 포함)
//  * 현세대(Weebill/AK/SCORP): 공개 SDK 없음. SCORP 통합문서에서 UART 115200 8N1,
//    `AA 55 ... FF` 형태의 채널매핑 명령 예시만 확인됨.
//    ⇒ 정밀 각도/속도 제어 가능 여부는 직접 스니핑(HCI/UART 로그)해야 하는 최고 난이도.
//  * 판정: DJI(공식 SDK)>지윤텍(역설계 완료)>Feiyu(역설계 자체가 선행과제).
//
// 착수 경로: ①USB-UART를 짐벌 서비스 포트에 연결, ②fygimbal 디코더로 부팅 트레이스 수집,
//            ③앱(ZY Play 대응물) 조작과 트레이스 대조로 이동 명령 식별.
//

class FeiyuSerialDriver : public GimbalDriver {
 public:
  const char* name() const override { return "FEIYU (UART, 스텁)"; }
  bool begin() override { return false; }          // TODO: 프로토콜 확정 후 구현
  bool trackYawStep(int16_t) override { return false; }
  bool ditherPitch(int16_t) override { return false; }
  bool shutterOpen() override { return false; }
  bool shutterClose() override { return false; }
};
