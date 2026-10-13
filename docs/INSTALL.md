# Installation (Windows 10 / 11)

You need about 15 GB of free disk space and 1-2 hours the first time (OpenCV and Qt are compiled
from source by vcpkg). You only do this once.

## Step 1 - Visual Studio 2022
1. Download **Visual Studio 2022 Community** (free): https://visualstudio.microsoft.com/downloads/
2. In the installer tick **"Desktop development with C++"** and install.

## Step 2 - Git and CMake
* Git: https://git-scm.com/download/win (all defaults are fine)
* CMake comes with Visual Studio (Step 1). If you want it separately: https://cmake.org/download/

## Step 3 - vcpkg (the package manager that downloads Qt and OpenCV)
Open **"x64 Native Tools Command Prompt for VS 2022"** (Start menu). Do NOT use C:\Windows\System32.

```bat
mkdir C:\Dev
cd C:\Dev
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
bootstrap-vcpkg.bat
```

## Step 4 - install Qt and OpenCV (long step: go and have lunch)
```bat
cd C:\Dev\vcpkg
vcpkg install qtbase[core,gui,widgets]:x64-windows opencv4:x64-windows
```
> **You do NOT need `qtcharts`.** The graphs in this project are drawn by our own code.
> (If an old attempt failed on qtcharts, ignore it.)

If you already installed `qtbase` and `opencv4` earlier, skip this step.

## Step 5 - build the project
Unzip `MediaAnalysisEngine_Final.zip` into a simple folder, for example `C:\Dev\MediaAnalysisEngine`
(avoid folders with spaces or special characters). In the same Developer Command Prompt:

```bat
cd C:\Dev\MediaAnalysisEngine
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/Dev/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release
```
The word **Release** must be written in full.

## Step 6 - run
```bat
build\Release\MediaAnalysisEngine.exe
```
Click **Browse Video**, choose `build\Release\sample_videos\sample_events.mp4` (copied there
automatically), press **START ANALYSIS**. Results are written to `build\Release\output\`.

## Step 7 (optional) - run the automatic tests
```bat
cd build
ctest -C Release --output-on-failure
```

---

## Visual Studio Code instead of the command prompt
Install the **CMake Tools** extension, then in `.vscode\settings.json`:
```json
{
  "cmake.configureArgs": [
    "-DCMAKE_TOOLCHAIN_FILE=C:/Dev/vcpkg/scripts/buildsystems/vcpkg.cmake",
    "-DVCPKG_TARGET_TRIPLET=x64-windows"
  ]
}
```
Pick the kit "Visual Studio Community 2022 Release - amd64", then Configure, Build, Run.

## If something goes wrong
| Problem | Fix |
|---|---|
| `cmake` is not recognized | Open the **x64 Native Tools Command Prompt for VS 2022**, not normal PowerShell. |
| `could not find Qt6` / `OpenCV` | The toolchain path is wrong. Check `C:/Dev/vcpkg/scripts/buildsystems/vcpkg.cmake` exists. Use `/` slashes. |
| "no Qt platform plugin could be initialized" | Create a folder `platforms` next to `MediaAnalysisEngine.exe` and copy `C:\Dev\vcpkg\installed\x64-windows\Qt6\plugins\platforms\qwindows.dll` into it. (The build normally does this for you.) |
| "Could not open the video" | Install OpenCV with FFmpeg: `vcpkg install opencv4[ffmpeg]:x64-windows`, rebuild. Also keep the file path free of special characters. |
| Build error `--config` | Write `--config Release`, not `--config Rel`. |
| Window opens but speedup is ~1x | Normal on very small/low-core machines or very short videos; use a longer HD video. |
| Console-only fallback | `build\Release\spectra_cli.exe video.mp4 --threads 4` and `... --benchmark` need no GUI. |

## Linux (optional, for developers)
```bash
sudo apt install build-essential cmake qt6-base-dev libopencv-dev
cmake -S . -B build && cmake --build build -j
cd build && ctest --output-on-failure
```
