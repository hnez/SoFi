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
#include <unistd.h>
#include <string.h>

#include <math.h>
#include <volk/volk.h>

#include "combiner.h"

#include "fft_thread.h"

static uint64_t factorial(uint64_t x)
{
  uint64_t pivot=1;

  for(uint64_t i=1; i<=x; i++) pivot*=i;

  return(pivot);
}

bool cb_init(struct combiner *cb, struct fft_thread *ffts, size_t num_ffts)
{
  if (!cb || !ffts || !num_ffts) {
    fprintf(stderr, "cb_init: NULL as input\n");
    return (false);
  }

  cb->num_edges= factorial(num_ffts - 1);
  cb->num_ffts= num_ffts;
  cb->len_fft= ffts[0].len_fft;

  cb->frame_no= 0;

  for (size_t i=0; i<num_ffts; i++) {
    if (ffts[i].len_fft != cb->len_fft) {
      fprintf(stderr, "cb_init: fft lengths do not match\n");
      return(false);
    }
  }

  cb->tmp_cplx= fftwf_alloc_complex(cb->len_fft);
  cb->tmp_real= fftwf_alloc_real(cb->len_fft);

  if(!cb->tmp_cplx || !cb->tmp_real) {
    fprintf(stderr, "cb_init: allocating temp buffers failed\n");

    return(false);
  }

  cb->inputs= calloc(num_ffts, sizeof(*cb->inputs));
  cb->outputs= calloc(num_ffts, sizeof(*cb->outputs));

  if(!cb->inputs || !cb->outputs) {
    fprintf(stderr, "cb_init: allocating i/o buffers failed\n");

    return(false);
  }

  for(size_t fi=0; fi<num_ffts; fi++) {
    cb->inputs[fi].thread= &ffts[fi];
    cb->inputs[fi].buffer= NULL;
  }

  for(size_t ina=0, i=0; ina<num_ffts; ina++) {
    for(size_t inb=ina+1; inb<num_ffts; inb++, i++) {
      fprintf(stderr, "cb_run: edge %ld: %ld <-> %ld\n", i, ina, inb);

      cb->outputs[i].input_a= ina;
      cb->outputs[i].input_b= inb;

      cb->outputs[i].acc= fftwf_alloc_complex(cb->len_fft);

      if(!cb->outputs[i].acc) {
        fprintf(stderr, "cb_run: allocating mean buffer failed\n");

        return(false);
      }

      memset(cb->outputs[i].acc, 0, sizeof(*cb->outputs[i].acc) * cb->len_fft);
    }
  }

  return(true);
}

bool cb_step(struct combiner *cb, float *mag_dst, float **phase_dsts)
{
  do {
    for(size_t fi=0; fi<cb->num_ffts; fi++) {
      cb->inputs[fi].buffer= ft_get_frame(cb->inputs[fi].thread, cb->frame_no);

      if(!cb->inputs[fi].buffer) {
        fprintf(stderr, "cb_step: Getting frame from fft_thread failed\n");

        return(false);
      }
    }

    for(size_t ei=0; ei<cb->num_edges; ei++) {
      size_t ina= cb->outputs[ei].input_a;
      size_t inb= cb->outputs[ei].input_b;

      /* Calculate Phase difference between
       * the two inputs for all frequencies */
      volk_32fc_x2_multiply_conjugate_32fc(cb->tmp_cplx,
                                           cb->inputs[ina].buffer->out,
                                           cb->inputs[inb].buffer->out,
                                           cb->len_fft);

      volk_32f_x2_add_32f((float *)cb->outputs[ei].acc,
                          (float *)cb->outputs[ei].acc,
                          (float *)cb->tmp_cplx,
                          2*cb->len_fft);
    }

    for(size_t fi=0; fi<cb->num_ffts; fi++) {
      if(!ft_release_frame(cb->inputs[fi].thread, cb->inputs[fi].buffer)) {
        fprintf(stderr, "cb_step: Relasing fft frame failed\n");

        return(false);
      }
    }

    cb->frame_no++;
  } while(cb->frame_no % CB_DECIMATOR);

  memset(mag_dst, 0, sizeof(*mag_dst) * cb->len_fft);

  for(size_t ei=0; ei<cb->num_edges; ei++) {
    /* Calculate and accumulate magnitudes squared */
    volk_32fc_magnitude_squared_32f(cb->tmp_real,
                                    cb->outputs[ei].acc,
                                    cb->len_fft);

    volk_32f_x2_add_32f(mag_dst, mag_dst,
                        cb->tmp_real, cb->len_fft);

    /* Calculate and output phase differences */
    volk_32fc_s32f_atan2_32f(phase_dsts[ei],
                             cb->outputs[ei].acc,
                             1.0,
                             cb->len_fft);

    /* Reset accumulators */
    memset(cb->outputs[ei].acc, 0,
           sizeof(*cb->outputs[ei].acc) * cb->len_fft);
  }

  volk_32f_s32f_normalize(mag_dst,
                          1.0/(CB_DECIMATOR * cb->num_edges),
                          cb->len_fft);

  return(true);
}

bool cb_cleanup(struct combiner *cb)
{
  fftwf_free(cb->tmp_cplx);
  fftwf_free(cb->tmp_real);

  free(cb->inputs);

  for(size_t ei=0; ei<cb->num_edges; ei++) {
    fftwf_free(cb->outputs[ei].acc);
  }

  free(cb->outputs);

  return(true);
}
