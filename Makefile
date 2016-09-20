CFLAGS= -pthread -lfftw3f
SOURCES= main.c

all: main

main: main.o
	gcc -o main main.o
