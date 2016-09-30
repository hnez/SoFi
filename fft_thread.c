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

#include <errno.h>
#include <string.h>

#include <fftw3.h>

#include "fft_thread.h"

#include "sdr.h"

bool ft_setup(struct fft_thread *ft, struct sdr *sdr, uint32_t len_fft)
{
  if (!ft || !sdr) {
    fprintf(stderr, "ft_setup: No ft or sdr structure\n");

    return (false);
  }

  ft->buf_in=  fftwf_alloc_complex(len_fft);
  ft->buf_out= fftwf_alloc_complex(len_fft);

  if (!ft->buf_in || !ft->buf_out) {
    fprintf(stderr, "ft_setup: allocating input/output buffers of length %d failed\n", len_fft);

    return(false);
  }


  ft->plan= fftwf_plan_dft_1d(len_fft, ft->buf_in, ft->buf_out, FFTW_FORWARD, FFTW_MEASURE);

  if (!ft->plan) {
    fprintf(stderr, "ft_setup: fftwf_plan failed\n");

    return(false);
  }

  ft->len_fft= len_fft;
  ft->dev= sdr;

  return(true);
}

bool ft_get_input(struct fft_thread *ft)
{
  if (!ft || !ft->dev) {
    fprintf(stderr, "ft_get_input: No ft or sdr  structure\n");

    return (false);
  }

  uint32_t pos=0;

  while(pos < ft->len_fft) {
    struct {
      uint8_t r;
      uint8_t i;
    } *samples;

    size_t bytes_rem= sizeof(*samples) * (ft->len_fft - pos);
    ssize_t bytes_rd= sdr_peek(ft->dev, bytes_rem, (void *)&samples);

    if (bytes_rd < 0) {
      return(false);
    }

    size_t samples_rd= bytes_rd/sizeof(*samples);

    for (size_t i=0; i<samples_rd; i++, pos++) {
      ft->buf_in[pos][0]= (float)samples[i].r - 127;
      ft->buf_in[pos][1]= (float)samples[i].i - 127;
    }

    if (!sdr_done(ft->dev)) {
      return(false);
    }
  }

  return(true);
}

bool ft_run_fft(struct fft_thread *ft)
{
  if (!ft || !ft->plan) {
    fprintf(stderr, "ft_run_fft: No ft structure\n");

    return (false);
  }

  fftwf_execute(ft->plan);

  return(true);
}

bool ft_destroy(struct fft_thread *ft)
{
  if (!ft) {
    fprintf(stderr, "ft_destroy: No ft structure\n");

    return (false);
  }

  fftwf_destroy_plan(ft->plan);

  fftwf_free(ft->buf_in);
  fftwf_free(ft->buf_out);

  return (true);
}
