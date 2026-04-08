# Accel_Tool

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

## Structure
```bash
MSCL Acquisition Thread
        |
        v
   Raw Sample Buffer
      |         |
      v         v
   Writer    Processing
                 |
                 v
         Processed Sample Buffer
             |             |
             v             v
            UI         Alarm/Monitor
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
build_debug\AccelTool.exe
build_release\AccelTool.exe
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


## CSV Output Fields

`accel_data.csv` contains one processed sample per row.

### `sample_index`
Monotonic sample number assigned by this application after a sweep is accepted as a valid sample.

### `node_address`
Wireless node address reported by the device.

### `host_timestamp_sec`
Local monotonic timestamp, in seconds, recorded by this application when the sample was accepted.  
This is a host-side timing reference, not the device's own timestamp.

### `device_tick`
Sweep tick reported by MSCL from the wireless sync sampling packet.  
This is the primary sequence indicator used for data loss detection.

### `device_timestamp_sec`
Whole seconds part of the device timestamp, converted to Unix time.

### `device_timestamp_nanosec`
Sub-second nanoseconds part of the device timestamp.  
This is the remainder after splitting `device_timestamp_unix_ns` into seconds and nanoseconds.

### `device_timestamp_unix_ns`
Full device timestamp in Unix epoch nanoseconds.  
This is the main absolute device-side time value.

### `expected_timestamp_step_ns`
Expected interval between adjacent samples, in nanoseconds, derived from the configured sample rate.  
For example, at `2048 Hz` this is approximately `488281 ns`.

### `timestamp_gap_ns`
Observed difference, in nanoseconds, between this sample's device timestamp and the previous accepted sample's device timestamp.

### `timestamp_gap_detected`
Timestamp anomaly flag.  
`1` means the observed timestamp step differs significantly from the expected sampling period.  
This is a secondary diagnostic signal, not the primary loss indicator.

### `tick_gap_detected`
Primary data loss flag.  
`1` means the current `device_tick` is not the expected next tick after the previous accepted sample.

### `tick_gap_count`
Estimated number of missing ticks between the previous accepted sample and the current sample.  
`0` means no tick gap was detected.

### `x`
Acceleration value for the X axis.

### `y`
Acceleration value for the Y axis.

### `z`
Acceleration value for the Z axis.

### `magnitude_xy`
Vector magnitude computed from X and Y:

`sqrt(x*x + y*y)`

### `magnitude_xyz`
Vector magnitude computed from X, Y, and Z:

`sqrt(x*x + y*y + z*z)`

### `applied_spec`
Configured spec threshold used for exceedance checking.

### `exceeds_spec`
Spec exceedance flag.  
`1` means the selected magnitude exceeded `applied_spec`, otherwise `0`.

## Notes On Timing And Loss Detection

### Host timestamp vs device timestamp
`host_timestamp_sec` is measured locally by the PC application.  
`device_timestamp_sec`, `device_timestamp_nanosec`, and `device_timestamp_unix_ns` come from the device timestamp parsed by MSCL.

### Primary loss indicator
`tick_gap_detected` and `tick_gap_count` are the main fields for identifying possible data loss.

### Secondary timing diagnostic
`timestamp_gap_ns` and `timestamp_gap_detected` help diagnose timing irregularities, but they should be interpreted as supporting evidence rather than the primary loss signal.

### Accepted samples only
These fields are recorded only for sweeps that passed channel validation and were accepted as valid samples by the application.
