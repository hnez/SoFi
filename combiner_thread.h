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

#include "polar_thread.h"

#define CT_WEIGHT_OLD (511)
#define CT_WEIGHT_CUR (1)
#define CT_WEIGHT_BOTH (CT_WEIGHT_OLD + CT_WEIGHT_CUR)

struct combiner_thread {
  struct polar_thread *polar_a;
  struct polar_thread *polar_b;

  uint32_t len_fft;

  float *phases;
  float *variances;
  float *mag_sq;
};
