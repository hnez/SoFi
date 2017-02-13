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
            grfilter.firdes.low_pass(1, sample_rate, 10e3, 3e3, grfilter.firdes.WIN_HAMMING, 6.76)
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

class Simulator(gr.top_block):
    def __init__(self, sample_rate, center_freq, senders, antennas, noise_common, noise_indie):
        gr.top_block.__init__(self)

        send_sims= list(
            Sender(sample_rate, freq - center_freq, 0.5)
            for (sx, sy, freq, ampl) in senders
        )

        ant_array= AntennaArray(senders, antennas)

        for (si, sim) in enumerate(send_sims):
            self.connect(sim, (ant_array, si))

        sinks= list(
            blocks.file_sink(gr.sizeof_gr_complex*1, 'antenna_{}.bin'.format(ai), False)
            for (ai, ant) in enumerate(antennas)
        )

        awgn= CommonAWGN(len(sinks), noise_common, noise_indie)

        for (ai, sink) in enumerate(sinks):
            self.connect((ant_array, ai), (awgn, ai))
            self.connect((awgn, ai), sink)

senders= (
    ( 10e3,     0, 100.25e6, 0.9),
    (    0,  10e3,  99.75e6, 0.6),
    (-10e3,     0,  100.5e6, 0.3),
    (    0, -10e3,   99.5e6, 0.4),
)

antennas= (
    ( 0.5,  0.5),
    ( 0.5, -0.5),
    (-0.5, -0.5),
    (-0.5,  0.5)
)

sim= Simulator(2e6, 100e6, senders, antennas, 0.1, 0.01)

sim.run()
