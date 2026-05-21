# Geostationary Satellite View

Geostationary Satellite View is a program that automatically collects satellite imagery from geostationary satellites such as the NOAA series GOES 16, 18 and 19. After receiving a certain amount of images, set by the user in `config/config.yml`, the program will compile all images into a hourly, daily, weekly or monthly timelapse.

Different satellites, sectors, products and radio bands can be chosen from `config/satellites.cpp` which contain different data such as Fire Temperatures, Air Mass, Dust Levels and lots more. The config file also allows you to set custom image download intervals and timelapse times. 

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

