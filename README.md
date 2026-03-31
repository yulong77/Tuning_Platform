# AccelTool

C++ tool for working with MicroStrain accelerometers using the MSCL library. Built with C++, CMake, MSVC, and Ninja. MSCL is included as a local SDK under `third_party/mscl`.

## Project Structure
```bash
AccelTool
├─ CMakeLists.txt
├─ README.md
├─ src/ # application source code
│ └─ main.cpp
├─ include/ # project headers
├─ config/ # configuration files
├─ third_party/ # third-party libraries
│ └─ mscl/
│ ├─ include/
│ │ ├─ mscl/
│ │ ├─ boost/
│ │ └─ openssl/
│ ├─ lib/
│ │ └─ MSCL.lib
│ └─ bin/
│ └─ MSCL.dll
└─ build/ # build output (generated)
```

## Requirements
Visual Studio Build Tools (MSVC)  
CMake ≥ 3.20  
Ninja

## Build
Open **x64 Native Tools Command Prompt for VS**
```bash
cd AccelTool
```

### Debug

```bash
cmake --preset debug
cmake --build --preset build-debug
```

### Release

```bash
cmake --preset release
cmake --build --preset build-release
```

## Run
```bash
build\debug\AccelTool.exe
build\release\AccelTool.exe
```

## Clean Build
If build configuration changes:
```bash
cd AccelTool
rmdir /s /q build
```

## Usage
```bash
AccelTool interactive mode
  I = Set to Idle + initialize device
  S = Start receiving data
  T = Stop receiving data
  Q = Quit
```