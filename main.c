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
#include <unistd.h>

#include "sdr.h"
#include "fft_thread.h"

int main(__attribute__((unused)) int argc, __attribute__((unused))char **argv)
{
  struct sdr sdr= {0};
  struct fft_thread ft= {0};

  if(!sdr_open(&sdr, "/dev/swradio0")) {
    return (-1);
  }

  if (!sdr_connect_buffers(&sdr, 8)) {
    return (-1);
  }

  if(!sdr_set_sample_rate(&sdr, 2000000)) {
    return(-1);
  }

  if(!sdr_set_center_freq(&sdr, 89000000)) {
    return(-1);
  }

  if(!sdr_start(&sdr)) {
    return (-1);
  }

  if(!ft_setup(&ft, &sdr, 1<<14)) {
    return(-1);
  }

  fprintf(stderr, "sizeof(*ft.buf_in)= %ld\n", sizeof(*ft.buf_in));
  fprintf(stderr, "sizeof(*ft.buf_in[0])= %ld\n", sizeof(*ft.buf_in[0]));

  for (int i=0; i<2048; i++) {
    if(!ft_get_input(&ft)) {
      return(-1);
    }

    if(!ft_run_fft(&ft)) {
      return(-1);
    }

    write(STDOUT_FILENO, ft.buf_in, sizeof(*ft.buf_in) * ft.len_fft);
  }

  if (!sdr_stop(&sdr)) {
    return(false);
  }

  if (!sdr_destroy(&sdr)) {
    return(false);
  }

  if(!ft_destroy(&ft)) {
    return(-1);
  }

  fprintf(stderr, "So long and thanks for all the fish!\n");

  return(0);
}
