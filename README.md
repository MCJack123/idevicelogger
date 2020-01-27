# idevicelogger
Reads system logs from all connected iOS devices to a directory.

## Requirements
* C++ compiler
* libimobiledevice
    * libplist
    * libusbmuxd

## Compiling
`g++ -o idevicelogger idevicelogger.cpp -limobiledevice -lplist -lusbmuxd`

## Usage
```
Usage: idevicelogger [OPTIONS]
Read system logs from all connected iOS devices to a directory.

  -o, --output DIRECTORY        output directory
  -b, --buffer-size SIZE        number of bytes to read before flushing file
  -h, --help                    show this help
```