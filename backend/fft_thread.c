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

#include <pthread.h>

#include <fftw3.h>

#include "fft_thread.h"

#include "sdr.h"


static bool ft_load_samples(struct fft_thread *ft, struct fft_buffer *buf)
{
  if (!ft || !ft->dev || !buf) {
    fprintf(stderr, "ft_get_input: No ft, sdr or buf structure\n");

    return (false);
  }

  for(size_t pos=0; pos < ft->len_fft;) {
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
      buf->in[pos][0]= ((float)samples[i].r - 127.5)/127.5;
      buf->in[pos][1]= ((float)samples[i].i - 127.5)/127.5;
    }

    if (!sdr_done(ft->dev)) {
      return(false);
    }
  }

  return(true);
}

static bool ft_calculate_fft(struct fft_buffer *buf)
{
  if (!buf || !buf->plan) {
    fprintf(stderr, "ft_calculate_fft: No buffer or plan\n");

    return (false);
  }

  fftwf_execute(buf->plan);

  return(true);
}

static struct fft_buffer *ft_get_consumed_buffer(struct fft_thread *ft)
{
  pthread_mutex_lock(&ft->buffers_meta_lock);

  for(;;) {
    for (size_t bidx=0; bidx<ft->buffers_count; bidx++) {
      if (ft->buffers[bidx].consumers==0) {
        pthread_mutex_unlock(&ft->buffers_meta_lock);

        return(&ft->buffers[bidx]);
      }
    }

    pthread_cond_wait(&ft->buffers_meta_notify, &ft->buffers_meta_lock);
  }
}

static struct fft_buffer *ft_get_frame_bidx_locked(struct fft_thread *ft, size_t frame)
{
  pthread_mutex_lock(&ft->buffers_meta_lock);

  for(;;) {
    for (size_t bidx=0; bidx<ft->buffers_count; bidx++) {
      if (ft->buffers[bidx].consumers && ft->buffers[bidx].frame_no==frame) {

        return(&ft->buffers[bidx]);
      }
    }

    if(!ft->running) {
      pthread_mutex_unlock(&ft->buffers_meta_lock);

      return(NULL);
    }

    pthread_cond_wait(&ft->buffers_meta_notify, &ft->buffers_meta_lock);
  }
}

static void *ft_main(void *dat)
{
  struct fft_thread *ft= dat;

  for (uint64_t frame= 0; ;frame++) {
    if (!ft->running) {
      return((void *)true);
    }

    struct fft_buffer *buf= ft_get_consumed_buffer(ft);

    if (!ft_load_samples(ft, buf)) {
      return((void *)false);
    }

    if (!ft_calculate_fft(buf)) {
      return((void *)false);
    }

    pthread_mutex_lock(&ft->buffers_meta_lock);

    buf->consumers= ft->consumers;
    buf->frame_no= frame;

    pthread_cond_broadcast(&ft->buffers_meta_notify);
    pthread_mutex_unlock(&ft->buffers_meta_lock);
  }
}

bool ft_setup(struct fft_thread *ft, struct sdr *dev, size_t len_fft,
              size_t buffers_count, uint64_t consumers_count)
{
  if (!ft || !dev) {
    fprintf(stderr, "ft_setup: No ft or sdr structure\n");

    return (false);
  }

  ft->dev= dev;
  ft->len_fft= len_fft;
  ft->consumers= consumers_count;
  ft->running= false;

  ft->buffers_count= buffers_count;
  pthread_mutex_init(&ft->buffers_meta_lock, NULL);
  pthread_cond_init(&ft->buffers_meta_notify, NULL);

  ft->buffers= calloc(buffers_count, sizeof(*ft->buffers));

  if (!ft->buffers) {
    fprintf(stderr, "ft_setup: Allocating %ld buffer slots failed\n", buffers_count);

    return (false);
  }

  for (size_t bidx=0; bidx<buffers_count; bidx++) {
    ft->buffers[bidx].in=  fftwf_alloc_complex(len_fft);
    ft->buffers[bidx].out= fftwf_alloc_complex(len_fft);

    if (!ft->buffers[bidx].in || !ft->buffers[bidx].out) {
      fprintf(stderr, "ft_setup: allocating input/output buffers of length %ld failed\n", len_fft);

      return(false);
    }

    ft->buffers[bidx].consumers= 0;
    ft->buffers[bidx].frame_no= 0;

    ft->buffers[bidx].plan= fftwf_plan_dft_1d(len_fft,
                                              ft->buffers[bidx].in, ft->buffers[bidx].out,
                                              FFTW_FORWARD, FFTW_MEASURE);

    if (!ft->buffers[bidx].plan) {
      fprintf(stderr, "ft_setup: fftwf_plan failed\n");
      return(false);
    }
  }

  return(true);
}

bool ft_start(struct fft_thread *ft)
{
  if (!ft) {
    fprintf(stderr, "ft_start: No ft structure\n");

    return(false);
  }

  if (ft->running) {
    return(true);
  }

  int stat= pthread_create(&ft->thread, NULL, &ft_main, ft);

  if (stat != 0) {
    fprintf(stderr, "ft_start: pthread_create failed\n");

    return(false);
  }

  ft->running= true;

  return(true);
}

bool ft_stop(struct fft_thread *ft)
{
  if (!ft) {
    fprintf(stderr, "ft_stop: No ft structure\n");

    return(false);
  }

  if (!ft->running) {
    return (true);
  }

  ft->running= false;
  bool status= false;

  if (pthread_join(ft->thread, (void **)&status) != 0) {
    fprintf(stderr, "ft_stop: Could not join thread\n");

    return(false);
  }

  return(status);
}

struct fft_buffer *ft_get_frame(struct fft_thread *ft, uint64_t frame)
{
  struct fft_buffer *buf= ft_get_frame_bidx_locked(ft, frame);

  pthread_mutex_unlock(&ft->buffers_meta_lock);

  return(buf);
}

bool ft_release_frame(struct fft_thread *ft, struct fft_buffer *buf)
{
  pthread_mutex_lock(&ft->buffers_meta_lock);

  buf->consumers--;

  pthread_cond_broadcast(&ft->buffers_meta_notify);
  pthread_mutex_unlock(&ft->buffers_meta_lock);

  return(true);
}

bool ft_destroy(struct fft_thread *ft)
{
  if (!ft) {
    fprintf(stderr, "ft_destroy: No ft structure\n");

    return (false);
  }

  if(!ft_stop(ft)) {
    fprintf(stderr, "ft_destroy: stopping the thread failed\n");

    return (false);
  }

  pthread_mutex_lock(&ft->buffers_meta_lock);

  for(size_t bidx=0; bidx<ft->buffers_count; bidx++) {
    if(ft->buffers[bidx].consumers) {
      fprintf(stderr,
              "ft_destroy: there is still a waiting consumer on frame %ld\n",
              ft->buffers[bidx].frame_no);

      return (false);
    }

    fftwf_destroy_plan(ft->buffers[bidx].plan);

    fftwf_free(ft->buffers[bidx].in);
    fftwf_free(ft->buffers[bidx].out);
  }

  free(ft->buffers);

  pthread_mutex_unlock(&ft->buffers_meta_lock);

  return (true);
}
