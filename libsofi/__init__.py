# Copyright 2017 Leonard Göhrs <leonard@goehrs.eu>
#
# This is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3, or (at your option)
# any later version.
#
# This software is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this software; see the file COPYING.  If not, write to
# the Free Software Foundation, Inc., 51 Franklin Street,
# Boston, MA 02110-1301, USA.

import os

import numpy as np
import ctypes as ct

moddir= os.path.dirname(__file__)
_libsofi= np.ctypeslib.load_library('libsofi', moddir)

class Sofi(object):
    def __init__(self):
        self._sofi_new= _libsofi.sofi_new
        self._sofi_new.argtypes= []
        self._sofi_new.restype= ct.c_void_p

        self._raw= self._sofi_new()

        if self._raw is None:
            raise Exception('Opening Sofi instance failed')

        self._sofi_get_nsdrs= _libsofi.sofi_get_nsdrs
        self._sofi_get_nsdrs.argtypes= [ct.c_void_p]
        self._sofi_get_nsdrs.restype= ct.c_uint64

        self.num_sdrs= self._sofi_get_nsdrs(self._raw)


        self._sofi_get_fftlen= _libsofi.sofi_get_fftlen
        self._sofi_get_fftlen.argtypes= [ct.c_void_p]
        self._sofi_get_fftlen.restype= ct.c_uint64

        self.fft_len= self._sofi_get_fftlen(self._raw)

        self.real_type= ct.POINTER(ct.c_float)

        print('{} SDRs, {} fft bins'.format(self.num_sdrs, self.fft_len))


        self._sofi_read= _libsofi.sofi_read
        self._sofi_read.argtypes= [
            ct.c_void_p, self.real_type, self.real_type * 6
        ]
        self._sofi_read.restype= ct.c_bool

        self._sofi_destroy= _libsofi.sofi_destroy
        self._sofi_destroy.argtypes= [ct.c_void_p]
        self._sofi_destroy.restype= ct.c_bool

        self._sofi_alloc_real= _libsofi.sofi_alloc_real
        self._sofi_alloc_real.argtypes= []
        self._sofi_alloc_real.restype= self.real_type

        self.mag_buf= self._sofi_alloc_real()
        self.phase_bufs= list(
            self._sofi_alloc_real()
            for i in range(6)
        )

    def __del__(self):
        self._sofi_destroy(self._raw)

    def __iter__(self):
        return self

    def __next__(self):
        phase_pointers= (self.real_type * 6)(*self.phase_bufs)

        self._sofi_read(
            self._raw, self.mag_buf, phase_pointers
        )

        np_mag= np.ctypeslib.as_array(self.mag_buf, (self.fft_len, ))

        np_phase= tuple(
            np.ctypeslib.as_array(phb, (self.fft_len, ))
            for phb in self.phase_bufs
        )

        return(np_mag, np_mag)
