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

        if stddev > width//8:
            continue

        dist= normal_dist(stddev) + 1.0

        sstart= center - len(dist)//2
        start= sstart if sstart >= 0 else sstart + width
        end= start + len(dist)

        db_canvas[start:end]*= dist

    # Combine the double canvas halves, subtract the
    # multiplication saving 1.
    # Normalize the canvas values by ca. len(center)
    canvas= db_canvas[:width] * db_canvas[width:] - 1
    canvas/= canvas.sum() or 1

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
        self.rel_wl= (wavelengths / self.dist) / (4*math.pi)

    def integrate_illegal(self, phases, variances):
        orig_dists= phases * self.rel_wl
        dists= np.abs(orig_dists) - self.dist
        np.clip(dists, 0, 100*self.dist, dists)

        res= (dists / variances).mean()

        return(res)

    def get_direction(self, idx, phase, variance):
        rel_len= phase * self.rel_wl[idx]

        if rel_len >= 1 or rel_len<=-1:
            return(None)

        rel_dir= np.arccos(rel_len)

        var_dir= 100 * (self.rel_wl[idx]**2 * variance) / (1 - rel_len)

        dir_lr= (self.dirc + rel_dir, self.dirc - rel_dir)
        var_lr= (var_dir, var_dir)

        return(dir_lr, var_lr)

class AntArray(object):
    speed_of_light= 299792458.0
    paint_areas= (148, 456, 654, 1063, 1169, 1367)

    def __init__(self, edges, fft_len, fq_low, fq_high):
        self.edges= edges

        self.fft_len= fft_len
        self.crop= self.fft_len//8
        self.samp_len= self.fft_len - 2*self.crop

        self.frequencies= np.linspace(fq_low, fq_high, self.fft_len)[self.crop:-self.crop]
        self.wavelengths= self.speed_of_light/self.frequencies

        for edge in self.edges:
            edge.set_wavelengths(self.wavelengths)

        self.init_spx_optim()

    def init_spx_optim(self):
        # FIXME: remove constants
        limits_samp_off= np.array([math.pi] * 3)
        limits_ph_off= np.array([math.pi] *3)

        limits_params= np.concatenate((limits_samp_off, limits_ph_off))

        self.spx_opt= SimplexOptim(-limits_params, limits_params)


        self.fq_drift= np.zeros(3) + 0.01
        self.ph_old= np.zeros(3)
        self.ph_acc= np.zeros(3)

    def read_samples(self, fd):
        raw_sample= fd.read(3 * 4 * self.fft_len)
        np_sample= np.frombuffer(raw_sample, np.float32)

        if len(np_sample) != 3*self.fft_len:
            raise(Exception())

        phases= np_sample[:self.fft_len][self.crop:-self.crop]
        variances= np_sample[self.fft_len:self.fft_len*2][self.crop:-self.crop]
        mag_sqs= np_sample[self.fft_len*2:][self.crop:-self.crop]

        return((phases, variances, mag_sqs))

    def paint_dist(self, idx, phases, variances):
        d_centers= list()
        d_vars= list()

        for (edge, ph, var) in zip(self.edges, phases, variances):
            cvs= edge.get_direction(idx, ph[idx], var[idx])

            if not cvs:
                continue

            (cs, vs)= cvs

            d_centers.extend(cs)
            d_vars.extend(vs)

        canvas= prop_paint(d_centers, np.sqrt(d_vars), self.samp_len)

        return(canvas)

    def file_step(self, fd):
        samples= list(self.read_samples(fd) for edge in self.edges)

        (orig_phases, variances, mag_sqs)= zip(*samples)

        def process_spx_params(parameters, update=False):
            ant_samp_offs= [0] + list(parameters[:3])
            ant_ph_offs= parameters[3:]

            ph_diff= ant_ph_offs - self.ph_old
            fq_drift= (self.fq_drift * 127 + ph_diff) / 128
            ph_acc= remainder(self.ph_acc + fq_drift)

            if update:
                self.fq_drift= fq_drift
                self.ph_acc= ph_acc
                self.ph_old= ant_ph_offs

            ant_ph_corr= [0] + list(ant_ph_offs + ph_acc)

            edge_samp_offs= list((a - b) for (a,b) in it.combinations(ant_samp_offs, 2))
            edge_ph_corr= list((a - b) for (a,b) in it.combinations(ant_ph_corr, 2))

            return((edge_ph_corr, edge_samp_offs))

        def corrected_phase(ph, pho, sao):
            return(remainder(ph + np.linspace(-sao, sao, len(ph)) + pho))

        def test_parameters(parameters):
            (ph_offs, edge_samp_offs)= process_spx_params(parameters)

            phases= list(
                corrected_phase(ph, pho, sao)
                for (ph, pho, sao) in zip(orig_phases, ph_offs, edge_samp_offs)
            )

            dist_limit= sum(
                math.log(edge.integrate_illegal(phase, variance) + 0.001)
                for (edge, phase, variance) in
                zip(self.edges, phases, variances)
            )

            match= sum(
                max(self.paint_dist(idx, phases, variances))
                for idx in self.paint_areas
            )

            return (match - dist_limit)

        parameters= self.spx_opt.optimize_hop(test_parameters)
        (ph_offs, edge_samp_offs)= process_spx_params(parameters, True)

        phases= list(
            corrected_phase(ph, pho, sao)
            for (ph, pho, sao) in zip(orig_phases, ph_offs, edge_samp_offs)
        )

        for pa in self.paint_areas:
            canvas= self.paint_dist(pa, phases, variances)

            array('f', canvas).tofile(sys.stdout.buffer)

        for ph in phases:
            array('f', ph).tofile(sys.stdout.buffer)

antennas= [
    (0,0),
    (-0.355, 0),
    (-0.1754, 0.3235),
    (-0.1855, 0.1585)
]

edges= list(AntArrayEdge(*aa, *ab) for (aa, ab) in it.combinations(antennas, 2))

antarr= AntArray(edges, 2048, 101e6, 103e6)

if __name__ == '__main__':
    while True:
        antarr.file_step(sys.stdin.buffer)
