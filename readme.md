# Compilation

gcc -o <*exe> <*.c> -lm -lncurses -lpthread -I/usr/include/SDL2 -lSDL2
gcc -std=c11 -pedantic -Wall -Wextra -O2 -o <*.exe> <*.c> -D_DEFAULT_SOURCE=1 -lcaer

