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

#include "sdr.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

bool sdr_open(struct sdr *sdr, char *path)
{
  if (!sdr || !path) {
    fprintf(stderr, "sdr_open: No sdr structure or device path\n");
    return (false);
  }

  sdr->dev_path= strdup(path);
  if (!sdr->dev_path) {
    fprintf(stderr, "sdr_open: string allocation failed\n");
    return (false);
  }

  sdr->fd= open(sdr->dev_path, O_RDWR);
  if (sdr->fd < 0) {
    fprintf(stderr, "sdr_open: open(%s) failed (%s)\n",
            sdr->dev_path, strerror(errno));

    return (false);
  }

  fprintf(stderr, "sdr_open: File %s is used for simulation\n", sdr->dev_path);

  return (true);
}

bool sdr_connect_buffers(struct sdr *sdr, uint32_t bufs_count)
{
  if (!sdr || !bufs_count || sdr->buffers) {
    fprintf(stderr, "sdr_connect_buffers: No sdr structure or bufs_count is zero\n");
    return (false);
  }

  sdr->buffers= calloc(1, sizeof(*sdr->buffers));
  if (!sdr->dev_path) {
    fprintf(stderr, "sdr_connect_buffers: Meta Buffer allocation failed\n");
    return (false);
  }

  sdr->buffers[0].start= calloc(1, 65536);
  if(!sdr->buffers[0].start) {
    fprintf(stderr, "sdr_connect_buffers: Buffer allocation failed\n");
    return (false);
  }

  sdr->buffers[0].len= 65536;
  sdr->bufs_count= 1;

  return(true);
}

bool sdr_start(struct sdr *sdr)
{
  return(sdr != NULL);
}

bool sdr_set_sample_rate(struct sdr *sdr, uint32_t samp_rate)
{
  return(sdr != NULL && samp_rate);
}

bool sdr_set_center_freq(struct sdr *sdr, uint32_t freq)
{
  return(sdr != NULL && freq);
}

bool sdr_stop(struct sdr *sdr)
{
  return(sdr != NULL);
}

bool sdr_destroy(struct sdr *sdr)
{
  if (!sdr || sdr->fd < 0) {
    fprintf(stderr, "sdr_close: missing sdr struct or fd is closed\n");
    return(false);
  }

  close(sdr->fd);
  free(sdr->dev_path);

  free(sdr->buffers->start);
  free(sdr->buffers);

  return(true);
}

ssize_t sdr_peek(struct sdr *sdr, size_t len, void **samples)
{
  if (!sdr || sdr->fd < 0 || !sdr->buffers || !samples) {
    fprintf(stderr, "sdr_peek: missing sdr struct or fd is closed\n");
    return(-1);
  }

  size_t alen= (len > sdr->buffers[0].len) ? sdr->buffers[0].len : len;

  ssize_t res= read(sdr->fd, sdr->buffers[0].start, alen);

  if(res > 0) {
    *samples= sdr->buffers[0].start;
    return(res);
  }
  else {
    return(-1);
  }
}

bool sdr_done(struct sdr *sdr)
{
  return(sdr != NULL);
}

bool sdr_seek(struct sdr *sdr, size_t len)
{
  if (!sdr || sdr->fd < 0 || !sdr->buffers) {
    fprintf(stderr, "sdr_seek: missing sdr struct or fd is closed\n");
    return(false);
  }

  if(lseek(sdr->fd, len, SEEK_CUR) < 0) {
    return(false);
  }

  return(true);
}
