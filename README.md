# Geostationary Satellite View

This is a work in progress project that receives live satellite images of Earth every 10 minutes. It then compiles all images from the past 24 hours to create a 24 hour timelapse of earth.

You can leave this program running on a Raspbery Pi 24/7 and it'll automatically receive satellite imagery and create an 24 hour visualisation of Earth every midnight, here's how:
1) SSH into your Raspberry Pi
2) Clone this repository by running `git clone https://github.com/PrzemekWasinski/GeoSatelliteView/`
3) Switch to this repository: `cd ./GeoSatelliteView`
4) Compile by running in the root folder: `cd ./build/ && cmake . && make -C . && cd ..`
5) Leave it running 24/7: `nohup ./build/output/main > /dev/null 2>&1 &`
6) Close your SSH session

All satellite imagery and videos will be found in the `data` directory.



