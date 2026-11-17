# labitop

터미널에서 PC 상태를 한 화면에 보여주는 모니터입니다. htop, nvtop, nvitop이 따로 보여주는 정보를 패널로 나눠 하나로 합쳤습니다.

```
 labitop  MYPC  up 4d 19:16:44                                                                       17:32:20  every 1s
╭─ CPU Intel Core Ultra 7 265F  20 threads ──────────────────────╮╭─ Processes 1-7 of 361, 7818 threads, by CPU% ──────╮
│Total ■■■···························   9.1%  4.80 GHz  ACPI 54°C││  #     PID  CPU%     MEM  THR  NAME                │
│                                                                ││  1   18560   5.9    131M   83  claude.exe          │
│                                                          ▁▁▁▁▁▁││  2       4   0.9   12.0K  446  System              │
│ 0 ■■■■■■·  84%  5 ·······   0% 10 ·······   0% 15 ·······   0% ││  3   11228   0.9    648M   35  claude.exe          │
│ 1 ■······  12%  6 ■······   5% 11 ·······   0% 16 ·······   0% ││  4   29208   0.6    237M   63  LittleLumiModel.exe │
│ 2 ·······   0%  7 ·······   0% 12 ·······   0% 17 ·······   0% ││  5    3124   0.3   4.52M   37  NVDisplay.Container.│
│ 3 ·······   0%  8 ·······   0% 13 ·······   0% 18 ■······  12% ││  6     288   0.0    100M    0  Secure System       │
│ 4 ·······   0%  9 ·······   0% 14 ·······   0% 19 ■■■····  42% ││  7     328   0.0   4.21M    4  Registry            │
╰────────────────────────────────────────────────────────────────╯╰────────────────────────────────────────────────────╯
╭─ GPU 1-6 of 31 processes, by GPU% ───────────────────────────────────────────────────────────────────────────────────╮
│RTX 5060 Ti                   30°C  13W/180W  fan 35%│ #     PID  TYPE  GPU%  ENGINE      VRAM  NAME                  │
│Util ···········································   0%│ 1    1640  G      0.0  -          2.05G  dwm.exe               │
│VRAM ■■■■■····························· 2.35G / 15.9G│ 2   22652  G      0.0  -           236M  chrome.exe            │
│                                                     │ 3     352  G      0.0  -           221M  ChatGPT.exe           │
│                                                     │ 4   35912  G      0.0  -           186M  Termius.exe           │
│                                                     │ 5    5316  G      0.0  -           153M  Slack.exe             │
│                                               ▁▁▁▁▁▁│ 6   25820  G      0.0  -           139M  DiscordCanary.exe     │
╰──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────╯
╭─ Memory DDR5 5600 MHz 2x16.0G ───────╮╭─ Storage read 0B/s  write 160K/s ────╮╭─ Network ────────────────────────────╮
│RAM ■■■■■■■■■■■········· 16.6G / 31.7G││C: NTFS  814G / 1.82T  free 1.02T  44%││Down 632B/s              scale 10.0K/s│
│Cmt ■■■■■■■■■■■■■■■····· 26.7G / 36.4G││■■■■■■■■■■■■■■■■■·····················││                                 ▂ ▅▄ │
│Avail 15.0G  Cached 13.8G  Used 53%   ││Disk0 WD Blue SN5000 2TB          34°C││                                ▅█▁██▁│
│                                      ││      read 0B/s   write 160K/s        ││Up   596B/s              scale 11.4K/s│
│                                ██████││Disk1 WD Blue SN5000 2TB          29°C││                                ▄▃▁█▄▁│
│DIMM 1   16.0G DDR5-5600  Samsung     ││      read 0B/s   write 0B/s          ││이더넷            rx 632B/s  tx 596B/s│
│DIMM 2   16.0G DDR5-5600  Samsung     ││                                ▂▁▅▁▁▁││Tailscale             rx 0B/s  tx 0B/s│
╰──────────────────────────────────────╯╰──────────────────────────────────────╯╰──────────────────────────────────────╯
 q quit | Tab switch table | up/down PgUp PgDn scroll | sort processes: c m p n | sort GPU: g v | +/- interval
```

