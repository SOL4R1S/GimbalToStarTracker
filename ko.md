# GimbalToStarTracker

[English](README.md) | 한국어

손들질 카메라 짐벌(DJI RS 시리즈)을 천체사진용 **적도의 마운트**로 바꾸는 ESP32 펌웨어입니다. HONOR 로봇폰의 '적도의 모드'와 동일한 원리를 ESP32 + CAN 트랜시버 + 3만 원대 부품으로 DIY 재현한 프로젝트.

```
A7M3 + DJI RS 4  +  ESP32(SN65HVD230)  →  시디리얼 추적 + 벌브 셔터 제어
                                        →  웨지 위에서 30초+ 점별 별사진
```

## 원리

적도의 마운트가 하는 일은 하나뿐입니다 — **지구 자전축과 정렬된 한 개 축을 정확히 한 속도로** 회시키는 것:

$$\omega_{sidereal} = \frac{360°}{86164.09\,\text{s}} = 0.004178°/\text{s} = 15.04″/\text{s}$$

DJI RS SDK의 위치 제어는 CAN 위에서 **0.1° 각도 단위 × 최대 25.5초 실행 시간**을 제공합니다. 이것으로 충분합니다:

| | 값 |
|---|---|
| 추적 명령 | 증분 yaw `+0.1°`를 `time_for_action = 239`틱(23.9초)에 걸쳐 실행 |
| 스케줄 주기 | `23,934 ms`, 호스트 클럭 앵커 방식(드리프트 누적 없음) |
| 레이트 오차 | 진짜 시디리얼 대비 **+19.6 ppm** |
| 셔터 | SDK 카메라 명령(`0x0D`)을 짐벌 유선 컨트롤 경로로 전달 → 진짜 벌브(30초 제한 없음) |

정확성 근거: DJI 공식 프로토콜 문서와 샘플 프레임 대조 — 프레임 빌더가 참조 바이트를 **바이트 단위로 재현**(CRC16 init `0x3AA3`, CRC32 init `0x00003AA3` 포함).

## 하드웨어

### 부품 목록 (BOM)

| # | 부품 | 사양 / 메모 | 예상 가격 |
|---|---|---|---|
| 1 | ESP32 DevKit | WROOM-32 또는 S3. CAN 컨트롤러(TWAI)가 칩에 내장 — 외부 CAN 컨트롤러 불필요. GPIO 2핀만 비어 있으면 됨(기본 GPIO21=TX, GPIO22=RX, `-DTRACK_TX_PIN/-DTRACK_RX_PIN`으로 변경 가능). | ₩5k–12k |
| 2 | CAN 트랜시버 모듈 | **SN65HVD230** (TI, 3.3V). 사용 핀: `D`(1), `GND`(2), `VCC`(3), `R`(4), `RS`(8), `CANH`(7), `CANL`(6). 5V 칩(MCP2551, TJA1050)은 로직 레벨 불일치로 **구매 금지**. 대부분의 모듈에 120Ω 점퍼 내장. | ₩2k–4k |
| 3 | 짐벌측 커넥터 | RSA/NATO 포트는 전용 6핀 커넥터. 조달 방법: (a) 저가 **"DJI RS 포커스 모터/휠 케이블"**을 절단·탭, (b) 애프터마켓 RSA 피그테일. 필요한 건 CANH·CANL·GND 세 선. | ₩5k–15k |
| 4 | 연결선·소모품 | 26–30 AWG 실리콘 와이어 4색(벤치 테스트는 듀폰 점퍼도 무방), 수축튜브, 납땜 용구. | ₩3k |
| 5 | 보조배터리 + 케이블 | DevKit 야외 전원. 5,000mAh면 며칠 버팀. | 보유 |
| 6 | 웨지 (위도 조절 삼각대 헤드) | pan축을 북극성에 향해야 함. 기어헤드·볼헤드 강하게 잠그면 광각은 대응 가능. | 보유 |
| 7 | 공구 | 멀티미터(종단·도통 확인), 납땜 인두, 니퍼. | — |

