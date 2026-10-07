# Geostationary Satellite View

Geostationary Satellite View automatically collects imagery from the configured GOES, Himawari and EUMETSAT sources. It compiles separate hourly, daily, three-day, weekly and monthly timelapses for every configured source.

Different satellites, sectors, products and spectral bands are selected in `config/satellites.h`. A download is attempted for every source once per configured interval, with requests evenly staggered across that interval. The config file also controls the download interval, timelapse periods and number of concurrent video encoders.

Failed downloads or invalid images get up to three retries after 30, 60 and 120 seconds, provided there is time before the next regular attempt. Retries are scheduled without sleeping in the download loop and preserve the original cadence. Persistent provider errors can still leave gaps; check `geosatelliteview.log` for HTTP status, request duration and retry messages. Downloads are serial, so slow requests can also delay other sources.

## Output Example: 

https://github.com/user-attachments/assets/24c6d048-e6a6-46e5-83bf-a6f6c49b1c12

All imagery and videos are stored in `data/`, organized as:

```text
data/
  imagery/
    daily/                         # hourly/, 3daily/, weekly/ when enabled
      2026-10-07_00-00-00/          # UTC period start
        GOES19/GOES19-FD-GEOCOLOR/
          20261007_120000.jpg
  output/
    daily/
      2026-10-07_00-00-00/
        GOES19/GOES19-FD-GEOCOLOR/output.mp4
    monthly/                       # enabled periods use the same layout
      2026-10-01_00-00-00/
        GOES19/GOES19-FD-GEOCOLOR/output.mp4
```

Downloaded images are resized to 1080 ? 1080 and saved as JPEGs at quality 90 for each enabled period. `DataPath` can point to the `data` folder itself or its parent directory.

Each enabled period builds its timelapse from the JPEGs in its own imagery folder. Hourly and daily align to UTC hours and days; three-day, weekly and monthly periods use fixed 72-hour, 7-day and 30-day blocks anchored to 1970-01-01 UTC. Existing folders from the older storage layout are not automatically migrated.

The program will delete all images after compiling them to save disk space, this can be turned off by setting `Delete` to `False` in `config/config.yml`.

This program is made to be ran and left, to automatically gather and compile satellite imagery. To start the program follow these steps:

1) Run the compile script: `compile.sh`
2) Start the program by running: `./build/main`
3) I recommend creating a Systemd process to automatically start this program on boot.

## Tech Stack
    Main language:      C++ (C++17)
    Build system:       CMake
    Scripting:          Python,  Bash
    Image compilation:  OpenCV

All application output, including OpenCV and codec diagnostics, is appended to `geosatelliteview.log`. This also applies when launching `./build/main` directly from the project directory.
