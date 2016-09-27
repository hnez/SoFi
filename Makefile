CFLAGS= -std=gnu11
#CFLAGS+= -g
CFLAGS+= -O3 -flto -march=native
CFLAGS+= -Wall -Wextra -Wpedantic -Wstrict-overflow -Werror -Wshadow -fno-strict-aliasing
CFLAGS+= -pthread
CFLAGS+= -lfftw3f -lv4l2

all: main

main: main.o sdr.o fft_thread.o
	gcc -o main main.o sdr.o fft_thread.o $(CFLAGS)
