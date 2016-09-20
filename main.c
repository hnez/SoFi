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

#include <libv4l2.h>
#include <linux/videodev2.h>

#define V4L2_PIX_FMT_SDR_U8     v4l2_fourcc('D', 'U', '0', '8')
#define V4L2_PIX_FMT_SDR_U16LE  v4l2_fourcc('D', 'U', '1', '6')

struct sdr {
  char *dev_path;
  int fd;

  struct {
    size_t len;
    void *start;
  } *buffers;
  int bufs_count;
};

static bool ioctl_irqsafe(int fh, unsigned long int request, void *arg)
{
  int ret;

  do {
    ret = v4l2_ioctl(fh, request, arg);
  } while (ret == -1 && ((errno == EINTR) || (errno == EAGAIN)));

  return(ret >= 0);
}

bool sdr_open(struct sdr *sdr)
{
  struct v4l2_format fmt;

  if (!sdr || !sdr->dev_path) {
    fprintf(stderr, "sdr_open: No sdr structure or device\n");
    return (false);
  }

  sdr->fd= open(sdr->dev_path, O_RDWR, 0);
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
    fprintf(stderr, "sdr_open: could not get desired pixel format\n");
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

  return (true);
}

bool sdr_start(struct sdr *sdr)
{
  enum v4l2_buf_type type;

  type= V4L2_BUF_TYPE_SDR_CAPTURE;

  if (!ioctl_irqsafe(sdr->fd, VIDIOC_STREAMON, &type)) {
    fprintf(stderr, "sdr_start: ioctl failed %d, %s\n", errno, strerror(errno));
    return (false);
  }

  return (true);
}


bool sdr_stop(struct sdr *sdr)
{
  enum v4l2_buf_type type;

  type= V4L2_BUF_TYPE_SDR_CAPTURE;

  if (!ioctl_irqsafe(sdr->fd, VIDIOC_STREAMOFF, &type)) {
    fprintf(stderr, "sdr_stop: ioctl failed %d, %s\n", errno, strerror(errno));
    return (false);
  }

  return (true);
}

int main(__attribute__((unused)) int argc, __attribute__((unused))char **argv)
{
}
