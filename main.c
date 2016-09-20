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
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <fftw.h>

#include "sdr.h"

int main(__attribute__((unused)) int argc, __attribute__((unused))char **argv)
{
  struct sdr dev= {.dev_path= "/dev/swradio0"};

  if(!sdr_open(&dev)) {
    return (-1);
  }

  if (!sdr_connect_buffers(&dev, 8)) {
    return (-1);
  }

  for (uint32_t i=0; i<dev.bufs_count; i++) {
    printf("start: %p, len: %ld\n", dev.buffers[i].start, dev.buffers[i].len);
  }

  if(!sdr_start(&dev)) {
    return (-1);
  }

  if(!sdr_stop(&dev)) {
    return (-1);
  }

  if(!sdr_close(&dev)) {
    return (-1);
  }
}
