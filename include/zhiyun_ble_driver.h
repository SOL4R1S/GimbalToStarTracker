#pragma once
//
// zhiyun_ble_driver.h — ZHIYUN Weebill-S / Crane M3 세대 BLE 드라이버 (스텁)
//
// 조사 결과 (2026-08, 커뮤니티 역설계 기반 — 하드웨어 미검증):
//  * 서비스 0xFEE9, write char d44bc439-abfd-45a2-b575-925416129600
//  * 프레임: 24 3C | len(2 LE) | 18 12 | inc | dir(01=req,10=rsp) | cmd | data | CRC16-XMODEM(LE)
//    - CRC는 length 다음부터 data까지 (petermaguire.xyz/posts/zhiyun-weebil-s-ble-protocol)
//  * pan 속도 cmd 0x02, payload `10 | u16LE`: 하위 12비트 크기, bit12 방향.
//    실측(Crane M3): 300 ≈ 33°/s ⇒ 약 0.11°/s per LSB → 시디리얼(0.00418°/s)은 LSB 미만!
//    ⇒ 지윤텍도 DJI와 동일한 "연속 속도가 아니라 연쇄 증분 위치이동" 전략 필요.
//  * pan 절대위치 cmd 0x08 (`10`+angle i16LE), roll은 위치제어 실측 정밀도 0.10° 확인됨.
//    단, 각도 스케일 표기(degrees/65535)가 불안정 — 펌웨어별 보정 계수 캘리브레이션 필수.
//  * 속도 명령은 ~200ms 주기 반복 필요(래치 안 됨). 위치 명령의 연쇄 동작 여부는 실측 필요.
//  * 페어링(보딩) 필수 — HID over GATT 노출로 미페어 시 0.5s 내 접속 해제됨.
//    ESP32는 NimBLE bonding 사용 (참고구현: github.com/VictorEscribano/zhiyun-gimbal-ble esp32/)
//
// 결론: 지원 가능성 높음. 순서 — ①bleebil/zhiyun-gimbal-ble 코드로 수동 실험,
//       ②pan 위치 cmd 0x08 소증분 연쇄 동작 확인, ③본 클래스 구현.
//

class ZhiyunBleDriver : public GimbalDriver {
 public:
  const char* name() const override { return "ZHIYUN (BLE, 스텁)"; }
  bool begin() override { return false; }          // TODO: NimBLE bonding + 0xFEE9 연결
  bool trackYawStep(int16_t) override { return false; }  // TODO: cmd 0x08 증분 연쇄
  bool ditherPitch(int16_t) override { return false; }
  bool shutterOpen() override { return false; }    // TODO: cmd 0x20 press (`c0 3c 00`)
  bool shutterClose() override { return false; }   // TODO: release 서브커맨드 확인 필요
};
