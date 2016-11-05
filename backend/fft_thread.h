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

#include <pthread.h>

#include <fftw3.h>

#include "sdr.h"

struct fft_buffer {
  uint64_t consumers;
  uint64_t frame_no;

  fftwf_complex *in;
  fftwf_complex *out;

  fftwf_plan plan;
};

struct fft_thread {
  struct sdr *dev;

  size_t len_fft;
  uint64_t consumers;
  bool running;
  pthread_t thread;

  float *window;

  struct fft_buffer *buffers;

  size_t buffers_count;
  pthread_mutex_t buffers_meta_lock;
  pthread_cond_t buffers_meta_notify;
};

bool ft_setup(struct fft_thread *ft, struct sdr *dev, float *window, size_t len_fft,
              size_t buffers_count, uint64_t consumers_count);

bool ft_start(struct fft_thread *ft);
bool ft_stop(struct fft_thread *ft);

struct fft_buffer *ft_get_frame(struct fft_thread *ft, uint64_t frame);
bool ft_release_frame(struct fft_thread *ft, struct fft_buffer *buf);

bool ft_destroy(struct fft_thread *ft);
