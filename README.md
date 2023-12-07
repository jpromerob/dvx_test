# Compilation

gcc -o dvx_recorder.exe dvx_recorder.c -lm -lncurses -lpthread -I/usr/include/SDL2 -lSDL2
gcc -o vidreader.exe vidreader.c -lm -lncurses -lpthread -I/usr/include/SDL2 -lSDL2

gcc -std=c11 -pedantic -Wall -Wextra -O2 -o c_ncs_dvxplorer.exe c_ncs_dvxplorer.c -D_DEFAULT_SOURCE=1 -lcaer
g++ -std=c++11 -pedantic -Wall -Wextra -O2 -o cpp_ncs_dvxplorer.exe cpp_ncs_dvxplorer.cpp -D_DEFAULT_SOURCE=1 -lcaer
