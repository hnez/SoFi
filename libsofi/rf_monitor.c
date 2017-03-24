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


/* This tool displays a spectrum for
 * the connected sdr devices to
 * help you find out which /dev/swradio?
 * is connected to which antenna */

#define NUM_SDRS (4)
#define SCREEN_WIDTH (128)
#define SCREEN_ROWS (12)

#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "sdr.h"
#include "fft_thread.h"

inline float squared(float x)
{
  return(x*x);
}

int main(__attribute__((unused)) int argc, __attribute__((unused))char **argv)
{
  struct {
    struct sdr sdr;
    struct fft_thread fft;
    char path[128];
    double amplitudes[SCREEN_WIDTH];
  } devices[NUM_SDRS]= {0};

  for (int i=0; i<NUM_SDRS; i++) {
    sprintf(devices[i].path, "/dev/swradio%d", i);

    fprintf(stderr, "Open dev %s\n", devices[i].path);

    if(!sdr_open(&devices[i].sdr, devices[i].path)) {
      return (1);
    }

    if (!sdr_connect_buffers(&devices[i].sdr, 8)) {
      return (1);
    }

    if(!sdr_set_center_freq(&devices[i].sdr, 101*1000*1000)) {
      return(1);
    }

    if(!ft_setup(&devices[i].fft, &devices[i].sdr, NULL,
                 SCREEN_WIDTH, 32, 1, true)) {
      return(1);
    }
  }

  for (int i=0; i<NUM_SDRS; i++) {
    fprintf(stderr, "Start dev %s\n", devices[i].path);

    if(!sdr_start(&devices[i].sdr)) {
      return(1);
    }
  }

  for (int i=0; i<NUM_SDRS; i++) {
    fprintf(stderr, "Speed up dev %s\n", devices[i].path);

    if(!sdr_set_sample_rate(&devices[i].sdr, 2000000)) {
      return(1);
    }
  }

  for (int i=0; i<NUM_SDRS; i++) {
    fprintf(stderr, "Start fft %s\n", devices[i].path);

    if(!ft_start(&devices[i].fft)) {
      return(1);
    }
  }

  for(uint64_t frame=0;; frame++) {
    for (int i=0; i<NUM_SDRS; i++) {
      struct fft_buffer *fbuf= NULL;

      fbuf= ft_get_frame(&devices[i].fft, frame);
      if(!fbuf) {
        return(1);
      }

      for(size_t pos=0; pos<SCREEN_WIDTH; pos++) {
        double aold= devices[i].amplitudes[pos];
        double anew=
          squared(fbuf->out[pos][0]) + squared(fbuf->out[pos][1]);

        devices[i].amplitudes[pos]= (8191*aold + anew)/8192;
      }

      if(!ft_release_frame(&devices[i].fft, fbuf)) {
        return(1);
      }
    }

    if(!(frame%1024)) {
      double amax= devices[0].amplitudes[0];
      double amin= amax;

      for(int i=0; i<NUM_SDRS; i++) {
        for(size_t pos=0; pos<SCREEN_WIDTH; pos++) {
          double ac= log(devices[i].amplitudes[pos]);

          if(ac<amin) amin=ac;
          if(ac>amax) amax=ac;
        }
      }

      /* Clear the screen and jump
       * to position 0,0 */
      printf("\x1b[2J\x1b[H");

      for(int i=0; i<NUM_SDRS; i++) {
        printf("Device %s:\n", devices[i].path);

        for(int row=0; row<SCREEN_ROWS; row++) {
          double row_pos_dot= (float)(2*row+1)/(2*SCREEN_ROWS);
          double row_pos_hash= (float)row/SCREEN_ROWS;

          double thr_dot= exp(amin*row_pos_dot + amax*(1-row_pos_dot));
          double thr_hash= exp(amin*row_pos_hash + amax*(1-row_pos_hash));

          for(size_t pos=0; pos<SCREEN_WIDTH; pos++) {
            double ac= devices[i].amplitudes[pos];

            fputc((ac >= thr_hash) ? '#' : ((ac >= thr_dot) ? '.' : ' '), stdout);
          }

          fputc('\n', stdout);
        }
      }
    }
  }

  return(0);
}
