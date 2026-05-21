#Bash script to compile this program

cmake -S . -B build && cmake --build build -j$(nproc) && echo "Compiled successfully! Run with: ./build/main"
