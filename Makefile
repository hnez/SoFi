CFLAGS= -std=gnu11
CFLAGS+= -O3 -flto -march=native
CFLAGS+= -Wall -Wextra -Wpedantic -Wstrict-overflow -Werror -Wshadow -fno-strict-aliasing
CFLAGS+= -pthread
CFLAGS+= -lfftw3f -lv4l2

SOURCES= main.c

all: main

main: main.o
	gcc -o main main.o $(CFLAGS)
