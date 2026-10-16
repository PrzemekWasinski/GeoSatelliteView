# Geostationary Satellite View

Geostationary Satellite View automatically collects imagery from the configured GOES, Himawari and EUMETSAT sources. It compiles separate hourly, daily, three-day, weekly and monthly timelapses for every configured source.

Different satellites, sectors, products and spectral bands are selected in `config/satellites.h`. A download is attempted for every source once per configured interval, with requests evenly staggered across that interval. The config file also controls the download interval, timelapse periods and number of concurrent video encoders.

Failed downloads or invalid images get up to three retries after 30, 60 and 120 seconds, provided there is time before the next regular attempt. Retries are scheduled without sleeping in the download loop and preserve the original cadence. Persistent provider errors can still leave gaps; check `geosatelliteview.log` for HTTP status, request duration and retry messages. Downloads are serial, so slow requests can also delay other sources.

Only the shortest compilation period collects images. Longer timelapses concatenate completed base videos with FFmpeg stream copy, avoiding image retention and video re-encoding. For example, daily plus three-day uses three daily videos; daily plus weekly uses seven. All enabled periods share fixed boundaries starting at the first successful capture. Monthly means 30 days.

When the shortest enabled period does not divide every longer period (such as three-day plus weekly), a common base period is used instead: daily clips in that example. These base videos are also kept in permanent outputs. Longer jobs wait for every required base video to be published successfully; missing or failed base jobs prevent incomplete longer videos from being published. Source videos and periodic archived images are preserved. As before, restarting clears temporary collections and starts new periods; unfinished aggregate groups are not resumed across restarts.

Install `ffmpeg` alongside the existing OpenCV and curl dependencies before building. Finished base videos release their temporary images immediately after publication. `EncoderWorkers` bounds both image encoding and video concatenation jobs.

## Output Examples: 

### GOES 19 Full Disc Geocolour
Shows The full view of North and South America in natural colours.

https://github.com/user-attachments/assets/6729f303-59af-45fa-90f1-fd093c0b4d5c

### GOES 18 Moving Object 1
The GOES satellites are also able to track moving objects such as hurricanes or typhoons, below is a 2 day timelapse of a typhoon in the Pacific Ocean.

https://github.com/user-attachments/assets/1a6cc701-310f-4dec-97b1-5f62fdf55cc7

### GOES 19 PNW Channel 10
Shows north-west America's temperature and humidity levels using different colours:
- Red to Yellow: Represents dry regions in the lower atmosphere.
- White, and Blue: Represents relatively moist regions.
- Green and darker colours: Represents cold, high-altitude cloud tops.

https://github.com/user-attachments/assets/f8ca8a2d-dbc9-49a8-a105-09081ad862fd

### Eumetsat Meteosat 12 Geocolour
Shows Europe and Africa in natural colours. The white 'blanket' that appears during nighttime as the satellite's camera is shut off during the night.

https://github.com/user-attachments/assets/899e9d45-49b5-4fd6-9a5c-f0824ee04410

## Tech Stack
    Main language:      C++ (C++17)
    Build system:       CMake
    Scripting:          Python,Bash
    Image compilation:  OpenCV
