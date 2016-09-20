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

#define V4L2_PIX_FMT_SDR_U8     v4l2_fourcc('C', 'U', '0', '8')
#define V4L2_PIX_FMT_SDR_U16LE  v4l2_fourcc('C', 'U', '1', '6')

struct sdr {
  char *dev_path;
  int fd;

  struct {
    size_t len;
    void *start;
  } *buffers;
  uint32_t bufs_count;

  struct {
    bool opened;

    uint32_t bufnum;
    size_t rdpos;
    size_t peekpos;
  } buffer_reader;
};

bool sdr_open(struct sdr *sdr);
bool sdr_connect_buffers(struct sdr *sdr, uint32_t bufs_count);
bool sdr_start(struct sdr *sdr);
bool sdr_stop(struct sdr *sdr);
bool sdr_close(struct sdr *sdr);
bool sdr_done(struct sdr *sdr);
ssize_t sdr_peek(struct sdr *sdr, size_t len, void **samples);
