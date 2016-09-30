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

#include "sdr.h"
#include "fft_thread.h"

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

bool sync_fft_threads(struct fft_thread *ffts, size_t num_ffts)
{
  if (!ffts || !num_ffts) {
    fprintf(stderr, "sync_fft_threads: missing fft structs\n");
    return(false);
  }

  uint32_t len_fft= ffts[0].len_fft;

  for (size_t num=1; num<num_ffts; num++) {
    if (ffts[num].len_fft != len_fft) {
      fprintf(stderr, "sync_fft_threads: fft lengths do not match\n");
      return(false);
    }
  }

  fftwf_complex *conjugate= fftwf_alloc_complex(len_fft);
  fftwf_complex *correlation= fftwf_alloc_complex(len_fft);
  if(!conjugate || !correlation) {
    fprintf(stderr, "sync_fft_threads: allocating correlation buffers faileded\n");

    return(false);
  }

  fftwf_plan plan= fftwf_plan_dft_1d(len_fft, conjugate, correlation,
                                     FFTW_BACKWARD, FFTW_ESTIMATE);

  if (!plan) {
    fprintf(stderr, "sync_fft_threads: fftwf_plan failed\n");
    return(false);
  }

  // Get samples for all receivers and calculate ffts
  for (size_t num=0; num<num_ffts; num++) {
    if(!ft_get_input(&ffts[num])) {
      fprintf(stderr, "sync_fft_threads: getting samples for thread %ld failed\n", num);

      return(false);
    }

    if(!ft_run_fft(&ffts[num])) {
      fprintf(stderr, "sync_fft_threads: calculating fft for thread %ld failed\n", num);

      return(false);
    }
  }

  int64_t shifts[num_ffts];
  shifts[0]= 0;

  for (size_t num=1; num<num_ffts; num++) {
    multiply_conjugate(conjugate, ffts[0].buf_out, ffts[num].buf_out, len_fft);

    fftwf_execute(plan);

    struct {
      float mag_sq;
      int64_t shift;
    } maximum= { .mag_sq=FLT_MIN, .shift=0};

    // Check if maximum correlation is in left half of fft (positive shift)
    for (uint32_t i=0; i<len_fft/2; i++) {
      float ms= mag_squared(correlation[i]);

      if (ms > maximum.mag_sq) {
        maximum.mag_sq= ms;
        maximum.shift= i;
      }
    }

    // Or right half of fft (negative shift)
    for (uint32_t i=0; i<len_fft/2; i++) {
      float ms= mag_squared(correlation[(len_fft-1) - i]);

      if (ms > maximum.mag_sq) {
        maximum.mag_sq= ms;
        maximum.shift= -i;
      }
    }

    shifts[num]= maximum.shift;
  }

  fftwf_destroy_plan(plan);
  fftwf_free(conjugate);
  fftwf_free(correlation);

  // Make the smallest shift a zero shift
  int64_t min= arr_min(shifts, num_ffts);
  for(size_t num=0; num<num_ffts; num++) {
    shifts[num]-= min;
  }

  // Output the calibrations that will be performed
  fprintf(stderr, "receiver offset calibration: ");
  for(size_t num=0; num<num_ffts; num++) fprintf(stderr, "%ld ", shifts[num]);
  fprintf(stderr, "\n");

  // Perform calibrations
  for(size_t num=0; num<num_ffts; num++) {
    sdr_peek(ffts[num].dev, shifts[num], NULL);
    sdr_done(ffts[num].dev);
  }

  return(true);
}
