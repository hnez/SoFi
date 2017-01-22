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

static uint64_t factorial(uint64_t x)
{
  uint64_t pivot=1;

  for(uint64_t i=1; i<=x; i++) pivot*=i;

  return(pivot);
}

static bool write_interruptsafe(int fd, void *dat, size_t len)
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

  if(!write_interruptsafe(fd, top, (len/2) * sizeof(*samples)) ||
     !write_interruptsafe(fd, bottom, (len/2) * sizeof(*samples))) {

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

  fftwf_complex *tmp_cplx= NULL;
  float *tmp_real= NULL;
  float *mag_acc= NULL;

  tmp_cplx= fftwf_alloc_complex(len_fft);
  tmp_real= fftwf_alloc_real(len_fft);
  mag_acc= fftwf_alloc_real(len_fft);

  if(!tmp_cplx || !tmp_real || !mag_acc) {
    fprintf(stderr, "cb_run: allocating temp buffer failed\n");

    return(false);
  }

  struct {
    struct fft_thread *thread;
    struct fft_buffer *buffer;
  } inputs[num_ffts];

  for(size_t fi=0; fi<num_ffts; fi++) {
    inputs[fi].thread= &ffts[fi];
    inputs[fi].buffer= NULL;
  }

  struct {
    size_t input_a;
    size_t input_b;

    fftwf_complex *mean;
  } outputs[num_edges];

  for(size_t ina=0, i=0; ina<num_ffts; ina++) {
    for(size_t inb=ina+1; inb<num_ffts; inb++, i++) {
      fprintf(stderr, "cb_run: edge %ld: %ld <-> %ld\n", i, ina, inb);

      outputs[i].input_a= ina;
      outputs[i].input_b= inb;

      outputs[i].mean= fftwf_alloc_complex(len_fft);

      if(!outputs[i].mean) {
        fprintf(stderr, "cb_run: allocating mean buffer failed\n");

        return(false);
      }

      memset(outputs[i].mean, 0, sizeof(*outputs[i].mean) * len_fft);
    }
  }

  for(uint64_t frame=0; ; frame++) {
    for(size_t fi=0; fi<num_ffts; fi++) {
      inputs[fi].buffer= ft_get_frame(inputs[fi].thread, frame);

      if(!inputs[fi].buffer) {
        fprintf(stderr, "cb_run: Getting frame from fft_thread failed\n");

        return(false);
      }
    }

    for(size_t ei=0; ei<num_edges; ei++) {
      size_t ina= outputs[ei].input_a;
      size_t inb= outputs[ei].input_b;

      /* Calculate Phase difference between
       * the two inputs for all frequencies */
      volk_32fc_x2_multiply_conjugate_32fc(tmp_cplx,
                                           inputs[ina].buffer->out,
                                           inputs[inb].buffer->out,
                                           len_fft);

      /* Calculate the weigthed mean between old and new value.
       * Using one of the inputs to add as target might be a bad idea,
       * as it is not documented but it looks fine in the volk source. */
      volk_32f_s32f_normalize((float *)outputs[ei].mean,
                              1.0/CB_WEIGHT_OLD,
                              2*len_fft);

      volk_32f_x2_add_32f((float *)outputs[ei].mean,
                          (float *)outputs[ei].mean,
                          (float *)tmp_cplx,
                          2*len_fft);

      volk_32f_s32f_normalize((float *)outputs[ei].mean,
                              CB_WEIGHT_OLD+1,
                              2*len_fft);
    }

    for(size_t fi=0; fi<num_ffts; fi++) {
      if(!ft_release_frame(inputs[fi].thread, inputs[fi].buffer)) {
        fprintf(stderr, "cb_run: Relasing fft frame failed\n");

        return(false);
      }
    }

    if((frame % CB_DECIMATOR) == 0) {
      memset(mag_acc, 0, sizeof(*mag_acc) * len_fft);

      for(size_t ei=0; ei<num_edges; ei++) {
        /* Calculate and accumulate magnitudes squared */
        volk_32fc_magnitude_squared_32f(tmp_real,
                                        outputs[ei].mean,
                                        len_fft);

        volk_32f_x2_add_32f(mag_acc, mag_acc, tmp_real, len_fft);

        /* Calculate and output phase differences */
        volk_32fc_s32f_atan2_32f(tmp_real,
                                 outputs[ei].mean,
                                 1.0,
                                 len_fft);

        write_flipped_fft_halves(fd, tmp_real, len_fft);
      }

      write_flipped_fft_halves(fd, mag_acc, len_fft);
    }
  }
}
