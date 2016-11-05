/*
 * Copyright 2016 Leonard Göhrs <leonard@goehrs.eu>
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#define NUM_SDRS (4)

#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "sdr.h"
#include "fft_thread.h"
#include "synchronize.h"
#include "window.h"

int main(__attribute__((unused)) int argc, __attribute__((unused))char **argv)
{
  struct sdr devs[NUM_SDRS]= {0};
  // struct fft_thread ffts[NUM_SDRS]= {0};

  for (int i=0; i<NUM_SDRS; i++) {
    char path[128];

    sprintf(path, "/dev/swradio%d", i);

    fprintf(stderr, "Open dev %s\n", path);

    if(!sdr_open(&devs[i], path)) {
      return (1);
    }

    if (!sdr_connect_buffers(&devs[i], 8)) {
      return (1);
    }

    if(!sdr_set_center_freq(&devs[i], 883*100*1000)) {
      return(1);
    }
  }

  for (int i=0; i<NUM_SDRS; i++) {
    fprintf(stderr, "Start dev %d\n", i);

    if(!sdr_start(&devs[i])) {
      return(1);
    }
  }

  for (int i=0; i<NUM_SDRS; i++) {
    fprintf(stderr, "Speed up dev %d\n", i);

    if(!sdr_set_sample_rate(&devs[i], 2000000)) {
      return(1);
    }
  }

  fprintf(stderr, "Start syncing\n");

  if(!sync_sdrs(devs, NUM_SDRS, 1<<18)) {
    return(-1);
  }




  return(0);
}
