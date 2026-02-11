# Geostationary Satellite View

GeostationaryvSatellite View is program that receives live satellite images of Earth every 10 minutes from a selected geostationary satellite. It then compiles all images from the past 24 hours to create a 24 hour timelapse of earth.

Output Example: https://www.youtube.com/shorts/U-eSh_t3CEo

You can leave this program running on a Raspbery Pi 24/7 and it'll automatically receive satellite imagery and create an 24 hour visualisation of Earth every midnight, here's how:
1) SSH into your Raspberry Pi/Computer
2) Clone this repository by running `git clone https://github.com/PrzemekWasinski/GeoSatelliteView/`
3) Switch to this repository: `cd ./GeoSatelliteView`
4) Compile by running: `cd ./build/ && cmake . && make -C . && cd ..`
5) Start the program: `nohup ./build/output/main > /dev/null 2>&1 &`
6) Close your SSH session

The program will run endlessly and all satellite imagery and videos will be found in the `data` directory.



