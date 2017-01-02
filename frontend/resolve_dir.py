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
        self.rel_wl= (wavelengths / self.dist) / (2*math.pi)

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

        var_dir= 25 * (self.rel_wl[idx]**2 * variance) / (1 - rel_len)

        dir_lr= (self.dirc + rel_dir, self.dirc - rel_dir)
        var_lr= (var_dir, var_dir)

        return(dir_lr, var_lr)

class AntArray(object):
    speed_of_light= 299792458.0
    paint_areas= (400//4, 712//4, 913//4, 1323//4, 1419//4, 1648//4)

    def __init__(self, edges, fft_len, fq_low, fq_high):
        self.edges= edges

        self.fft_len= fft_len

        self.frequencies= np.linspace(fq_low, fq_high, self.fft_len)
        self.wavelengths= self.speed_of_light/self.frequencies

        for edge in self.edges:
            edge.set_wavelengths(self.wavelengths)

        self.init_spx_optim()

        lin= np.linspace(-np.pi, np.pi, fft_len)
        self.cow_re= np.cos(lin)
        self.cow_im= np.sin(lin)
        self.focus_rates= np.concatenate((
            np.linspace(0, 1, fft_len//2),
            np.linspace(1, 0, fft_len//2)
        ))

    def center_of_weight(self, weights):
        re_sum= (weights * self.cow_re).sum()
        im_sum= (weights * self.cow_im).sum()

        rpos= math.atan2(im_sum, re_sum)/(2 * math.pi) + 0.5

        return(rpos * len(weights))

    def rate_focus(self, canvas):
        cow= int(self.center_of_weight(canvas) + 0.5)

        recenter= np.concatenate((canvas[cow:], canvas[:cow]))

        return((recenter * self.focus_rates).mean())

    def init_spx_optim(self):
        # FIXME: remove constants
        limits_samp_off= np.array([math.pi] * 3)
        limits_ph_off= np.array([math.pi] *3)

        limits_params= np.concatenate((limits_samp_off, limits_ph_off))

        self.spx_opt= SimplexOptim(-limits_params, limits_params)


        self.fq_drift= np.zeros(3)
        self.ph_acc= np.zeros(3)
        self.ph_old= np.zeros(3)
        self.sao_old= np.zeros(3)

    def read_samples(self, fd):
        raw_sample= fd.read(3 * 4 * self.fft_len)
        np_sample= np.frombuffer(raw_sample, np.float32)

        if len(np_sample) != 3*self.fft_len:
            raise(Exception())

        phases= np_sample[:self.fft_len]
        variances= np_sample[self.fft_len:self.fft_len*2]
        mag_sqs= np_sample[self.fft_len*2:]

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

        canvas= prop_paint(d_centers, np.sqrt(d_vars), self.fft_len)

        return(canvas)

    def file_step(self, fd):
        samples= list(self.read_samples(fd) for edge in self.edges)

        (orig_phases, variances, mag_sqs)= zip(*samples)

        def process_spx_params(parameters, update=False):
            ant_samp_offs= parameters[:3]
            ant_ph_offs= parameters[3:]

            ph_diff= ant_ph_offs - self.ph_old
            fq_drift= (self.fq_drift * 127 + ph_diff) / 128
            ph_acc= remainder(self.ph_acc + fq_drift)

            change= math.sqrt(
                ((self.ph_old - ant_ph_offs)**2).sum() +
                ((self.sao_old - ant_samp_offs)**2).sum()
            )

            if update:
                self.fq_drift= fq_drift
                self.ph_acc= ph_acc
                self.ph_old= ant_ph_offs
                self.sao_old= ant_samp_offs

            ant_ph_corr= [0] + list(ant_ph_offs + ph_acc)
            ant_samp_corr= [0] + list(ant_samp_offs)

            edge_samp_offs= list((a - b) for (a,b) in it.combinations(ant_samp_corr, 2))
            edge_ph_corr= list((a - b) for (a,b) in it.combinations(ant_ph_corr, 2))

            return((edge_ph_corr, edge_samp_offs, change))

        def corrected_phase(ph, pho, sao):
            corrected= remainder(ph + np.linspace(-sao, sao, len(ph)) + pho)

            corrected[:16]= 0
            corrected[-16:]= 0

            return(corrected)

        def test_parameters(parameters):
            (ph_offs, edge_samp_offs, change)= process_spx_params(parameters)

            phases= list(
                corrected_phase(ph, pho, sao)
                for (ph, pho, sao) in zip(orig_phases, ph_offs, edge_samp_offs)
            )

            dist_limit= sum(
                math.log(edge.integrate_illegal(phase, variance) + 0.001)
                for (edge, phase, variance) in
                zip(self.edges, phases, variances)
            )

            focus= sum(
                self.rate_focus(self.paint_dist(idx, phases, variances))
                for idx in self.paint_areas[1:2]
            )

            focus*= 0
            dist_limit*= 1
            change*= 0

            #print('{:8} {:8} {:8}'.format(focus, dist_limit, change), file=sys.stderr)

            return (-focus -dist_limit -change)

        parameters= self.spx_opt.optimize_hop(test_parameters)
        (ph_offs, edge_samp_offs, change)= process_spx_params(parameters, True)

        phases= list(
            corrected_phase(ph, pho, sao)
            for (ph, pho, sao) in zip(orig_phases, ph_offs, edge_samp_offs)
        )

        for pa in self.paint_areas:
            canvas= self.paint_dist(pa, phases, variances)

            array('f', canvas).tofile(sys.stdout.buffer)

        for (ph, edge) in zip(phases, self.edges):
            array('f', edge.rel_wl * ph/edge.dist).tofile(sys.stdout.buffer)

        #for (ph, edge) in zip(phases, self.edges):
        #    array('f', ph).tofile(sys.stdout.buffer)

antennas= [
    (0,0),
    (-0.355, 0),
    (-0.1754, 0.3235),
    (-0.1855, 0.1585)
]

edges= list(AntArrayEdge(*aa, *ab) for (aa, ab) in it.combinations(antennas, 2))

antarr= AntArray(edges, 512, 101e6, 103e6)

if __name__ == '__main__':
    while True:
        antarr.file_step(sys.stdin.buffer)
