# labitop

터미널에서 PC 상태를 한 화면에 보여주는 모니터입니다. htop, nvtop, nvitop이 따로 보여주는 정보를 패널로 나눠 하나로 합쳤습니다.

```
 labitop  MYPC  up 4d 18:07:24                                                                       16:23:01  every 1s
╭─ CPU Intel Core Ultra 7 265F  20 threads ────────────────╮╭─ GPU ────────────────────────────────────────────────────╮
│Total ■■······················  10.4%  4.70 GHz  ACPI 62°C││RTX 5060 Ti                        30°C  14W/180W  fan 35%│
│                                      ▁          ▃        ││Util ················································   0%│
│                                  ▆▅▄▆█▄▅▅▇▇▆▅▄▆▅█▄▆▅▄▅▆▆█││VRAM ■■■■■■································· 2.39G / 15.9G│
│ 0 ■■···  32%  5 ■····  17% 10 ■····   5% 15 ■····   5%   ││                                                          │
│ 1 ■····  11%  6 ·····   0% 11 ■····   9% 16 ·····   0%   ││                                  ▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁│
╰──────────────────────────────────────────────────────────╯╰──────────────────────────────────────────────────────────╯
╭─ Memory DDR5 5600 MHz 2x16.0G ─────────────────╮╭─ Storage read 0B/s  write 53.6K/s ───╮╭─ Network ──────────────────╮
│RAM ■■■■■■■■■■■■■■■■·············· 17.1G / 31.7G││C: NTFS  814G / 1.82T  free 1.02T  44%││Down 398B/s    scale 10.0K/s│
│Cmt ■■■■■■■■■■■■■■■■■■■■■■········ 26.6G / 36.4G││■■■■■■■■■■■■■■■■■·····················││    ▂▄█▃▂▄█▂█▄▆▄▆▇▁▆█▃██▂▄▂▂│
│Available 14.6G   Cached 13.5G   Used 54%       ││Disk0 WD Blue SN5000 2TB          34°C││Up   180B/s     scale 110K/s│
│DIMM 1   16.0G DDR5-5600  Samsung M323R2GA3EB0-C││      read 0B/s   write 53.6K/s       ││    ▁▁▄▁▁▂█▁▁▁▁▁▂▁▁▁▁▁▁▁▁▁▁▁│
╰────────────────────────────────────────────────╯╰──────────────────────────────────────╯╰────────────────────────────╯
╭─ Processes 359 tasks, 7729 threads, sorted by CPU% ──────────────────────────────────────────────────────────────────╮
│    PID  CPU%     MEM  THR  GPU%    VRAM  NAME                                                                        │
```

## 패널

| 패널 | 내용 |
| --- | --- |
| CPU | 모델명, 스레드 수, 전체 사용률, 현재 클럭(GHz), 온도, 사용률 이력 그래프, 코어별 막대 |
| GPU | 모델명, 온도, 전력/한도, 팬 속도, 사용률 막대, VRAM 막대, 사용률 이력 그래프 |
| Memory | 규격과 속도(예: DDR5 5600 MHz), RAM 사용량, 커밋(Windows) 또는 스왑(Linux), 캐시, 사용률 이력 그래프, 슬롯별 모듈(용량, 제조사, 부품 번호) |
| Storage | 볼륨별 사용량 막대와 여유 공간, 물리 디스크별 모델명·온도·읽기/쓰기 속도, 전체 입출력 이력 그래프 |
| Network | 전체 다운로드/업로드 속도와 이력 그래프, 인터페이스별 속도 |
| Processes | PID, CPU%, 메모리, 스레드 수, GPU%, VRAM, 이름 |

### 배치

중요도 순서는 CPU = GPU ≥ Memory > Storage > Network ≫ Processes입니다. 화면 자리는 이 순서로 나눕니다.

- 윗줄: CPU와 GPU가 화면을 반씩 나눠 씁니다. 이력 그래프도 이 줄이 가장 높습니다.
- 둘째 줄: Memory, Storage, Network가 5 : 4 : 3 폭으로 나눠 씁니다. 그래프 높이는 윗줄의 절반입니다.
- 맨 아래: Processes. 남는 자리만 씁니다.

터미널 크기에 따라 다음과 같이 바뀝니다.

| 폭 | 배치 |
| --- | --- |
| 120칸 이상 | CPU · GPU / Memory · Storage · Network |
| 80~119칸 | CPU · GPU / Memory · Storage / Network |
| 80칸 미만 | CPU, GPU, Memory, Storage, Network 순으로 세로로 쌓기 |

높이가 넉넉하면 그래프를 최대 12줄까지 키우고 프로세스 목록에 8줄 이상을 줍니다. 높이가 부족하면 프로세스 목록부터 줄이거나 숨기고, 그래도 모자라면 그래프를 줄입니다.

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

3. `build\labitop.exe`가 생깁니다. 이 파일 하나만 다른 Windows PC로 복사해도 실행됩니다. 어디서나 실행하려면 `PATH`에 있는 폴더로 옮기세요.

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

3. 원하면 시스템 경로에 설치합니다.

   ```bash
   sudo install -m 755 build/labitop /usr/local/bin/labitop
   ```

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

```powershell
.\build\labitop.exe              # 실시간 화면
.\build\labitop.exe -i 0.5       # 0.5초마다 갱신
.\build\labitop.exe --ascii      # 글자가 어긋나는 터미널용
.\build\labitop.exe --once > snapshot.txt
```

### 키

| 키 | 동작 |
| --- | --- |
| `q`, `Esc`, `Ctrl+C` | 종료 |
| `c` / `m` / `g` / `v` / `p` / `n` | CPU% / 메모리 / GPU% / VRAM / PID / 이름 순으로 정렬 |
| `↑` `↓` (또는 `k` `j`) | 프로세스 목록 한 줄 스크롤 |
| `PgUp` `PgDn` | 한 페이지 스크롤 |
| `Home` `End` | 목록 처음 / 끝 |
| `+` / `-` | 갱신 주기를 2배로 늘리기 / 절반으로 줄이기 (0.25~10초) |

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
| 프로세스별 GPU% | 지원 | 미지원 (프로세스별 VRAM만) |
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
| `CMakeLists.txt` | CMake 빌드 설정 |
