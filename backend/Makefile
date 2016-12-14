CFLAGS= -std=gnu11
CFLAGS+= -O3 -flto -march=native
CFLAGS+= -Wall -Wextra -Wpedantic -Wstrict-overflow -Wshadow -fno-strict-aliasing
CFLAGS+= -pthread
CFLAGS+= -lfftw3f -lv4l2 -lm -lvolk

ifeq ($(DEBUG), true)
CFLAGS+= -Werror -g
endif

SOURCES= fft_thread.c window.c synchronize.c sdr.c combiner.c
OBJECTS= $(patsubst %.c, %.o, $(SOURCES))

all: sofi vu_meter

sofi: $(OBJECTS) sofi.c
	gcc -o $@ $^ $(CFLAGS)

.PHONY: clean
clean:
	rm -f $(OBJECTS) sofi vu_meter
