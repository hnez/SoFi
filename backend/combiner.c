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
#include <unistd.h>
#include <string.h>

#include <math.h>
#include <volk/volk.h>

#include "combiner.h"

#include "fft_thread.h"


inline float cb_weight(float old, float cur)
{
  return((old * CB_WEIGHT_OLD + cur * CB_WEIGHT_CUR) / CB_WEIGHT_BOTH);
}

inline float squared(float x)
{
  return(x*x);
}

/**
 * When the current phase is at the boundary to rolling over
 * (+M_PI or -M_PI) and the mean is at the other boundary (-M_PI or +M_PI).
 * The mean should wander towards the boundary.
 *
 * To check which direction the mean should go this functions performs
 * the normal mean calculation and versions where the the current
 * value is shifted up/down by 2*M_PI.
 *
 * The version that creates the smallest variance wins.
 */
inline float phase_mean(float old, float cur, float *variance)
{
  float pnaive= cb_weight(old, cur);
  float ptop= cb_weight(old, cur + 2*M_PI);
  float pbottom= cb_weight(old, cur - 2*M_PI);

  float vnaive= squared(pnaive - old);
  float vtop= squared(ptop - old);
  float vbottom= squared(pbottom - old);

  if (vnaive <= vtop && vnaive <= vbottom) {
    *variance= vnaive;

    return(pnaive);
  }
  else {
    if (vtop < vbottom) {
      *variance= vtop;

      return(remainderf(ptop, 2*M_PI));
    }
    else {
      *variance= vbottom;

      return(remainderf(pbottom, 2*M_PI));
    }
  }
}

static uint64_t factorial(uint64_t x)
{
  uint64_t pivot=1;

  for(uint64_t i=1; i<=x; i++) pivot*=i;

  return(pivot);
}

static bool write_forreal(int fd, void *dat, size_t len)
{
  for(size_t written=0; written<len;) {
    ssize_t ret= write(fd, dat, len-written);

    if(ret<0) return(false);

    dat=&((uint8_t *)dat)[ret];
    written+=ret;
  }

  return(true);
}

static bool write_flipped_fft_halves(int fd, float *samples, size_t len)
{
  if(len%2 != 0) {
    fprintf(stderr, "write_flipped_fft_halves: got uneven input length\n");
    return(false);
  }

  float *top= &samples[len/2];
  float *bottom= &samples[0];

  if(!write_forreal(fd, top, (len/2) * sizeof(*samples)) ||
     !write_forreal(fd, bottom, (len/2) * sizeof(*samples))) {
    
    fprintf(stderr, "write_flipped_fft_halves: write failed\n");
    return(false);
  }

  return(true);
}

bool cb_run(int fd, struct fft_thread *ffts, size_t num_ffts)
{
  if (!ffts || !num_ffts) {
    fprintf(stderr, "cb_run: NULL as input\n");
    return (false);
  }

  size_t num_edges= factorial(num_ffts - 1);

  size_t len_fft= ffts[0].len_fft;

  for (size_t i=0; i<num_ffts; i++) {
    if (ffts[i].len_fft != len_fft) {
      fprintf(stderr, "cb_run: fft lengths do not match\n");
      return(false);
    }
  }

  struct {
    float *phase;
    float *mag_sq;
  } inputs[num_ffts];

  for (size_t i=0; i<num_ffts; i++) {
    inputs[i].phase= fftwf_alloc_real(len_fft);
    inputs[i].mag_sq= fftwf_alloc_real(len_fft);
  }

  struct {
    size_t input_a;
    size_t input_b;
    float *mean_phase;
    float *mean_var;
    float *mean_mag_sq;
  } outputs[num_edges];

  for(size_t ina=0, i=0; ina<num_ffts; ina++) {
    for(size_t inb=ina+1; inb<num_ffts; inb++, i++) {
      fprintf(stderr, "cb_run: edge %ld: %ld <-> %ld\n", i, ina, inb);

      outputs[i].input_a= ina;
      outputs[i].input_b= inb;

      outputs[i].mean_phase= fftwf_alloc_real(len_fft);
      outputs[i].mean_var= fftwf_alloc_real(len_fft);
      outputs[i].mean_mag_sq= fftwf_alloc_real(len_fft);

      memset(outputs[i].mean_phase, 0, sizeof(float) * len_fft);
      memset(outputs[i].mean_var, 0, sizeof(float) * len_fft);
      memset(outputs[i].mean_mag_sq, 0, sizeof(float) * len_fft);
    }
  }

  for(uint64_t frame=0; ; frame++) {
    for(size_t fi=0; fi<num_ffts; fi++) {
      struct fft_buffer *buf= NULL;

      buf= ft_get_frame(&ffts[fi], frame);

      volk_32fc_s32f_atan2_32f(inputs[fi].phase,
                               (lv_32fc_t*)buf->out,
                               1.0,
                               len_fft);

      volk_32fc_magnitude_squared_32f(inputs[fi].mag_sq,
                                      (lv_32fc_t*)buf->out,
                                      len_fft);

      ft_release_frame(&ffts[fi], buf);
    }

    for(size_t ei=0; ei<num_edges; ei++) {
      size_t ina= outputs[ei].input_a;
      size_t inb= outputs[ei].input_b;

      for(size_t i=0; i<len_fft; i++) {
        float phase= remainderf(inputs[ina].phase[i] - inputs[inb].phase[i], 2*M_PI);
        float mag_sq= inputs[ina].mag_sq[i] * inputs[ina].mag_sq[i];

        float variance= 0;

        outputs[ei].mean_phase[i]= phase_mean(outputs[ei].mean_phase[i], phase, &variance);;

        outputs[ei].mean_var[i]= cb_weight(outputs[ei].mean_var[i], variance);
        outputs[ei].mean_mag_sq[i]= cb_weight(outputs[ei].mean_mag_sq[i], mag_sq);
      }
    }

    if((frame % 64) == 0) {
      for(size_t ei=0; ei<num_edges; ei++) {
        write_flipped_fft_halves(fd, outputs[ei].mean_phase, len_fft);
        write_flipped_fft_halves(fd, outputs[ei].mean_var, len_fft);
        write_flipped_fft_halves(fd, outputs[ei].mean_mag_sq, len_fft);
      }
    }
  }

  return(true);
}
