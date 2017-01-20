#!/usr/bin/env python3

from array import array
from simplex_optim import SimplexOptim

import itertools as it
import numpy as np
import math
import sys

def remainder(x1, x2=2*math.pi):
    return(x1 - x2*np.round(x1 / x2))

class VirtualAntennaArray(object):
    def __init__(self, ant_positions):
        distances= list()
        angles= list()

        for (a, b) in it.combinations(ant_positions, 2):
            dx= a[0]-b[0]
            dy= a[1]-b[1]

            distances.append(math.sqrt(dx**2 + dy**2))
            angles.append(math.atan2(dy, dx)) # swap dx and dy?

        self.distances= np.array(distances)
        self.angles= np.array(angles)

        self.num_edges= len(self.distances)

    def simulator_mat(self, wavelength, bins):
        dc= 2 * math.pi * self.distances / wavelength

        angles= np.linspace(-math.pi, math.pi, bins)

        sim_mat= np.zeros((bins, self.num_edges))

        for (idx, angle) in enumerate(angles):
            rel_angles= self.angles + angle

            sim_mat[idx, :]= dc * np.sin(rel_angles)

        return(sim_mat)

class PhysicalAntennaArray(object):
    speed_of_light= 299792458.0

    def __init__(self, virt, pois, fft_len, fq_low, fq_high):
        self.fft_len= fft_len

        self.frequencies= np.linspace(fq_low, fq_high, self.fft_len)
        self.wavelengths= self.speed_of_light/self.frequencies

        self.simulations= list(
            (idx, virt.simulator_mat(self.wavelengths[idx], fft_len))
            for idx in pois
        )

        self.init_spx_optim()

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
        raw_sample= fd.read(2 * 4 * self.fft_len)
        np_sample= np.frombuffer(raw_sample, np.float32)

        if len(np_sample) != 2*self.fft_len:
            raise(Exception())

        mag_sqs= np_sample[:self.fft_len]
        phases= np_sample[self.fft_len:]

        return((phases, mag_sqs))

    def file_step(self, fd):
        samples= list(self.read_samples(fd) for i in range(6))

        (orig_phases, mag_sqs)= zip(*samples)

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

            corrected[:40]= 0
            corrected[-40:]= 0

            return(corrected)

        def test_parameters(parameters):
            (ph_offs, edge_samp_offs, change)= process_spx_params(parameters)

            phases= list(
                corrected_phase(ph, pho, sao)
                for (ph, pho, sao) in zip(orig_phases, ph_offs, edge_samp_offs)
            )

            dc_off= 0
            steep= 0

            for (ph, mag) in zip(phases, mag_sqs):
                selector= mag <= 0.2*mag.mean()

                dc_off+= (ph[selector]**2).mean()

                steep+= ((ph[1:] - ph[:-1])**2).mean()

            #print(dc_off, steep, file=sys.stderr)

            return (-dc_off**0.25)# -10*steep)

        parameters= self.spx_opt.optimize_hop(test_parameters)
        (ph_offs, edge_samp_offs, change)= process_spx_params(parameters, True)

        phases= list(
            corrected_phase(ph, pho, sao)
            for (ph, pho, sao) in zip(orig_phases, ph_offs, edge_samp_offs)
        )

        for ph in phases:
            array('f', ph).tofile(sys.stdout.buffer)

        test_angles= np.linspace(-math.pi, math.pi, self.fft_len)

        for (idx, mat)  in self.simulations:
            phvec= np.array(list(ph[idx-5:idx+5].mean() for ph in phases))

            vspectrum= mat.dot(phvec)

            array('f', vspectrum).tofile(sys.stdout.buffer)

antennas= [
    (-8.5, -7.5),
    (-28.5, 0),
    (-28.5, -28.5),
    (0, -28.5)
]

pois= (152, 250, 300, 402, 610, 712)

vantarr= VirtualAntennaArray(antennas)
physarr= PhysicalAntennaArray(vantarr, pois, 1024, 100e6, 102e6)

if __name__ == '__main__':
    while True:
        physarr.file_step(sys.stdin.buffer)
