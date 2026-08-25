//
// zhiyun_ble_driver.cpp — 드라이버 컴파일 게이트
//
// main.cpp는 기본 경로(DJI CAN)만 사용하므로 이 TU가 없으면 zhiyunble env에서도
// 드라이버 실구현이 컴파일되지 않는다. 이 파일이 헤더를 강제 include하여
// NimBLE 경로의 타입 검증을 빌드 게이트에 포함시킨다.
//

#include "zhiyun_ble_driver.h"

#ifdef ASTRO_ZHIYUN_BLE
namespace {
// 인스턴스 생성으로 vtable·인라인 메서드 링크까지 강제 검증.
// 채용 여부는 하드웨어 보딩 검증(HARDWARE-PENDING) 후 main.cpp에서 결정.
ZhiyunBleDriver g_zyCompileGate;
}  // namespace
#endif
