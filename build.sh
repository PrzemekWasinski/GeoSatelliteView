#Bash script to compile this program

cmake -S . -B build

cmake --build build -j$(nproc)