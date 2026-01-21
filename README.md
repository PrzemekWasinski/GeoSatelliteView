# Geostationary Satelliet View

This is a work in progress project that receives live images of Earth every 10 minutes. It then compiles all images from the past 24 hours to create a 24H timelapse of earth

Compile with: ``g++ -std=c++17 main.cpp timelapse.cpp -o ./build/output/main `pkg-config --cflags --libs opencv4` -lcurl``
