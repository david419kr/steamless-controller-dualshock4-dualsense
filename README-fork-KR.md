# SteamlessController VIIPER Backend 포크

이 브랜치는 Steam Controller를 VIIPER 기반 가상 Xbox 360, DualShock 4, 또는 실험적 DualSense 컨트롤러로 노출하는 SteamlessController 포크입니다. 목표는 기존 Xbox 360 동작을 유지하면서, PlayStation 네이티브 입력 경로가 필요한 게임에서 자이로, 터치패드, 진동, 트랙패드 햅틱, DualSense 트리거 햅틱 근사를 사용할 수 있게 하는 것입니다.

## 포크 변경점

- 가상 컨트롤러 백엔드는 패치된 [VIIPER](https://github.com/Alia5/VIIPER) sidecar를 사용합니다.
- 릴리즈 빌드는 `SteamlessController.exe`와 패치된 `viiper.exe`를 같은 폴더에 함께 배치해야 합니다.
- Windows에서 VIIPER virtual USB 장치를 OS에 붙이려면 범용 [usbip-win2](https://github.com/vadimgrn/usbip-win2) 드라이버가 필요합니다.
- Steam에서 사용하려면 [HidHide](https://github.com/nefarius/HidHide/releases/latest)를 설치하세요. Steamless Mode가 켜져 있을 때 실제 Steam Controller/Puck을 숨기고, Steam에는 가상 컨트롤러만 보이도록 하기 위한 용도입니다.
- HidHide로 Steam에서 사용하려면 Steamless Mode를 먼저 켠 다음 Steam을 재시작하세요. Steam이 이미 켜져 있는 상태에서는 실제 컨트롤러와 가상 PlayStation 컨트롤러가 동시에 인식되어 중복 입력이 발생할 수 있습니다.
- 앱이 실행되지 않으면 [Microsoft Visual C++ Redistributable 2015-2022 x64](https://aka.ms/vc14/vc_redist.x64.exe)가 필요할 수 있습니다.
- [최신 릴리즈](https://github.com/david419kr/steamless-controller-dualshock4/releases/latest)에서 앱을 내려받고, `viiper.exe`가 같은 폴더에 있는 상태로 실행하면 됩니다.
- DualShock 4 또는 DualSense를 지원하는 Steam 게임에서 네이티브 자이로를 사용하려면, Steam 라이브러리에서 해당 게임을 우클릭한 뒤 속성에서 Steam Input을 비활성화해야 할 수 있습니다.
- DualShock 4 모드는 버튼, 스틱, 트리거, 네이티브 자이로/가속도, 터치패드 출력, 진동, Steam Controller 트랙패드 햅틱, 트랙패드 D-pad, L4/L5/R4/R5 백버튼 매핑을 지원합니다.
- DualSense 모드는 실험적 기능입니다. DualSense USB composite HID + Audio identity, 네이티브 자이로/가속도, 터치패드 출력, compatible rumble, adaptive-trigger output 파싱, USB audio haptics 추출, Steam Controller 햅틱 기반 트리거/오디오 효과 합성을 지원합니다.
- HidHide와 DualShock 4 모드를 함께 사용하여 Steam에서 **Pragmata**의 네이티브 자이로 조작이 동작하는 것을 확인했습니다.
- Steam 외 사용처로 **Cemu**에서도 네이티브 자이로 입력이 동작하는 것을 확인했습니다.

## 간단 사용 순서

1. `SteamlessController.exe`와 패치된 `viiper.exe`를 같은 폴더에 둡니다.
2. [usbip-win2](https://github.com/vadimgrn/usbip-win2)를 설치합니다.
3. Steam에서 네이티브 PlayStation 컨트롤러 입력으로 사용하려면 HidHide를 설치합니다.
4. 앱을 실행하고 트레이 메뉴에서 `Output Mode -> DualShock 4` 또는 `Output Mode -> DualSense`를 선택합니다.
5. Steamless Mode를 켠 다음 Steam을 재시작합니다.
6. 게임별로 필요하면 Steam 라이브러리의 해당 게임 속성에서 Steam Input을 비활성화합니다.

## 빌드 방법

앱과 패치된 VIIPER sidecar를 함께 빌드하려면 다음 명령을 사용합니다.

```powershell
cmake --preset release
cmake --build --preset release
```

패치된 `viiper.exe`만 직접 빌드할 수도 있습니다.

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-viiper.ps1 -OutputDir build\release\Release
```

이 스크립트는 upstream VIIPER `v0.6.1`을 받아 `third_party\viiper-patches\viiper-v0.6.1-ds4-compat.patch`와 `third_party\viiper-patches\viiper-v0.6.1-dualsense.patch`를 적용하고, 서버 버전을 `v0.6.1-steamless5`로 표시한 `viiper.exe`를 빌드합니다. stock `v0.6.1`은 PlayStation 모드에서 의도적으로 거부합니다. 네이티브 자이로/터치패드/트리거/오디오 햅틱 클라이언트가 요구하는 feature report와 descriptor 보강이 없기 때문입니다.

## 단일 installer 패키징

앱, 패치된 `viiper.exe`, Microsoft Visual C++ Redistributable 2015-2022 x64, usbip-win2, 선택 설치용 HidHide를 하나의 offline installer로 묶을 수 있습니다.

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-installer.ps1
```

출력 파일은 `build\installer\SteamlessController-Setup.exe`입니다. 설치 시 VC++ runtime과 usbip-win2는 없으면 자동 설치하고, HidHide는 선택 항목으로 표시하되 기본 체크 상태입니다. HidHide는 Steam 중복 입력 방지에 권장됩니다.

이 스크립트는 prerequisite 설치 파일을 `build\prereqs`에 내려받고, URL과 SHA-256 해시를 `build\prereqs\prereqs.json`에 기록한 뒤 Inno Setup 6으로 `resources\InnoInstallerScript.iss`를 컴파일합니다. `ISCC.exe`가 `PATH`에 없으면 Inno Setup 6을 먼저 설치해야 합니다.

이미 CMake configure가 끝난 상태라면 다음 preset도 사용할 수 있습니다.

```powershell
cmake --preset release
cmake --build --preset installer
```

이 테스트 브랜치에서는 SteamlessController가 bundled VIIPER sidecar를 자동 실행할 때 `SteamlessController-viiper.log`와 `SteamlessController-viiper-raw.log`를 가능하면 바탕화면에 기록하고, 실패하면 `%LOCALAPPDATA%\SteamlessController`로 fallback합니다. raw log는 의도적으로 매우 커질 수 있으므로 일반 릴리즈 전에는 기본 비활성화해야 합니다.

## 필요한 개발 도구

- Visual Studio 2022, Desktop development with C++ workload
- CMake 3.20 이상
- Windows SDK 10.0.22000 이상
- Git
- Go 1.26 이상
- 단일 installer 빌드 시 Inno Setup 6

## 라이선스 메모

패치된 `viiper.exe`를 배포 artifact에 포함하는 경우 VIIPER의 GPLv3 라이선스 고지와 대응 소스/패치 위치를 함께 제공해야 합니다. `tools\build-viiper.ps1`은 출력 폴더에 `VIIPER-LICENSE.txt`, `VIIPER-SOURCE.txt`, `viiper-v0.6.1-ds4-compat.patch`, `viiper-v0.6.1-dualsense.patch`를 같이 복사합니다.

원본 SteamlessController 설명과 상세 빌드 타깃은 [README.md](README.md)를 참고하세요.
