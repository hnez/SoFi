CFLAGS= -std=gnu11
CFLAGS+= -O3 -flto -march=native -ffast-math -funsafe-math-optimizations
CFLAGS+= -Wall -Wextra -Wpedantic -Wstrict-overflow -Wshadow -fno-strict-aliasing
CFLAGS+= -pthread
CFLAGS+= -lfftw3f -lv4l2 -lm -lvolk
CFLAGS+= -fPIC

ifeq ($(DEBUG), true)
CFLAGS+= -Werror -g
endif

SOURCES= fft_thread.c window.c synchronize.c sdr.c combiner.c
OBJECTS= $(patsubst %.c, %.o, $(SOURCES))

all: libsofi.so rf_monitor

libsofi.so: $(OBJECTS) libsofi.c
	gcc -shared -o $@ $^ $(CFLAGS)

rf_monitor: $(OBJECTS) rf_monitor.c
	gcc -o $@ $^ $(CFLAGS)

.PHONY: clean
clean:
	rm -f $(OBJECTS) sofi vu_meter
