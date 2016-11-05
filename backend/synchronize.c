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

#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <float.h>

#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "sdr.h"
#include "fft_thread.h"
#include "window.h"


static void multiply_conjugate(fftwf_complex *dst, fftwf_complex *a, fftwf_complex *b, size_t len)
{
  for (; len; len--, dst++, a++, b++) {
    *dst[0]= (*a[0])*(*b[0]) + (*a[1])*(*b[1]);
    *dst[1]= (*a[1])*(*b[0]) - (*a[0])*(*b[1]);
  }
}

inline float mag_squared(fftwf_complex z)
{
  return(z[0]*z[0] + z[1]*z[1]);
}

inline int64_t arr_min(int64_t *vals, size_t len)
{
  int64_t min= vals[0];

  for(size_t i=1; i<len; i++) {
    if (vals[i] < min) min=vals[i];
  }

  return(min);
}

bool sync_sdrs(struct sdr *devs, size_t num_devs, size_t sync_len)
{
  if (!devs || !num_devs) {
    fprintf(stderr, "sync_sdrs: no devices\n");
    return(false);
  }

  float *window= window_hamming(sync_len);
  if(!window) {
    fprintf(stderr, "sync_sdrs: creating window function failed\n");
    return(false);
  }

  struct fft_thread ffts[num_devs];

  for (size_t i=0; i<num_devs; i++) {
    fprintf(stderr, "sync_sdrs: setting up dev %ld\n", i);

    if (!ft_setup(&ffts[i], &devs[i], window, sync_len, 1, 1)) {
      fprintf(stderr, "sync_sdrs: ft_setup failed\n");
      return(false);
    }

    if (!ft_start(&ffts[i])) {
      fprintf(stderr, "sync_sdrs: ft_start failed\n");
      return(false);
    }
  }

  fftwf_complex *conjugate= fftwf_alloc_complex(sync_len);
  fftwf_complex *correlation= fftwf_alloc_complex(sync_len);
  if(!conjugate || !correlation) {
    fprintf(stderr, "sync_sdrs: allocating correlation buffers faileded\n");

    return(false);
  }

  fftwf_plan plan= fftwf_plan_dft_1d(sync_len, conjugate, correlation,
                                     FFTW_BACKWARD, FFTW_ESTIMATE);
  if (!plan) {
    fprintf(stderr, "sync_sdrs: fftwf_plan failed\n");
    return(false);
  }

  bool synced= false;

  for(uint64_t frame=0; !synced; frame++) {
    fprintf(stderr, "sync_sdrs: syncing frame %ld\n", frame);

    struct fft_buffer *bufs[num_devs];

    for (size_t i=0; i<num_devs; i++) {
      bufs[i]= ft_get_frame(&ffts[i], frame);
      if(!bufs[i]) {
        fprintf(stderr, "sync_sdrs: retrieving fft failed\n");
        return(false);
      }
    }

    int64_t shifts[num_devs];
    shifts[0]= 0;

    for (size_t sdev=1; sdev<num_devs; sdev++) {
      multiply_conjugate(conjugate, bufs[0]->out, bufs[sdev]->out, sync_len);

      fftwf_execute(plan);

      struct {
        float mag_sq;
        int64_t shift;
      } maximum= { .mag_sq=FLT_MIN, .shift=0};

      // Check if maximum correlation is in left half of fft (positive shift)
      for (uint32_t i=0; i<sync_len/2; i++) {
        float ms= mag_squared(correlation[i])/window[i];

        if (ms > maximum.mag_sq) {
          maximum.mag_sq= ms;
          maximum.shift= i;
        }
      }

      // Or right half of fft (negative shift)
      for (uint32_t f=0, r=sync_len-1; f<sync_len/2; f++, r--) {
        float ms= mag_squared(correlation[r])/window[r];

        if (ms > maximum.mag_sq) {
          maximum.mag_sq= ms;
          maximum.shift= -(int64_t)f;
        }
      }

      shifts[sdev]= maximum.shift;
    }

    synced= true;
    int64_t min_shift= arr_min(shifts, num_devs);
    for(size_t dev=0; dev<num_devs; dev++) {
      shifts[dev]-= min_shift;

      if (shifts[dev]) synced= false;
    }

    // Output the calibrations that will be performed
    fprintf(stderr, "sync_sdrs: receiver offset calibration: ");
    for(size_t dev=0; dev<num_devs; dev++) fprintf(stderr, "%li ", shifts[dev]);
    fprintf(stderr, "\n");

    // Read shifts[dev] 2-byte samples
    for(size_t dev=0; dev<num_devs; dev++) {
      if(frame > 10) sdr_seek(&devs[dev], shifts[dev] * 2);
    }

    /* Note: Do not release the fft buffer before the
     * receiver realignement is made.
     * Otherwise the fft thread might be reading from the device */
    for (size_t i=0; i<num_devs; i++) {
      if(synced) {
        if (!ft_stop(&ffts[i])) {
          fprintf(stderr, "sync_sdrs: stopping fft failed\n");
          return(false);
        }
      }

      if (!ft_release_frame(&ffts[i], bufs[i])) {
        fprintf(stderr, "sync_sdrs: releasing fft failed\n");
        return(false);
      }
    }
  }

  fftwf_destroy_plan(plan);
  fftwf_free(conjugate);
  fftwf_free(correlation);

  for (size_t i=0; i<num_devs; i++) {
    if(!ft_destroy(&ffts[i])) {
      fprintf(stderr, "sync_sdrs: destroying fft thread failed\n");
      return(false);
    }
  }

  free(window);

  return (true);
}
