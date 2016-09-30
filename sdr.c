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

#include <libv4l2.h>
#include <linux/videodev2.h>

static bool ioctl_irqsafe(int fh, unsigned long int request, void *arg)
{
  int ret;

  do {
    ret = v4l2_ioctl(fh, request, arg);
  } while (ret == -1 && ((errno == EINTR) || (errno == EAGAIN)));

  return(ret >= 0);
}

bool sdr_open(struct sdr *sdr, char *path)
{
  struct v4l2_format fmt;

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

  // Set the desired sample format to unsigned 8 bit
  memset(&fmt, 0, sizeof(fmt));
  fmt.type= V4L2_BUF_TYPE_SDR_CAPTURE;
  fmt.fmt.sdr.pixelformat= V4L2_PIX_FMT_SDR_U8;

  if (!ioctl_irqsafe(sdr->fd, VIDIOC_S_FMT, &fmt)) {
    fprintf(stderr, "sdr_open: ioctl failed %d, %s\n", errno, strerror(errno));
    return (false);
  }

  if (fmt.fmt.sdr.pixelformat != V4L2_PIX_FMT_SDR_U8) {
    fprintf(stderr,
            "sdr_open: could not get desired pixel format "
            "ioctl returned format %c%c%c%c\n",
            (fmt.fmt.sdr.pixelformat >> 0) & 0xff,
            (fmt.fmt.sdr.pixelformat >> 8) & 0xff,
            (fmt.fmt.sdr.pixelformat >> 16) & 0xff,
            (fmt.fmt.sdr.pixelformat >> 24) & 0xff);

    return (false);
  }

  return (true);
}

bool sdr_connect_buffers(struct sdr *sdr, uint32_t bufs_count)
{
  struct v4l2_requestbuffers req;
  struct v4l2_buffer buf;

  if (!sdr || sdr->fd < 0 || sdr->buffers) {
    fprintf(stderr, "sdr_connect_buffers: No sdr structure, device not open "
            "or buffers already set\n");

    return (false);
  }

  memset(&req, 0, sizeof(req));
  req.count= bufs_count;
  req.type= V4L2_BUF_TYPE_SDR_CAPTURE;
  req.memory= V4L2_MEMORY_MMAP;

  // Ask the v4l kernel code to prepare bufs_count buffers for us
  if (!ioctl_irqsafe(sdr->fd, VIDIOC_REQBUFS, &req)) {
    fprintf(stderr, "sdr_connect_buffers: ioctl failed %d, %s\n", errno, strerror(errno));
    return (false);
  }

  sdr->buffers= calloc(bufs_count, sizeof(*sdr->buffers));
  if (!sdr->buffers) {
    fprintf(stderr, "sdr_connect_buffers: buffer list allocation failed\n");
    return (false);
  }

  // Retreive the bufs_count buffers from the kernel
  for (uint32_t bidx=0; bidx < req.count; bidx++) {
    memset(&buf, 0, sizeof(buf));
    buf.type= V4L2_BUF_TYPE_SDR_CAPTURE;
    buf.memory= V4L2_MEMORY_MMAP;
    buf.index= bidx;

    if (!ioctl_irqsafe(sdr->fd, VIDIOC_QUERYBUF, &buf)) {
      fprintf(stderr, "sdr_connect_buffers: ioctl failed %d, %s\n", errno, strerror(errno));
      return (false);
    }

    sdr->buffers[bidx].len= buf.length;
    sdr->buffers[bidx].start=
      v4l2_mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, sdr->fd, buf.m.offset);

    if (sdr->buffers[bidx].start == MAP_FAILED) {
      fprintf(stderr, "sdr_connect_buffers: mmap failed\n");
      return (false);
    }
  }

  // Queue the received buffers as available
  for (uint32_t bidx=0; bidx < req.count; bidx++) {
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_SDR_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = bidx;

    if (!ioctl_irqsafe(sdr->fd, VIDIOC_QBUF, &buf)) {
      fprintf(stderr, "sdr_connect_buffers: ioctl failed %d, %s\n", errno, strerror(errno));
      return (false);
    }
  }

  sdr->bufs_count= bufs_count;

  return (true);
}

bool sdr_start(struct sdr *sdr)
{
  enum v4l2_buf_type type;

  if (!sdr || sdr->fd < 0) {
    fprintf(stderr, "sdr_start: missing sdr struct or fd is closed\n");
    return(false);
  }

  type= V4L2_BUF_TYPE_SDR_CAPTURE;

  if (!ioctl_irqsafe(sdr->fd, VIDIOC_STREAMON, &type)) {
    fprintf(stderr, "sdr_start: ioctl failed %d, %s\n", errno, strerror(errno));
    return (false);
  }

  return (true);
}

bool sdr_set_sample_rate(struct sdr *sdr, uint32_t samp_rate)
{
  if (!sdr || sdr->fd < 0) {
    fprintf(stderr, "sdr_set_sample_rate: missing sdr struct or fd is closed\n");
    return(false);
  }

  struct v4l2_frequency frequency= {0};
  frequency.tuner = 0;
  frequency.type = V4L2_TUNER_ADC;
  frequency.frequency = samp_rate;

  if (!ioctl_irqsafe(sdr->fd, VIDIOC_S_FREQUENCY, &frequency)) {
    fprintf(stderr, "sdr_set_sample_rate: ioctl failed %d, %s\n",
            errno, strerror(errno));

    return(false);
  }

  return(true);
}

