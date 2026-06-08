# SteamlessController DualShock 4 / DualSense / Switch Pro 지원 포크

이 브랜치는 Steam Controller를 가상 Xbox 360, DualShock 4, 실험적 DualSense, 또는 Switch Pro Controller로 노출하는 SteamlessController 포크입니다. 목표는 기존 Xbox 360 동작을 유지하면서, PlayStation/Nintendo 네이티브 입력 경로가 필요한 게임에서 자이로, 터치패드, 진동, 트랙패드 햅틱, DualShock 4 / DualSense / Switch Pro 지원을 사용할 수 있게 하는 것입니다.

## 릴리즈 설치 방법

1. [최신 릴리즈](https://github.com/david419kr/steamless-controller-XB-PS-NS/releases/latest)에서 `SteamlessController-Setup.exe`를 내려받아 설치합니다.
2. 설치가 끝나면 Windows를 한 번 재부팅합니다.
3. Steam Controller 2026을 유선 또는 무선 Puck으로 연결합니다.
4. 트레이 메뉴에서 `Output Mode -> DualShock 4`, `Output Mode -> DualSense`, 또는 `Output Mode -> Switch Pro Controller`를 선택하고 `Enable Steamless Mode`를 켭니다.
5. 필요하면 `Auto Enable Mode -> Auto Enable`을 켜서 Steam Controller가 감지될 때 Steamless Mode가 자동으로 켜지게 할 수 있습니다. `Auto Restart Steam`은 Steam이 이미 실행 중일 때만 함께 재시작합니다.
6. Steam에서 네이티브 PlayStation/Nintendo 컨트롤러 입력으로 쓰려면 Steamless Mode를 켠 뒤 트레이 메뉴의 `Restart Steam`을 실행하세요.
7. 게임별로 필요하면 Steam 라이브러리의 해당 게임 속성에서 Steam Input을 비활성화합니다.

## 지원 기능

- DualShock 4 모드는 버튼, 스틱, 트리거, 네이티브 자이로/가속도, 터치패드 출력, 진동, Steam Controller 트랙패드 햅틱, 트랙패드 D-pad, L4/L5/R4/R5 백버튼 매핑을 지원합니다.
- HidHide와 DualShock 4 모드를 함께 사용하여 Steam에서 **Pragmata**의 네이티브 자이로 조작이 동작하는 것을 확인했습니다.
- DualSense 모드를 사용하여 **Ratchet & Clank: Rift Apart**와 **Marvel's Spider-Man Remastered**에서 향상된 진동을 best-effort로 테스트했습니다.
- Switch Pro Controller 모드는 버튼, 스틱, ZL/ZR, 네이티브 자이로/가속도, Capture를 지원합니다. 또한 HD Rumble translation layer가 존재하여 실제 HD Rumble에 가까운 느낌을 emulation합니다.
- Auto Enable Mode는 Steam Controller가 감지되면 Steamless Mode를 자동으로 켤 수 있으며, Steam이 이미 실행 중인 경우에만 Steam 자동 재시작도 함께 적용할 수 있습니다.
- Steam 외 사용처로 **Cemu**에서도 네이티브 자이로 입력이 동작하는 것을 확인했습니다.

## DualSense 지원

DualSense 모드는 실험적 기능입니다. Steam Controller 2026을 가상 DualSense 컨트롤러처럼 보이게 만들고, DualSense 네이티브 지원 게임에서 자이로/가속도, 터치패드, 일반 진동, 향상된 진동, 어댑티브 트리거 신호를 최대한 활용하려는 best-effort 구현입니다.

DualSense의 향상된 진동은 실제 DualSense 전용 구동부와 컨트롤러 구조에 맞춰 설계되어 있으므로 Steam Controller에서 물리적으로 완벽히 재현할 수 없습니다. 이 포크는 해당 신호를 받아 Steam Controller의 트랙패드 pulse/click/tick 햅틱과 진동 모터를 조합해 비슷한 느낌을 내려고 시도합니다. 일반 진동과는 다른 색다른 느낌을 받을 수 있지만, 오리지널 PS5 DualSense와 같은 수준을 기대해서는 안 됩니다.

어댑티브 트리거도 Steam Controller에는 물리 저항 장치가 없기 때문에 원본처럼 방아쇠가 실제로 무거워지거나 걸리는 동작은 재현할 수 없습니다. 대신 게임이 보내는 어댑티브 트리거 신호를 읽어 진동 모터와 햅틱으로 어느 정도의 피드백을 주도록 합성합니다.

Steam Controller 2026은 유선 연결과 무선 Puck 연결 모두에서 사용할 수 있으며, 이 포크의 DualSense 모드도 해당 입력을 바탕으로 동작합니다. DualSense 모드의 향상된 진동이 너무 강하거나 약하게 느껴지면 트레이 메뉴의 `Output Mode -> DualSense Settings...`에서 `Rumble Threshold` 값을 조정할 수 있습니다. 잘 모르겠다면 기본값을 사용하는 것을 권장합니다.

## 포함 구성 요소와 기술 메모

- 설치형 릴리즈는 `SteamlessController.exe`, 패치된 `viiper.exe`, Microsoft Visual C++ Redistributable 2015-2022 x64, [usbip-win2](https://github.com/vadimgrn/usbip-win2), 선택 설치용 [HidHide](https://github.com/nefarius/HidHide/releases/latest)를 포함합니다.
- VC++ runtime과 usbip-win2는 없으면 installer가 자동으로 설치합니다. 사용자가 별도로 내려받아 설치할 필요는 없습니다.
- HidHide는 선택 항목이지만 기본 체크 상태입니다. Steam에서 실제 Steam Controller/Puck을 숨기고 가상 컨트롤러만 보이게 하려면 설치하는 것을 권장합니다.
- 가상 컨트롤러 생성은 내부적으로 패치된 [VIIPER](https://github.com/Alia5/VIIPER) sidecar를 사용합니다. 일반 사용자는 `viiper.exe`를 직접 실행할 필요가 없습니다.
- HidHide를 사용하는 경우 Steamless Mode를 먼저 켠 다음 Steam을 재시작하세요. Steam이 이미 켜져 있는 상태에서는 실제 컨트롤러와 가상 PlayStation 컨트롤러가 동시에 인식되어 중복 입력이 발생할 수 있습니다.
- `Auto Enable Mode -> Auto Restart Steam`은 Steam이 이미 실행 중일 때만 Steam을 재시작합니다. 트레이 메뉴의 수동 `Restart Steam`은 기존처럼 Steam이 꺼져 있어도 Steam을 실행합니다.

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

이 스크립트는 upstream VIIPER `v0.6.1`을 받아 `third_party\viiper-patches` 아래의 DS4, DualSense, Switch Pro patch/overlay를 적용하고, 서버 버전을 `v0.6.1-steamless8`로 표시한 `viiper.exe`를 빌드합니다. stock `v0.6.1`은 PlayStation/Switch 모드에서 의도적으로 거부합니다. 네이티브 자이로/터치패드/트리거/향상된 진동 또는 Nintendo 컨트롤러 클라이언트가 요구하는 feature report와 descriptor 보강이 없기 때문입니다.

## 단일 installer 패키징

앱, 패치된 `viiper.exe`, Microsoft Visual C++ Redistributable 2015-2022 x64, usbip-win2, 선택 설치용 HidHide를 하나의 offline installer로 묶을 수 있습니다.

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-installer.ps1
```

출력 파일은 `build\installer\SteamlessController-Setup.exe`입니다. 설치 시 VC++ runtime과 usbip-win2는 없으면 자동 설치하고, HidHide는 선택 항목으로 표시하되 기본 체크 상태입니다. HidHide는 Steam 중복 입력 방지에 권장됩니다.

이 스크립트는 prerequisite 설치 파일을 `build\prereqs`에 내려받고, URL과 SHA-256 해시를 `build\prereqs\prereqs.json`에 기록한 뒤 Inno Setup 6으로 `resources\InnoInstallerScript.iss`를 컴파일합니다. `ISCC.exe`가 `PATH`에 없으면 Inno Setup 6을 먼저 설치해야 합니다.

릴리즈 태그는 앱 빌드 표시 형식과 같은 `Build 260609` 형식을 사용합니다. 트레이 메뉴에는 `Build YYMMDD (Check Update)`가 표시되며, 클릭하면 GitHub 릴리즈에서 해당 태그보다 최신 릴리즈가 있는지 확인합니다. 현재 빌드 태그가 릴리즈에 없으면 최신 빌드로 간주합니다.

이미 CMake configure가 끝난 상태라면 다음 preset도 사용할 수 있습니다.

```powershell
cmake --preset release
cmake --build --preset installer
```

## 필요한 개발 도구

- Visual Studio 2022, Desktop development with C++ workload
- CMake 3.20 이상
- Windows SDK 10.0.22000 이상
- Git
- Go 1.26 이상
- 단일 installer 빌드 시 Inno Setup 6

## 라이선스 메모

패치된 `viiper.exe`를 배포 artifact에 포함하는 경우 VIIPER의 GPLv3 라이선스 고지와 대응 소스/패치 위치를 함께 제공해야 합니다. `tools\build-viiper.ps1`은 출력 폴더에 `VIIPER-LICENSE.txt`, `VIIPER-SOURCE.txt`, DS4/DualSense/Switch Pro patch 파일과 `nspro-overlay`를 같이 복사합니다.

원본 SteamlessController 설명과 상세 빌드 타깃은 [README.md](README.md)를 참고하세요.