전자부품 합계 ≈ **₩15k–35k** (웨지·배터리 등 보유品 제외). ESP32 경로에는 USB-CAN 어댑터가 **불필요**합니다 — TWAI 페리페럴이 CAN을 네이티브 처리. (PC에서 먼저 프로토타입하고 싶다면 USBCAN-II + [ConstantRobotics/DJIR_SDK](https://github.com/ConstantRobotics/DJIR_SDK) 경로도 가능.)

### 짐벌의 CAN 노출 위치

**RS 4** 기준: RSA/NATO 포트는 **터치스크린 왼쪽**(오른쪽 NATO 레일은 기계 장착 전용). 핀 배열(DJI R SDK 문서 §3.1.2):

| 핀 | 신호 | 메모 |
|---|---|---|
| 1 | VCC | 8V ±0.4, 정격 0.8A — AD_COM에 액세서리 감지(10–100k 풀다운)돼야 출력 활성화 |
| 2 | CANL | ← 사용 |
| 3 | SBUS_RX | 미사용 |
| 4 | CANH | ← 사용 |
| 5 | AD_COM | 포트 전원 원할 경우 풀다운 필요 |
| 6 | GND | ← 필수 (공통 그라운드) |

이 표는 RS 2 세대 RSA 포트 기준 문서지만 RS 4가 같은 커넥터 패밀리와 Focus Wheel 호환성을 유지합니다. **첫 전원 인상 전 멀티미터로 재확인**하세요 — 트랜시버 모듈이 일종의 퓨즈 역할을 합니다.

커넥터 조달: 마켓에서 *"DJI RS focus motor cable"* 또는 *"Ronin RSA cable"* 검색 → 액세서리 쪽 끝을 잘라 CANH/CANL/GND 스트립. 짐벌 본체 납땜은 불필요하고 권장하지 않습니다.

### 배선

MCU ↔ 트랜시버:

| SN65HVD230 핀 | 연결 대상 |
|---|---|
| D (1) | ESP32 GPIO21 (TWAI TX) |
| R (4) | ESP32 GPIO22 (TWAI RX) |
| VCC (3) | ESP32 3V3 |
| GND (2) | ESP32 GND |
| RS (8) | **GND** — 고속 모드(1Mbps 필수, 슬로프 제한 사용 금지) |
| CANH (7) | → 짐벌 CANH |
| CANL (6) | → 짐벌 CANL |

트랜시버 ↔ 짐벌: 와이어 3가닥(CANH, CANL, GND). CAN이 차등신호라도 **ESP32 회로와 짐벌의 GND 공통화는 필수**입니다.

### 버스 종단 (120Ω)

모든 장비 **전원 OFF** 상태에서 케이블 끝단의 CANH–CANL 간 저항 측정:

- **약 60Ω** → 짐벌 내부에 종단 존재. 모듈의 120Ω 점퍼 **OFF**
- **매우 높음(kΩ 이상)** → 모듈의 120Ω 점퍼 ON
- 중간 값이 출렁이면 커넥터 반착 의심 — 재결합 후 재측정

### ESP32 전원

- **권장**: DevKit에 독립 보조배터리(USB). 짐벌 전원 계열과 분리되어 리스크 없이 밤새 동작.
- 고급: RSA 1번 핀(8V) → 벅 컨버터 → 5V/VIN. 단 AD_COM 풀다운이 있어야 포트가 전원을 출력하고, 0.8A 예산은 다른 액세서리와 공유됩니다.

### 브링업 순서

1. 전원 OFF 상태에서 와이어 3가닥 도통 확인 + 위 종단 측정
2. 펌웨어 먼저 플래시 후 ESP32 단독 전원: 폰이 AP 접속, `/probe`가 `{"ok":false,...}`면 CAN 미연결 상태라는 정상 신호
3. 짐벌 OFF → CANH/CANL/GND 결선 → 짐벌 전원 ON → `/probe`가 `{"ok":true}` 반환
4. 이후에 카메라 컨트롤 케이블 연결하고 테스트컷 실행

## 빠른 시작

```bash
pip3 install platformio
python3 -m platformio run -e esp32dev            # 빌드
python3 -m platformio run -e esp32dev -t upload   # 플래시
# 폰으로 WiFi 'GimbalToStarTracker' 접속 (기본 비번 astro1234) → http://192.168.4.1
```

**하드웨어 없이** 먼저 체험할 수 있습니다 — 실제 Tracker 상태머신을 구동하는 호스트 시뮬레이터:

```bash
mkdir -p build && g++ -std=c++17 -Wall -I include -I third_party \
  tools/simulator.cpp src/djiprotocol.cpp -o build/simulator
./build/simulator 8080        # → http://127.0.0.1:8080
```

호스트 단위 테스트(무하드웨어):

```bash
g++ -std=c++17 -I include src/djiprotocol.cpp test/test_protocol_native/test_protocol_native.cpp -o /tmp/t && /tmp/t
g++ -std=c++17 -I include test/test_tracker_native/test_tracker_native.cpp -o /tmp/t2 && /tmp/t2
g++ -std=c++17 -I include src/zhiyun_frame.cpp test/test_zhiyun_native/test_zhiyun_native.cpp -o /tmp/t3 && /tmp/t3
```

## 현장 사용

1. 밸런싱 → 짐벌 모드 스위치를 **FPV**로 → 블루투스 셔터 페어링 삭제(유선 전용)
2. RS 4 ↔ 카메라 컨트롤 케이블 연결 후 짐벌 셔터 버튼 반누름 — AF 반응이면 유선 채널 정상
3. ESP32 전원 → AstroTrack이 아닌 **GimbalToStarTracker** AP 접속 → `http://192.168.4.1`
4. 딜레이/노출/갭/프레임/디더링 설정 → 시작 → 폰은 치워두기
5. 보드의 BOOT 버튼 = 물리 시작/정지 토글

프레이밍 팁: 흐릿한 목표는 라이브뷰에 안 보입니다. **테스트컷**(ISO 12800 / 8s) 버튼으로 촬영해 카메라 LCD에서 확인하세요.

## 짐벌 지원 현황

| 브랜드 | 상태 | 경로 |
|---|---|---|
| DJI RS 3 Pro / RS 4 / RS 4 Pro / RS 5 | ✅ 구현 완료 (실기 브링업 대기) | 공식 RS SDK over CAN |
| ZHIYUN Weebill-S / Crane M3 세대 | 🟡 코드 완성, 빌드 게이트만 통과 | 역설계 BLE(`0xFEE9`, 보딩 필수). 속도 LSB ≈ 0.11°/s라 pan 위치 cmd `0x08` 미세 증분 연쇄 사용 — 실기 캘리브레이션 필요 |
| FeiyuTech Weebill/AK/SCORP | ❌ 조사 백로그 | 공개 SDK 없음. UART 스니핑이 선행 과제 |

새 브랜드 추가 = [`include/gimbal_driver.h`](include/gimbal_driver.h) 인터페이스 구현(~8개 메서드). 조사 결과가 주석으로 정리된 스텁: [`include/zhiyun_ble_driver.h`](include/zhiyun_ble_driver.h), [`include/feiyu_serial_driver.h`](include/feiyu_serial_driver.h).

## 주의 사항

- 기본 AP 비밀번호(`astro1234`)는 공개 저장소에 있는 값 — `platformio.ini`의 `-DAP_PASS='"..."'` 로 변경하세요.
- HTTP 엔드포인트는 무인증 설계입니다(야외 단독망 전제). AP를 집 네트워크에 bridge하지 마세요.
- 장렌즈 무인 촬영 전 밸런스·웨지 잠금 확인. 노출 중 정지 시 셔터는 자동으로 닫힙니다.
- 짐벌은 웨지로 기울여 **pan축을 북극성에** 조준해야 합니다. FPV 모드에서 롤·피치가 프레임 기준 고정되므로 수평보정이 추적과 충돌하지 않습니다.

## 자세한 문서

프로토콜 근거·레포지토리 구조·영문 전체 가이드는 [README.md](README.md)를 참고하세요.

## 라이선스

Apache-2.0 — [LICENSE](LICENSE).
