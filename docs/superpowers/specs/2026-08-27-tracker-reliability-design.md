# Tracker Reliability Hardening Design

**Date:** 2026-08-27

## Goal

DJI RS 기반 천체 촬영에서 셔터 충돌과 통신 실패를 안전하게 처리하고, 추적 상태·DJI 자세 응답·설정 입력을 실제 운용 가능한 수준으로 강화한다.

## Scope

### Included

- `Tracker`에 `Fault` 상태와 고정된 오류 코드를 추가한다.
- 짐벌 명령의 `bool` 실패를 상태머신이 처리한다.
- 활성 촬영 중 중복 시작·테스트컷·설정 변경을 거부한다.
- 테스트컷과 장노출 셔터의 수명주기를 서로 격리한다.
- 설정값을 공통 검증하고 펌웨어·시뮬레이터에서 같은 HTTP 결과를 반환한다.
- 추적 타이머의 지연 catch-up을 한 번의 명령으로 제한하고 현재 시각 기준으로 재동기화한다.
- DJI 디더링을 증분 명령으로도 실제 `+진폭`과 `-진폭` 사이에서 왕복하도록 고친다.
- DJI 자세 응답의 CmdSet/CmdID·payload 위치를 검증하고 yaw 유효성을 실제 응답에만 연결한다.
- DJI `/probe`가 자세 응답 수신 여부를 기준으로 판단하도록 한다.
- 위 동작을 native Tracker·protocol 테스트와 시뮬레이터 시나리오로 검증한다.

### Excluded

- 실기 검증 전 ZHIYUN 런타임 활성화
- Feiyu, GPS, plate solving
- 새 외부 의존성
- 대규모 웹 UI 개편
- 비차단 probe 리팩터링(이번 변경에서는 응답 상관검증만 수행)

## Architecture

`Tracker`는 촬영·추적·셔터의 단일 소유자로 남는다. HTTP 계층은 현재 위상으로 허용 여부를 판단하고, Tracker는 명령 반환값을 검사해 실패를 `Fault`로 승격한다. 테스트컷은 HTTP 계층의 별도 셔터 동작이므로 Tracker가 활성 상태인 동안 거부하며, stop 시에만 별도 플래그를 정리한다.

설정 검증은 `tracker.h`의 공통 순수 함수로 두어 Arduino 펌웨어와 호스트 시뮬레이터가 같은 범위를 사용한다. DJI 응답 해석은 `djiprotocol`의 순수 함수로 분리해 하드웨어 없이 golden response를 검증한다.

## Behavior Contract

### Tracker

- `start`는 Idle 또는 Done에서만 새 시퀀스를 시작한다.
- `Fault`는 새 시퀀스를 시작하지 않는다. stop으로 셔터 닫기를 시도하고 Idle로 돌아간다.
- `shutterOpen` 실패, `shutterClose` 실패, 추적 이동 실패, 디더링 실패는 오류 코드와 함께 Fault로 전환한다.
- Fault 전환 시 Exposing/Opening 여부와 무관하게 `shutterClose`를 한 번 시도한다.
- 한 `tick`에서 추적 명령은 최대 한 번만 전송한다. 지연이 감지되면 다음 추적 시각을 `now + kTrackStepMs`로 재설정한다.
- 디더링 목표 오프셋은 `+amp`, `-amp`를 번갈아 사용하며, 드라이버에는 목표 오프셋과 현재 오프셋의 차이를 증분으로 전달한다.

### HTTP

- 범위 밖 또는 유한하지 않은 설정은 HTTP 400과 JSON 오류를 반환한다.
- 활성 시퀀스(`Delay`부터 `Dithering`/`Settling`/`Gap`까지) 중 설정 변경과 시작은 HTTP 409다.
- 테스트컷은 활성 시퀀스 중 HTTP 409다.
- 테스트컷이 열린 상태에서 start는 허용하지 않는다.
- stop은 테스트컷과 Tracker 시퀀스 모두를 닫고 정리한다.
- 펌웨어와 시뮬레이터의 오류 JSON 필드와 HTTP 상태 코드는 동일하다.

### DJI response

- 완성 프레임의 command set/id는 `frame[12]`/`frame[13]`에서 확인한다.
- 자세 payload는 `rc`, `data_type`, yaw/roll/pitch 순서로 확인하고 yaw는 payload의 첫 `int16` 자세값으로 읽는다.
- yaw telemetry validity는 유효한 자세 응답을 수신했을 때만 true다.
- probe 성공은 요청 이후 유효한 자세 응답을 수신했을 때만 true다.

## Tests

- 기존 Tracker 회귀 테스트를 유지한다.
- 명령 실패 시 Fault 전환과 셔터 닫기 시도를 검증한다.
- 지연된 tick에서 추적 명령이 한 번만 발생하는지 검증한다.
- 디더링 위치가 `0 → +amp → -amp → +amp`가 되는지 검증한다.
- 잘못된 설정값과 HTTP 충돌 응답을 시뮬레이터로 검증한다.
- DJI 자세 응답 golden frame에서 CmdSet/CmdID와 yaw를 바이트 단위로 검증한다.
- `test_protocol_native`, `test_tracker_native`, `test_zhiyun_native`, `esp32dev` 빌드를 실행한다.