## 패널

| 패널 | 내용 |
| --- | --- |
| CPU | 모델명, 스레드 수, 전체 사용률, 현재 클럭(GHz), 온도, 사용률 이력 그래프, 코어별 막대 |
| GPU | 모델명, 온도, 전력/한도, 팬 속도, 사용률 막대, VRAM 막대, 사용률 이력 그래프, GPU를 쓰는 프로세스 표 |
| Memory | 규격과 속도(예: DDR5 5600 MHz), RAM 사용량, 커밋(Windows) 또는 스왑(Linux), 캐시, 사용률 이력 그래프, 슬롯별 모듈(용량, 제조사, 부품 번호) |
| Storage | 볼륨별 사용량 막대와 여유 공간, 물리 디스크별 모델명·온도·읽기/쓰기 속도, 전체 입출력 이력 그래프 |
| Network | 전체 다운로드/업로드 속도와 이력 그래프, 인터페이스별 속도 |
| Processes | PID, CPU%, 메모리, 스레드 수, 이름 |

### GPU 프로세스 표

GPU 패널 안에 GPU를 쓰는 프로세스가 따로 나옵니다. `Tab`으로 GPU 패널을 선택하면 `↑` `↓`로 스크롤하고, 정렬 키로 순서를 바꾸고, `Enter`로 화면 전체에 펼쳐 볼 수 있습니다. 키는 아래 [키](#키) 절에 있습니다.

| 열 | 뜻 |
| --- | --- |
| `TYPE` | `C` = compute(CUDA, OpenCL 등), `G` = graphics(Direct3D, OpenGL, Vulkan), `C+G` = 둘 다 |
| `GPU%` | 그 프로세스가 가장 많이 쓰는 엔진의 사용률 |
| `ENGINE` | 지금 가장 바쁜 엔진. Windows 성능 카운터는 `3D`, `Compute`, `Copy`, `Decode`, `Encode` 등, NVIDIA 드라이버는 `SM`(셰이더·연산 코어), `Encode`, `Decode`. 둘 중 값이 큰 쪽이 나옵니다. |
| `VRAM` | 그 프로세스가 쓰는 전용 GPU 메모리 |

`TYPE` 판별 방법은 OS마다 다릅니다.

- **Windows:** 드라이버(WDDM)는 GPU를 쓰는 모든 프로세스를 compute와 graphics 양쪽에 올립니다(`nvidia-smi`가 전부 `C+G`로 보여주는 이유). 그래서 labitop은 프로세스가 불러온 라이브러리로 판단합니다. `nvcuda.dll`, `OpenCL.dll`, `amdhip64*.dll`, `ze_loader.dll`, `DirectML.dll`이 있으면 `C`, `d3d9/10/11/12.dll`, `opengl32.dll`, `vulkan-1.dll`이 있으면 `G`입니다. 권한 때문에 읽을 수 없는 프로세스(예: `dwm.exe`)는 3D 엔진을 쓰면 `G`로 표시합니다.
- **Linux:** NVIDIA 드라이버가 알려주는 compute / graphics 프로세스 목록을 그대로 씁니다.

GPU가 여러 개면 모든 GPU의 프로세스가 한 표에 섞여 나옵니다.

### 배치

중요도 순서는 CPU = GPU ≥ Memory > Storage > Network ≫ Processes입니다. 화면 자리는 이 순서로 나눕니다.

- 윗줄: CPU(왼쪽)와 Processes(오른쪽). Processes는 CPU 패널과 같은 높이만 씁니다.
- 둘째 줄: GPU가 한 줄을 다 씁니다. 왼쪽에 게이지와 그래프, 오른쪽에 GPU 프로세스 표가 나옵니다.
- 셋째 줄: Memory, Storage, Network가 3등분합니다. 그래프 높이는 위 두 줄의 절반입니다.

터미널 크기에 따라 다음과 같이 바뀝니다.

| 폭 | 배치 |
| --- | --- |
| 120칸 이상 | CPU · Processes / GPU / Memory · Storage · Network |
| 80~119칸 | CPU / GPU / Memory · Storage · Network / Processes |
| 80칸 미만 | CPU, GPU, Memory, Storage, Network, Processes 순으로 세로로 쌓기 |

GPU 패널은 안쪽 폭이 90칸 이상이면 프로세스 표를 게이지 오른쪽에, 그보다 좁으면 아래에 둡니다. 그래프는 화면 높이에 맞춰 최대 12줄까지 커지고, 그래도 남는 줄은 GPU 프로세스 표에 줍니다. 좁은 패널에서는 긴 문구 대신 짧은 형태(예: `read/write` 대신 `R/W`)를 씁니다.

C++17로 작성했고 외부 라이브러리를 쓰지 않습니다.

## 지원 상태

| OS | 상태 |
| --- | --- |
| Windows 10 / 11 (x64) | 빌드와 실행을 확인했습니다. |
| Linux | 코드는 작성했지만 아직 컴파일과 실행을 확인하지 못했습니다. |

## Requirements

### 실행

**Windows**

- Windows 10 이상. 추가로 설치할 런타임은 없습니다. 실행 파일은 Windows에 기본 포함된 DLL(`kernel32`, `ntdll`, `msvcrt`, `advapi32`, `iphlpapi`, `pdh`, `dxgi`)만 사용합니다.
- 관리자 권한은 필요 없습니다.
- 터미널: Windows Terminal을 권장합니다. 예전 콘솔 창(conhost)에서 테두리나 막대가 어긋나 보이면 `--ascii` 옵션을 쓰세요.
- NVIDIA GPU의 온도, 전력, 팬 속도를 보려면 NVIDIA 드라이버가 필요합니다. 드라이버에 포함된 `nvml.dll`을 실행 중에 불러옵니다. 드라이버가 없어도 프로그램은 실행됩니다.

**Linux**

- `/proc`과 `/sys` 파일 시스템이 있는 커널.
- UTF-8 터미널. 아니면 `--ascii` 옵션을 쓰세요.
- NVIDIA GPU 정보를 보려면 NVIDIA 드라이버(`libnvidia-ml.so.1`)가 필요합니다. 없으면 GPU 패널에 `no GPU data`가 나옵니다.
- 메모리 모듈 정보(규격, 속도, 제조사)는 `/sys/firmware/dmi/tables/DMI`에서 읽습니다. 이 파일은 보통 root만 읽을 수 있어서, 일반 사용자로 실행하면 이 정보만 빠집니다.

### 빌드

- C++17 컴파일러: GCC(Windows에서는 MinGW-w64). GCC 15.2로 확인했습니다. MSVC용 설정도 `CMakeLists.txt`에 있지만 확인하지 못했습니다.
- 선택 사항: CMake 3.15 이상과 Ninja.

## Installation

### Windows

1. MinGW-w64 GCC를 설치합니다. [Scoop](https://scoop.sh)을 쓴다면:

   ```powershell
   scoop install gcc
   ```

2. 저장소 폴더에서 빌드 스크립트를 실행합니다.

   ```powershell
   .\build.bat
   ```

   테스트까지 빌드하고 실행하려면 `.\build.bat test`를 씁니다.

3. `build\labitop.exe`가 생깁니다. 이 파일 하나만 다른 Windows PC로 복사해도 실행됩니다.

4. 어느 폴더에서나 `labitop` 명령으로 실행하려면 설치 스크립트를 실행합니다. 관리자 권한은 필요 없습니다.

   ```powershell
   .\install.bat
   ```

   스크립트가 하는 일:

   - `build.bat`으로 새로 빌드합니다. g++가 없으면 이미 있는 `build\labitop.exe`를 그대로 씁니다.
   - 실행 파일을 `%LOCALAPPDATA%\Programs\labitop\labitop.exe`로 복사합니다.
   - 그 폴더를 **사용자** PATH에 추가합니다. 이미 있으면 그대로 둡니다. 시스템 PATH와 다른 항목은 건드리지 않습니다.

   설치가 끝나면 **새 터미널**을 열고 `labitop`을 입력합니다. 이미 열려 있던 터미널은 예전 PATH를 그대로 갖고 있어서 명령을 찾지 못합니다.

   코드를 고친 뒤 다시 `.\install.bat`을 실행하면 새 버전으로 바뀝니다. 설치된 labitop이 실행 중이면 복사가 실패하니 먼저 종료하세요.

   제거:

   ```powershell
   .\install.bat uninstall
   ```

   PATH에서 그 폴더를 빼고 설치된 `labitop.exe`를 지웁니다. 저장소의 `build` 폴더는 그대로 둡니다.

   수동으로 하려면 `labitop.exe`를 원하는 폴더에 두고, 시작 메뉴에서 "계정의 환경 변수 편집"을 열어 사용자 변수 `Path`에 그 폴더를 추가하면 됩니다.

빌드가 `Permission denied`로 실패하면 실행 중인 labitop을 먼저 종료하세요. Windows는 실행 중인 파일을 덮어쓸 수 없습니다.

스크립트 없이 직접 빌드하려면:

```powershell
mkdir build
g++ -std=c++17 -O2 -static src/main.cpp src/term.cpp src/parse.cpp src/nvml.cpp src/sys_win.cpp src/sys_linux.cpp -o build/labitop.exe -lntdll -lpsapi -liphlpapi -lpdh -ldxgi
```

CMake를 쓰는 방법도 있습니다. 이 경우 Ninja가 필요합니다(`scoop install cmake ninja`).

```powershell
cmake -S . -B build -G Ninja
cmake --build build
```

### Linux

아래 절차는 아직 실제 Linux에서 확인하지 못했습니다.

1. 컴파일러를 설치합니다.

   ```bash
   # Debian / Ubuntu
   sudo apt install g++

   # Fedora
   sudo dnf install gcc-c++

   # Arch
   sudo pacman -S gcc
   ```

2. 빌드 스크립트를 실행합니다.

   ```bash
   sh build.sh
   ```

   테스트까지 빌드하고 실행하려면 `sh build.sh test`를 씁니다. 다른 컴파일러를 쓰려면 `CXX=clang++ sh build.sh`처럼 지정합니다.

   CMake를 쓰려면(`cmake` 패키지 필요):

   ```bash
   cmake -S . -B build
   cmake --build build
   ```

3. 어느 폴더에서나 `labitop` 명령으로 실행하려면 설치 스크립트를 실행합니다.

   ```bash
   sh install.sh
   ```

   빌드한 뒤 `~/.local/bin/labitop`으로 복사합니다. 대부분의 배포판은 `~/.local/bin`이 PATH에 들어 있습니다. 없으면 스크립트가 `~/.bashrc`(또는 `~/.zshrc`)에 넣을 줄을 알려 줍니다. 셸 설정 파일은 직접 고치지 않습니다.

   모든 사용자용으로 `/usr/local/bin`에 설치하려면:

   ```bash
   sh build.sh && sudo PREFIX=/usr/local sh install.sh
   ```

   제거는 설치할 때와 같은 `PREFIX`로 `sh install.sh uninstall`(또는 `sudo PREFIX=/usr/local sh install.sh uninstall`)입니다.

## 사용법

```
labitop [-i 초] [--once] [--ascii]
```

| 옵션 | 설명 |
| --- | --- |
| `-i`, `--interval <초>` | 갱신 주기. 기본값 1초, 범위 0.1~60초. |
| `--once` | 한 프레임을 표준 출력에 찍고 종료합니다. 프로세스 전체를 출력하므로 파일 저장이나 스크립트에 쓸 수 있습니다. 이력이 없으므로 그래프는 나오지 않습니다. |
| `--ascii` | 테두리, 막대, 그래프를 ASCII 문자(`+ - | #`)로 그립니다. |
| `-h`, `--help` | 도움말. |

예시:

설치 스크립트로 PATH에 등록했다면 어디서나 `labitop`으로, 아니면 저장소 폴더에서 `.\build\labitop.exe`(Linux는 `./build/labitop`)로 실행합니다.

```powershell
labitop                  # 실시간 화면
labitop -i 0.5           # 0.5초마다 갱신
labitop --ascii          # 글자가 어긋나는 터미널용
labitop --once > snapshot.txt
```

### 키

| 키 | 동작 |
| --- | --- |
모든 패널을 선택할 수 있습니다. 선택된 패널은 테두리가 파랗게, 제목이 반전되어 보이고, 맨 아래 줄에 그 패널에서 쓸 수 있는 키가 나옵니다. 시작할 때는 Processes가 선택되어 있습니다.

**어느 패널에서나**

| 키 | 동작 |
| --- | --- |
| `Tab` / `Shift+Tab`, `→` / `←` | 다음 / 이전 패널 선택 (화면에서 읽는 순서) |
| `Enter` 또는 `z` | 선택된 패널을 화면 전체로 확대. 한 번 더 누르면 원래 화면으로. 확대한 채로 `Tab`을 누르면 다음 패널이 확대됩니다. |
| `Esc` | 확대 중이면 원래 화면으로, 아니면 종료 |
| `q`, `Ctrl+C` | 종료 |
| `+` / `-` | 갱신 주기를 2배로 늘리기 / 절반으로 줄이기 (0.25~10초) |

**목록이 있는 패널** (Processes, GPU, Network)

| 키 | 동작 |
| --- | --- |
| `↑` `↓` (또는 `k` `j`) | 한 줄 스크롤 |
| `PgUp` `PgDn` | 한 페이지 스크롤 |
| `Home` `End` | 처음 / 끝 |

**정렬 키**는 패널마다 따로이고, 정렬 기준도 패널마다 따로 기억합니다. 지금 쓰는 기준은 맨 아래 줄에서 강조되어 보입니다. 같은 키를 한 번 더 누르거나 `r`을 누르면 순서가 뒤집힙니다.

| 패널 | 키 | 기본 |
| --- | --- | --- |
| Processes | `c` CPU% · `m` 메모리 · `t` 스레드 수 · `p` PID · `n` 이름 | CPU% |
| GPU | `g` GPU% · `v` VRAM · `t` TYPE(C 먼저) · `p` PID · `n` 이름 | GPU% |
| Storage | `n` 이름 · `u` 사용률 · `s` 크기 · `f` 여유 공간 (볼륨 목록) | 이름 |
| Network | `t` 합계 · `d` 다운로드 · `u` 업로드 · `n` 이름 (인터페이스 목록) | 합계 |

CPU와 Memory 패널은 정렬하거나 스크롤할 목록이 없습니다. 이 둘은 선택해서 확대하는 용도입니다. 확대하면 이력 그래프가 화면 높이만큼 커집니다.

Processes와 GPU 표의 `#` 열은 현재 정렬 순서에서의 순위입니다. 패널 제목에는 지금 보이는 범위가 나옵니다(예: `1-7 of 360`). 목록은 갱신할 때마다 다시 정렬되므로, 같은 순위에 다른 프로세스가 올 수 있습니다.

### 화면 읽는 법

- 막대와 사용률 그래프의 색은 값에 따라 바뀝니다: 50% 미만 초록, 80% 미만 노랑, 그 이상 빨강. 색을 못 쓰는 출력(파일 저장 등)에서는 빈 칸을 `·`로 표시합니다.
- 이력 그래프는 오른쪽 끝이 최신입니다. 갱신할 때마다 한 칸씩 왼쪽으로 밀립니다.
- Network와 Storage 그래프는 화면에 보이는 구간의 최댓값에 맞춰 높이를 조정합니다. Network 패널의 `scale`이 그 최댓값입니다.
- 프로세스의 `CPU%`는 전체 코어 기준 0~100%입니다(Windows 작업 관리자와 같은 방식). htop처럼 코어 하나를 100%로 세지 않습니다.
- `Cmt`(Windows)는 커밋된 메모리와 커밋 한도입니다. Linux에서는 이 자리에 스왑(`Swp`)이 나옵니다.
- 프로세스의 `MEM`은 Windows에서는 전용 작업 집합, Linux에서는 RSS입니다.

## 값의 출처와 한계

| 항목 | Windows | Linux |
| --- | --- | --- |
| CPU 클럭 | 성능 카운터 `% Processor Performance` × 기준 클럭 (작업 관리자의 "속도"와 같은 방식) | `/proc/cpuinfo`의 `cpu MHz` 평균 |
| CPU 온도 | ACPI 열 영역(`Thermal Zone Information`). 메인보드가 알려주는 값이라 `ACPI`로 표시합니다. 코어 센서 값과 다를 수 있고, 메인보드에 따라 아예 없으면 표시되지 않습니다. | hwmon의 `coretemp`, `k10temp`, `zenpower`, `cpu_thermal` |
| 메모리 규격/속도 | 펌웨어(SMBIOS) 표 | 같은 표. 보통 root 권한 필요 |
| GPU | NVIDIA는 NVML. 그 외 GPU는 이름, 사용률, VRAM 사용량만 (온도, 전력, 팬 없음) | NVIDIA만 지원 |
| 프로세스별 GPU%·엔진 | WDDM 성능 카운터 (모든 GPU 제조사) + NVIDIA는 NVML 프로세스 사용률 | NVML 프로세스 사용률 (NVIDIA만, 드라이버가 지원할 때) |
| 프로세스 `TYPE` | 불러온 라이브러리로 추정 | NVML compute / graphics 목록 |
| 디스크 온도 | 저장 장치 온도 조회(주로 NVMe). 드라이버가 지원하지 않으면 표시되지 않습니다. | NVMe는 기본 지원, SATA는 `drivetemp` 커널 모듈 필요 |
| 디스크 읽기/쓰기 속도 | 물리 디스크 성능 카운터 | `/sys/block/*/stat` |

그 밖의 한계:

- Windows에서 실제 CPU 코어 온도는 커널 드라이버 없이는 읽을 수 없어서 지원하지 않습니다.
- 네트워크 드라이브(Windows)와 `/dev/`로 시작하지 않는 파일 시스템(Linux, 예: NFS, ZFS)은 볼륨 목록에 나오지 않습니다.
- 프로세스를 종료하는 기능은 없습니다. 보기 전용입니다.

## 테스트

화면 그리기(글자 폭 계산, 자르기, 막대, 그래프)와 파서(`/proc` stat, 마운트 경로, SMBIOS 메모리 표)를 검사합니다. 파서는 OS 호출이 없는 코드라서 Windows에서도 실행됩니다.

```powershell
.\build.bat test
```

Linux에서는 `sh build.sh test`, CMake로 빌드했다면 `ctest --test-dir build`로 실행합니다.

## 소스 구조

| 파일 | 역할 |
| --- | --- |
| `src/main.cpp` | 패널 배치, 화면 그리기, 키 처리, 사용률·속도 계산 |
| `src/sys.h` | 공통 데이터 구조와 플랫폼 인터페이스 |
| `src/sys_win.cpp` | Windows 수집 |
| `src/sys_linux.cpp` | Linux 수집 (`/proc`, `/sys`) |
| `src/parse.cpp` | OS와 무관한 파서 (`/proc` stat, 마운트 경로, SMBIOS) |
| `src/nvml.cpp` | NVIDIA GPU 수집 (양쪽 OS 공통) |
| `src/term.cpp` | 터미널 제어와 키 입력 |
| `tests/test.cpp` | 테스트 |
| `build.bat` | Windows 빌드 스크립트 |
| `build.sh` | Linux 빌드 스크립트 |
| `install.bat`, `install.ps1` | Windows 설치·제거 스크립트 (사용자 PATH 등록) |
| `install.sh` | Linux 설치·제거 스크립트 |
| `CMakeLists.txt` | CMake 빌드 설정 |