bool sdr_set_center_freq(struct sdr *sdr, uint32_t freq)
{
  if (!sdr || sdr->fd < 0) {
    fprintf(stderr, "sdr_set_center_freq: missing sdr struct or fd is closed\n");
    return(false);
  }

  struct v4l2_frequency frequency= {0};
  frequency.tuner = 1;
  frequency.type = V4L2_TUNER_RF;
  frequency.frequency = freq;

  if (!ioctl_irqsafe(sdr->fd, VIDIOC_S_FREQUENCY, &frequency)) {
    fprintf(stderr, "sdr_set_center_freq: ioctl failed %d, %s\n",
            errno, strerror(errno));

    return(false);
  }

  return(true);
}

bool sdr_stop(struct sdr *sdr)
{
  enum v4l2_buf_type type;

  if (!sdr || sdr->fd < 0) {
    fprintf(stderr, "sdr_stop: missing sdr struct or fd is closed\n");
    return(false);
  }

  type= V4L2_BUF_TYPE_SDR_CAPTURE;

  if (!ioctl_irqsafe(sdr->fd, VIDIOC_STREAMOFF, &type)) {
    fprintf(stderr, "sdr_stop: ioctl failed %d, %s\n", errno, strerror(errno));
    return (false);
  }

  return (true);
}

bool sdr_destroy(struct sdr *sdr)
{
  if (!sdr || sdr->fd < 0 || (sdr->bufs_count && !sdr->buffers)) {
    fprintf(stderr, "sdr_close: missing sdr struct or fd is closed\n");
    return(false);
  }

  for (uint32_t bidx= 0; bidx < sdr->bufs_count; bidx++) {
    v4l2_munmap(sdr->buffers[bidx].start, sdr->buffers[bidx].len);
  }

  if (sdr->buffers) {
    free(sdr->buffers);
  }

  v4l2_close(sdr->fd);
  free(sdr->dev_path);

  return(false);
}

/**
 * Get a pointer to the next samples.
 *
 * @param sdr pointer to a opened sdr structure
 * @param len the desired length to read
 * @param samples. A pointer to the samples will be written here
 * @return the nuber of bytes that may be read from the sample buffer or -1 on error
 */
ssize_t sdr_peek(struct sdr *sdr, size_t len, void **samples)
{
  if (!sdr || sdr->fd < 0 || !sdr->buffers) {
    fprintf(stderr, "sdr_peek: missing sdr struct or fd is closed\n");
    return(-1);
  }

  // check if there is already a buffer being read from
  if (!sdr->buffer_reader.opened) {
    struct v4l2_buffer buf;

    memset(&buf, 0, sizeof(buf));
    buf.type= V4L2_BUF_TYPE_SDR_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if(!ioctl_irqsafe(sdr->fd, VIDIOC_DQBUF, &buf)) {
      fprintf(stderr, "sdr_peek: ioctl failed %d, %s\n", errno, strerror(errno));
      return(-1);
    }

    sdr->buffer_reader.opened= true;

    sdr->buffer_reader.bufnum= buf.index;
    sdr->buffer_reader.rdpos= 0;
    sdr->buffer_reader.peekpos= 0;
  }

  uint32_t bufnum= sdr->buffer_reader.bufnum;
  uint32_t rdpos= sdr->buffer_reader.rdpos;

  if (bufnum > sdr->bufs_count) {
    fprintf(stderr,
            "sdr_peek: illegal bufnum index (%d/%d)\n",
            bufnum, sdr->bufs_count);

    return (-1);
  }

  if (rdpos > sdr->buffers[bufnum].len) {
    fprintf(stderr, "sdr_peek: buffer empty?\n");

    return (-1);
  }

  size_t len_rem= sdr->buffers[bufnum].len - rdpos;
  size_t len_trunc= len_rem < len ? len_rem : len;

  if(samples) *samples= (uint8_t *)sdr->buffers[bufnum].start + rdpos;
  sdr->buffer_reader.peekpos+= len_trunc;

  return(len_trunc);
}

/**
 * Mark the last peeked samples as done.
 * The pointer returned by peek is no longer valid after marking it done.
 */
bool sdr_done(struct sdr *sdr)
{
  if (!sdr || sdr->fd < 0 || !sdr->buffers) {
    fprintf(stderr, "sdr_peek: missing sdr struct or buffers\n");
    return(-1);
  }

  if (!sdr->buffer_reader.opened) {
    fprintf(stderr, "sdr_peek: buffer_reader not open. Peek was not called before\n");
    return(-1);
  }

  sdr->buffer_reader.rdpos= sdr->buffer_reader.peekpos;

  size_t rdpos= sdr->buffer_reader.rdpos;
  uint32_t bufnum= sdr->buffer_reader.bufnum;

  if (rdpos > sdr->buffers[bufnum].len) {
    fprintf(stderr, "sdr_done: rdpos > buffer_len\n");
    return (false);
  }

  // Check if buffer is empty
  if (rdpos == sdr->buffers[bufnum].len) {
    struct v4l2_buffer buf;

    // Requeue this buffer
    memset(&buf, 0, sizeof(buf));
    buf.type= V4L2_BUF_TYPE_SDR_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = bufnum;

    if (!ioctl_irqsafe(sdr->fd, VIDIOC_QBUF, &buf)) {
      fprintf(stderr, "sdr_done: ioctl failed %d, %s\n", errno, strerror(errno));
      return (false);
    }

    /* Mark the reader as closed an make
     * accidential accesses break horribly */
    sdr->buffer_reader.opened= false;
    sdr->buffer_reader.bufnum= (uint32_t) -1;
  }

  return(true);
}
