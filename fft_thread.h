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

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <fftw3.h>

#include "sdr.h"

struct fft_thread {
  struct sdr dev;

  uint32_t len_fft;

  fftwf_complex *buf_in;
  fftwf_complex *buf_out;

  fftwf_plan plan;
};

bool ft_setup(struct fft_thread *ft, uint32_t len_fft);
bool ft_get_input(struct fft_thread *ft);
bool ft_run_fft(struct fft_thread *ft);
bool ft_destroy(struct fft_thread *ft);
