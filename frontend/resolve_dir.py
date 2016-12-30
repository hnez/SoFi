#!/usr/bin/env python3

from array import array
from simplex_optim import SimplexOptim

import itertools as it
import numpy as np
import math
import sys

def normal_dist(stddev):
    scale_f= stddev * math.sqrt(2*math.pi)

    x= np.arange(3 * stddev)
    x_sq= -(x**2)/(2*(stddev**2))

    # Only calculate one half of the symetric function
    top= np.exp(x_sq) / scale_f

    # patch together the top and bottom half
    full= np.concatenate((top[:0:-1], top))

    return(full)

def prop_paint(centers, stddevs, width):
    # Allocate twice the canvas size to make wrapping easier
    # The halves are combined in the end
    db_canvas= np.ones(2*width)

    spread= width/(2*math.pi)

    for (ocenter, ostddev) in zip(centers, stddevs):
        center= int((ocenter + math.pi) * spread + 0.5)
        stddev= ostddev * spread

        dist= normal_dist(stddev) + 1.0

        sstart= center - len(dist)//2
        start= sstart if sstart >= 0 else sstart + width
        end= start + len(dist)

        db_canvas[start:end]*= dist

    # Combine the double canvas halves, subtract the
    # multiplication saving 1.
    # Normalize the canvas values by ca. len(center)
    canvas= db_canvas[:width] * db_canvas[width:] - 1
    canvas/= canvas.sum()

    return(canvas)

def remainder(x1, x2=2*math.pi):
    return(x1 - x2*np.round(x1 / x2))

class AntArrayEdge(object):
    def __init__(self, pos_ax, pos_ay, pos_bx, pos_by):
        self.pos_a= np.array([pos_ax, pos_ay], dtype=float)
        self.pos_b= np.array([pos_bx, pos_by], dtype=float)

        edge= self.pos_b - self.pos_a

        self.dist= np.linalg.norm(edge)
        self.dirc= math.atan2(edge[0], edge[1])

    def set_wavelengths(self, wavelengths):
        self.rel_wl= wavelengths / self.dist

    def get_directions(self, phases, variances):
        rel_wl= self.rel_wl
        rel_len= remainder(phases * rel_wl, 1.0)

        rel_dirs= np.arccos(rel_len)

        # FIXME: the 1.01 should be 1.0 but shit breaks
        # loose when rel_len containes 1.0
        var_dirs= (rel_wl**2 * variances) / (1.01 - rel_len)

        left= (self.dirc + rel_dirs, var_dirs)
        right= (self.dirc - rel_dirs, var_dirs)

        return((left, right))

class AntArray(object):
    speed_of_light= 299792458.0

    def __init__(self, edges, fft_len, fq_low, fq_high):
        self.edges= edges

        self.fft_len= fft_len

        self.frequencies= np.linspace(fq_low, fq_high, fft_len)
        self.wavelengths= self.speed_of_light/self.frequencies

        for edge in self.edges:
            edge.set_wavelengths(self.wavelengths)

        self.init_spx_optim()

    def init_spx_optim(self):
        # FIXME: remove constants
        limits_samp_off= np.array([math.pi] * 3)
        limits_ph_off= np.array([math.pi] *6)

        limits_params= np.concatenate((limits_samp_off, limits_ph_off))

        self.spx_opt= SimplexOptim(-limits_params, limits_params)

    def read_samples(self, fd):
        raw_sample= fd.read(3 * 4 * self.fft_len)
        np_sample= np.frombuffer(raw_sample, np.float32)

        if len(np_sample) != 3*self.fft_len:
            raise(Exception())

        phases= np_sample[:self.fft_len]
        variances= np_sample[self.fft_len:self.fft_len*2]
        mag_sqs= np_sample[self.fft_len*2:]

        return((phases, variances, mag_sqs))

    def file_step(self, fd):
        samples= list(self.read_samples(fd) for edge in self.edges)

        (orig_phases, variances, mag_sqs)= zip(*samples)

        def parse_spx_params(parameters):
            ant_samp_offs= [0] + list(parameters[:3])
            ph_offs= parameters[3:]

            edge_samp_offs= list((a - b) for (a,b) in it.combinations(ant_samp_offs, 2))

            return((ph_offs, edge_samp_offs))

        def corrected_phase(ph, pho, sao):
            return(remainder(ph + np.linspace(-sao, sao, len(ph)) + pho))

        def test_parameters(parameters):
            (ph_offs, edge_samp_offs)= parse_spx_params(parameters)

            phases= list(
                corrected_phase(ph, pho, sao)
                for (ph, pho, sao) in zip(orig_phases, ph_offs, edge_samp_offs)
            )

            steepness= sum(
                abs(remainder(ph[128:] - ph[:-128]).mean())
                for ph in phases
            )

            dc_off= sum(abs(ph[910:920].mean()) for ph in phases) / 5
            
            return (-steepness-dc_off)

        parameters= self.spx_opt.optimize_hop(test_parameters)
        (ph_offs, edge_samp_offs)= parse_spx_params(parameters)

        phases= list(
            corrected_phase(ph, pho, sao)
            for (ph, pho, sao) in zip(orig_phases, ph_offs, edge_samp_offs)
        )


        #for ph in orig_phases:
        #    array('f', ph).tofile(sys.stdout.buffer)

        for ph in phases:
            array('f', ph).tofile(sys.stdout.buffer)


antennas= [
    (-0.2, -0.2), (-0.2,  0.2),
    ( 0.2,  0.2), ( 0.2, -0.2)
]

edges= list(AntArrayEdge(*aa, *ab) for (aa, ab) in it.combinations(antennas, 2))

antarr= AntArray(edges, 2048, 90.5e6, 92.5e6)

while True:
    antarr.file_step(sys.stdin.buffer)
