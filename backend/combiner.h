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

#include "fft_thread.h"

#define CB_WEIGHT_OLD (400)
#define CB_DECIMATOR (163)

struct combiner {
  size_t num_edges;
  size_t num_ffts;
  size_t len_fft;

  uint64_t frame_no;

  fftwf_complex *tmp_cplx;
  float *tmp_real;

  struct {
    struct fft_thread *thread;
    struct fft_buffer *buffer;
  } *inputs;

  struct {
    size_t input_a;
    size_t input_b;

    fftwf_complex *acc;
  } *outputs;
};

bool cb_init(struct combiner *cb, struct fft_thread *ffts, size_t num_ffts);
bool cb_step(struct combiner *cb, float *mag_dst, float **phase_dsts);
bool cb_cleanup(struct combiner *cb);
