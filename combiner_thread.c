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

#include <math.h>

#include "combiner_thread.h"

#include "fft_thread.h"

inline float ct_weight(float old, float cur)
{
  return((old * CT_WEIGHT_OLD + cur * CT_WEIGHT_CUR) / CT_WEIGHT_BOTH);
}

inline float squared(float x)
{
  return(x*x);
}

bool ct_setup(struct combiner_thread *ct, struct polar_thread *a, struct polar_thread *b)
{
  if (!ct || !a || !b) {
    fprintf(stderr, "ct_setup: NULL as input\n");
    return (false);
  }

  if (a->len_fft != b->len_fft) {
    fprintf(stderr, "ct_setup: fft lengths do not match %d vs. %d\n",
            a->len_fft, b->len_fft);
    return(false);
  }

  uint32_t len_fft= a->len_fft;

  ct->polar_a= a;
  ct->polar_b= b;

  ct->phases=    calloc(len_fft, sizeof(*ct->phases));
  ct->variances= calloc(len_fft, sizeof(*ct->variances));
  ct->mag_sq=    calloc(len_fft, sizeof(*ct->mag_sq));

  if (!ct->phases || !ct->variances || !ct->mag_sq) {
    fprintf(stderr, "ct_setup: Allocating buffers failed\n");
    return (false);
  }

  ct->len_fft= len_fft;

  return (true);
}

bool ct_destroy(struct combiner_thread *ct)
{
  if (!ct || !ct->phases || !ct->variances || !ct->mag_sq) {
    fprintf(stderr, "ct_destroy: NULL input\n");
    return (false);
  }

  free(ct->phases);
  free(ct->variances);
  free(ct->mag_sq);

  ct->len_fft= 0;
}

bool ct_process(struct combiner_thread *ct)
{
  if (!ct) {
    fprintf(stderr, "ct_process: No ct structure\n");

    return (false);
  }

  for (uint32_t i=0; i<ct->len_fft; i++) {
    // Calculate phase differences
    float dphi_old= ct->phases[i];
    float dphi_cur= remainderf(ct->polar_a->phases[i] - ct->polar_b->phases[i],
                               2*M_PI);

    /* Averaging angles is hard.
     * I hope this works */
    if (dhpi_old < -M_PI_2 && dphi_cur > M_PI_2) {
    dhpi_cur-= 2*M_PI;
    }
    else if (dphi_old > M_PI_2 && dphi_cur < -M_PI_2) {
      dhpi_cur+= 2*M_PI;
    }

    float dphi_new= remainderf(ct_weight(dphi_old, dphi_cur), 2*M_PI);
    ct->phases[i]= dphi_new;

    // Calculate variances
    float var_old= ct->variances[i];
    float var_cur= squared(dphi_cur - dphi_new);
    float var_new= ct_weight(var_old, var_cur);
    ct->variances[i]= var_new;

    // Calculate magnitudes
    float magsq_old= ct->mag_sq[i];
    float magsq_cur= ct->polar_a->mag_sq[i] * ct->polar_b->mag_sq[i];
    float magsq_new= ct_weight(magsq_old, magsq_cur);
    ct->mag_sq[i]= magsq_new;
  }

  return (true);
}
