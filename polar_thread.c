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

#include "polar_thread.h"

bool pt_setup(struct polar_thread *pt, struct fft_thread *fft)
{
  if (!pt || !fft) {
    fprintf(stderr, "pt_setup: NULL as input\n");
    return (false);
  }

  uint32_t len_fft= fft->len_fft;

  pt->phases= calloc(len_fft, sizeof(*pt->phases));
  pt->mag_sq= calloc(len_fft, sizeof(*pt->mag_sq));

  if (!pt->phases || !pt->mag_sq) {
    fprintf(stderr, "pt_setup: Allocating buffers failed\n");
    return (false);
  }

  pt->fft= fft;
  pt->len_fft= len_fft;

  return (true);
}

bool pt_destroy(struct polar_thread *pt)
{
  if (!pt || !pt->phases || !pt->mag_sq) {
    fprintf(stderr, "pt_destroy: NULL input\n");
    return (false);
  }

  free(pt->phases);
  free(pt->mag_sq);

  pt->len_fft= 0;

  return(true);
}

bool pt_process(struct polar_thread *pt)
{
  if (!pt) {
    fprintf(stderr, "pt_process: No pt structure\n");
    return (false);
  }

  for (uint32_t i=0; i<pt->len_fft; i++) {
    float re= pt->fft->buf_out[i][0];
    float im= pt->fft->buf_out[i][1];

    pt->phases[i]= atan2(re, im);
    pt->mag_sq[i]= re*re + im*im;
  }

  return (true);
}
