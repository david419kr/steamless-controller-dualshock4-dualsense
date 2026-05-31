# SteamlessController DualShock 4 포크

이 포크는 Steam Controller를 Steam 없이 사용하되, 기존 Xbox 360 가상 컨트롤러뿐 아니라 DualShock 4 가상 컨트롤러로도 출력할 수 있도록 확장한 버전입니다. 목적은 Steam 게임에서 Steam Controller를 네이티브 DualShock 4 입력 장치처럼 사용하는 것입니다.

## 포크 변경점

- 가상 컨트롤러 백엔드는 [VIIPER](https://github.com/Alia5/VIIPER)를 사용합니다. `viiper.exe` v0.6.1 이상을 `SteamlessController.exe`와 같은 폴더에 두세요.
- VIIPER는 ViGEmBus를 쓰지 않지만, Windows에서 virtual USB 장치를 OS에 붙이려면 범용 [usbip-win2](https://github.com/vadimgrn/usbip-win2) 드라이버가 필요합니다.
- Steam에서 사용하려면 [HidHide](https://github.com/nefarius/HidHide/releases/latest)를 설치하세요. Steamless Mode가 켜져 있을 때 실제 Steam Controller/Puck을 숨기고, Steam에는 가상 DualShock 4만 보이도록 하기 위한 용도입니다.
- HidHide로 Steam에서 사용하려면 Steamless Mode를 먼저 켠 다음 Steam을 재시작하세요. Steam이 이미 켜져 있는 상태에서는 Steam Controller와 가상 DualShock 4가 동시에 인식되어 중복 입력이 발생할 수 있습니다.
- 앱이 실행되지 않으면 [Microsoft Visual C++ Redistributable 2015-2022 x64](https://aka.ms/vc14/vc_redist.x64.exe)가 필요할 수 있습니다.
- [최신 릴리즈](https://github.com/david419kr/steamless-controller-dualshock4/releases/latest)에서 `SteamlessController.exe`를 내려받고, `viiper.exe`를 같은 폴더에 둔 뒤 실행하면 됩니다.
- DualShock 4를 지원하는 Steam 게임에서 네이티브 자이로를 사용하려면, Steam 라이브러리에서 해당 게임을 우클릭한 뒤 속성에서 Steam Input을 비활성화해야 할 수 있습니다.
- DualShock 4 모드는 버튼, 스틱, 트리거, 네이티브 자이로/가속도, 터치패드 출력, 진동, 일부 향상된 Steam Controller 빌트인 트랙패드 햅틱을 지원합니다.
- HidHide와 DualShock 4 모드를 함께 사용하여 Steam에서 **Pragmata**의 네이티브 자이로 조작이 동작하는 것을 확인했습니다.
- Steam 외 사용처로 **Cemu**에서도 네이티브 자이로 입력이 동작하는 것을 확인했습니다.
- L4/L5/R4/R5 백버튼 단일 버튼 할당을 지원합니다.

## 간단 사용 순서

1. `viiper.exe` v0.6.1 이상을 `SteamlessController.exe`와 같은 폴더에 둡니다.
2. [usbip-win2](https://github.com/vadimgrn/usbip-win2)를 설치합니다.
3. Steam에서 네이티브 DualShock 4 입력으로 사용하려면 HidHide를 설치합니다.
4. 릴리즈 페이지에서 `SteamlessController.exe`를 내려받아 실행합니다.
5. 트레이 메뉴에서 `Output Mode -> DualShock 4`를 선택합니다.
6. Steamless Mode를 켠 다음 Steam을 재시작합니다.
7. 게임별로 필요하면 Steam 라이브러리의 해당 게임 속성에서 Steam Input을 비활성화합니다.

원본 SteamlessController 설명은 [README.md](README.md)의 하단 원본 섹션을 참고하세요.
