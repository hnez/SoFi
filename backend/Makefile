CFLAGS= -std=gnu11
CFLAGS+= -O3 -flto -march=native
CFLAGS+= -Wall -Wextra -Wpedantic -Wstrict-overflow -Wshadow -fno-strict-aliasing
CFLAGS+= -pthread
CFLAGS+= -lfftw3f -lv4l2 -lm -lvolk

ifeq ($(DEBUG), true)
CFLAGS+= -Werror -g
endif

PROGNAME= cheapodoa
SOURCES= main.c fft_thread.c window.c synchronize.c sdr.c
OBJECTS= $(patsubst %.c, %.o, $(SOURCES))

all: $(PROGNAME)

$(PROGNAME): $(OBJECTS)
	gcc -o $(PROGNAME) $(OBJECTS) $(CFLAGS)

.PHONY: clean
clean:
	rm -f $(OBJECTS) main
