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

#define NUM_SDRS 2

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

bool dev_setup(struct sdr *sdr, struct fft_thread *ft, char*path)
{
  if(!sdr_open(sdr, path)) {
    return (false);
  }

  if (!sdr_connect_buffers(sdr, 8)) {
    return (false);
  }

  if(!sdr_set_sample_rate(sdr, 2000000)) {
    return(false);
  }

  if(!sdr_set_center_freq(sdr, 89000000)) {
    return(false);
  }

  if(!ft_setup(ft, sdr, 1<<14)) {
    return(false);
  }

  return(true);
}

bool dev_destroy(struct sdr *sdr, struct fft_thread *ft)
{
  if (!sdr_stop(sdr)) {
    return(false);
  }

  if (!sdr_destroy(sdr)) {
    return(false);
  }

  if(!ft_destroy(ft)) {
    return(false);
  }

  return(true);
}

int main(__attribute__((unused)) int argc, __attribute__((unused))char **argv)
{
  struct sdr sdrs[NUM_SDRS]= {0};
  struct fft_thread fts[NUM_SDRS]= {0};

  for (int i=0; i<NUM_SDRS; i++) {
    char path[128];

    sprintf(path, "/dev/swradio%d", i);

    fprintf(stderr, "Open dev %s\n", path);

    if(!dev_setup(&sdrs[i], &fts[i], path)) {
      return (-1);
    }
  }

  for (int i=0; i<NUM_SDRS; i++) {
    fprintf(stderr, "Start dev %d\n", i);

    if(!sdr_start(&sdrs[i])) {
      return (-1);
    }
  }

  fprintf(stderr, "Syncronize\n");

  for (int i=0; i<20; i++) {
    if (!sync_fft_threads(fts, NUM_SDRS)) {
      return(-1);
    }
  }

  for (int i=0; i<NUM_SDRS; i++) {
    fprintf(stderr, "Destroy dev %d\n", i);

    if(!dev_destroy(&sdrs[i], &fts[i])) {
      return (-1);
    }
  }

  return(0);
}
