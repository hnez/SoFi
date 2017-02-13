#!/usr/bin/env python

'''
 Having to use python and not python3 in 2017
 makes me want to cry.
 Get your shit together GNURadio.
'''

import math
import numpy as np
from random import randint
from gnuradio import gr, analog, channels, blocks
from gnuradio import filter as grfilter

def noise_seed():
    return(randint(0, 0xffffffff))

SPEED_OF_LIGHT= 299792458.0

class Sender(gr.hier_block2):
    def __init__(self, sample_rate, frequency, amplitude):
        gr.hier_block2.__init__(
            self,
            "Sender Simulator",
            gr.io_signature(0, 0, 0),
            gr.io_signature(1, 1, gr.sizeof_gr_complex)
        )

        signal= analog.fastnoise_source_c(
            analog.GR_GAUSSIAN, 1, noise_seed(), 8192
        )

        lpfilter= grfilter.fir_filter_ccf(
            1,
            grfilter.firdes.low_pass(1, sample_rate, 30e3, 20e3, grfilter.firdes.WIN_HAMMING, 6.76)
        )

        lo_signal= analog.sig_source_c(
            sample_rate, analog.GR_COS_WAVE, frequency, amplitude, 0
        )

        mixer= blocks.multiply_vcc(1)

        self.connect(signal, lpfilter, (mixer, 0))
        self.connect(lo_signal, (mixer, 1))
        self.connect(mixer, self)

class CommonAWGN(gr.hier_block2):
    def __init__(self, channels, ampl_common, ampl_indie):
        gr.hier_block2.__init__(
            self,
            "Common AWGN Channel",
            gr.io_signature(channels, channels, gr.sizeof_gr_complex),
            gr.io_signature(channels, channels, gr.sizeof_gr_complex)
        )

        common_noise= analog.fastnoise_source_c(
            analog.GR_GAUSSIAN, ampl_common, noise_seed(), 8192
        )

        for i in range(channels):
            adder= blocks.add_vcc(1)

            indie_noise= analog.fastnoise_source_c(
                analog.GR_GAUSSIAN, ampl_indie, noise_seed(), 8192
            )

            self.connect((self, i), (adder, 0))
            self.connect(common_noise, (adder, 1))
            self.connect(indie_noise, (adder, 2))

            self.connect(adder, (self, i))

def AntennaArray(senders, antennas):
    transfer= list(list(0j for s in senders) for a in antennas)

    for (si, (sx, sy, freq, ampl)) in enumerate(senders):
        wl= SPEED_OF_LIGHT / freq

        for (ai, (ax, ay)) in enumerate(antennas):
            dx= sx - ax
            dy= sy - ay

            d= math.sqrt(dx*dx + dy*dy)

            phase= 2 * math.pi * d/wl

            transfer[ai][si]= np.exp(1j * phase)

    return(blocks.multiply_matrix_cc(transfer, gr.TPP_DONT))

class Simulator(gr.hier_block2):
    def __init__(self, sample_rate, center_freq, senders, antennas, noise_common, noise_indie):
        gr.hier_block2.__init__(
            self,
            "Simulatate the received signals",
            gr.io_signature(0, 0, 0),
            gr.io_signature(len(antennas), len(antennas), gr.sizeof_gr_complex)
        )

        send_sims= list(
            Sender(sample_rate, freq - center_freq, 0.5)
            for (sx, sy, freq, ampl) in senders
        )

        ant_array= AntennaArray(senders, antennas)

        for (si, sim) in enumerate(send_sims):
            self.connect(sim, (ant_array, si))

        awgn= CommonAWGN(len(antennas), noise_common, noise_indie)

        for (ai, ant) in enumerate(antennas):
            self.connect((ant_array, ai), (awgn, ai))
            self.connect((awgn, ai), (self, ai))

class Packer(gr.hier_block2):
    def __init__(self, antennas):
        n_ant= len(antennas)

        gr.hier_block2.__init__(
            self,
            "Pack for Backend",
            gr.io_signature(n_ant, n_ant, gr.sizeof_gr_complex),
            gr.io_signature(n_ant, n_ant, gr.sizeof_char)
        )

        for (ai, ant) in enumerate(antennas):
            agc= analog.agc_cc(1e-6, 100.0, 100.0)
            self.connect((self, ai), agc)

            to_real= blocks.complex_to_real(1)
            to_cplx= blocks.complex_to_imag(1)

            interleaver= blocks.interleave(gr.sizeof_float*1, 1)
            self.connect(agc, to_real, (interleaver, 0))
            self.connect(agc, to_cplx, (interleaver, 1))

            center= blocks.add_const_vff((127, ))
            convert= blocks.float_to_uchar()

            self.connect(interleaver, center, convert, (self, ai))

class TopBlock(gr.top_block):
    def __init__(self, sample_rate, center_freq, senders, antennas, noise_common, noise_indie):
        gr.top_block.__init__(self, "Top Block")

        sim= Simulator(sample_rate, center_freq, senders, antennas, noise_common, noise_indie)
        pack= Packer(antennas)

        for (ai, ant) in enumerate(antennas):
            self.connect((sim, ai), (pack, ai))

            sink= blocks.file_sink(
                gr.sizeof_char,
                'swradio{}'.format(ai),
                False)

            self.connect((pack, ai), sink)
            
senders= (
    ( 10e3,     0,  99.25e6, 0.025),
    (    0,  10e3,  99.50e6, 0.05),
    
    (-10e3,     0,  99.75e6, 0.1),
    (    0, -10e3, 100.00e6, 0.2),
    
    (-10e3, -10e3,  100.25e6, 0.4),
    ( 10e3, -10e3,  100.50e6, 0.9),
)

antennas= (
    ( 0.0,  0.0),
    ( 0.0,  0.75),
    (-0.5, -0.5),
    ( 0.5, -0.5)
)

top= TopBlock(2e6, 100e6, senders, antennas, 0.4, 0.3)

top.run()
