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
#define FFT_LEN (1024)

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
#include "combiner.h"

struct sofi_state {
  struct sdr devs[NUM_SDRS];
  struct fft_thread ffts[NUM_SDRS];
  float *window;
  struct combiner cb;
};

float *sofi_alloc_real(void)
{
  float *target= fftwf_alloc_real(FFT_LEN);

  return(target);
}

struct sofi_state *sofi_new(void)
{
  struct sofi_state *s= calloc(1, sizeof(struct sofi_state));

  if(!s) {
    fprintf(stderr, "Allocating sofi state failed!\n");
    return(NULL);
  }

  s->window= window_hamming(FFT_LEN);

  for (int i=0; i<NUM_SDRS; i++) {
    char path[128];

    sprintf(path, "/dev/swradio%d", i);

    fprintf(stderr, "Open dev %s\n", path);

    if(!sdr_open(&s->devs[i], path)) {
      return (NULL);
    }

    if (!sdr_connect_buffers(&s->devs[i], 32)) {
      return (NULL);
    }

    if(!sdr_set_center_freq(&s->devs[i], 975*100*1000)) {
      return(NULL);
    }

    if(!ft_setup(&s->ffts[i], &s->devs[i], s->window,
                 FFT_LEN, 32, 1, true)) {
      return(NULL);
    }
  }

  for (int i=0; i<NUM_SDRS; i++) {
    fprintf(stderr, "Start dev %d\n", i);

    if(!sdr_start(&s->devs[i])) {
      return(NULL);
    }
  }

  for (int i=0; i<NUM_SDRS; i++) {
    fprintf(stderr, "Speed up dev %d\n", i);

    if(!sdr_set_sample_rate(&s->devs[i], 2000000)) {
      return(NULL);
    }
  }

  fprintf(stderr, "Start syncing\n");

  //fprintf(stderr, "*** WARNING: Skipping sync process ***\n");
  if(!sync_sdrs(s->devs, NUM_SDRS, 1<<18)) {
    return(NULL);
  }


  fprintf(stderr, "Start fft threads\n");

  for (int i=0; i<NUM_SDRS; i++) {
    fprintf(stderr, "Start fft %d\n", i);

    if(!ft_start(&s->ffts[i])) {
      return(NULL);
    }
  }

  if(!cb_init(&s->cb, s->ffts, NUM_SDRS)) {
    return(NULL);
  }

  return(s);
}

uint64_t sofi_get_nsdrs(__attribute__((unused)) struct sofi_state *s)
{
  return(NUM_SDRS);
}

uint64_t sofi_get_fftlen(__attribute__((unused)) struct sofi_state *s)
{
  return(FFT_LEN);
}

bool sofi_read(struct sofi_state *s, float *mag_dst, float **phase_dsts)
{
  bool ret= cb_step(&s->cb, mag_dst, phase_dsts);

  return(ret);
}

bool sofi_destroy(__attribute__((unused)) struct sofi_state *s)
{
  fprintf(stderr, "sofi_destroy: not yet implemented\n");

  return(true);
}
