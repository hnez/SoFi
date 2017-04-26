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

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <fftw3.h>

#include <math.h>

static float hamming(size_t pos, size_t len)
{
  float alpha= 0.53836;
  float beta= 1 - alpha;

  float phi= (2*M_PI*pos)/(len-1);

  float res= alpha - beta * cos(phi);

  return(res);
}

float *window_hamming(size_t len_fft)
{
  float *ret= NULL;

  ret= calloc(len_fft, sizeof(*ret));
  if (!ret) {
    fprintf(stderr, "window_hamming_fwd: allocating window failed\n");
    return(false);
  }

  for(size_t i=0; i<len_fft; i++) {
    ret[i]= hamming(i, len_fft);
  }

  return (ret);
}
