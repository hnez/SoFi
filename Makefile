CFLAGS= -pthread -lfftw3f -lv4l2
SOURCES= main.c

all: main

main: main.o
	gcc -o main main.o $(CFLAGS)
