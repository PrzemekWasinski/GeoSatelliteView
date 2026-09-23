# Geostationary Satellite View

Geostationary Satellite View automatically collects imagery from the configured GOES, Himawari and EUMETSAT sources. It compiles a separate hourly, daily, weekly or monthly timelapse for every configured source.

Different satellites, sectors, products and spectral bands are selected in `config/satellites.h`. Every source is downloaded once per configured interval, with requests evenly staggered across that interval. The config file also controls the download interval, timelapse periods and number of concurrent video encoders.

## Output Example: 

https://github.com/user-attachments/assets/24c6d048-e6a6-46e5-83bf-a6f6c49b1c12

All imagery and videos are stored in `data/`. The program will delete all images after compiling them to save disk space, this can be turned off by setting `Delete` to `False` in `config/config.yml`.

This program is made to be ran and left, to automatically gather and compile satellite imagery. To start the program follow these steps:

1) Run the compile script: `compile.sh`
2) Start the program by running: `./build/main`
3) I recommend creating a Systemd process to automatically start this program on boot.

## Tech Stack
    Main language:      C++ (C++17)
    Build system:       CMake
    Scripting:          Python,  Bash
    Image compilation:  OpenCV
