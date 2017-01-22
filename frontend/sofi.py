#!/usr/bin/env python3

from array import array
import itertools as it
import numpy as np
from scipy import signal
import math
import sys

def remainder(x1, x2=2*math.pi):
    return(x1 - x2*np.round(x1 / x2))

class PIDController(object):
    def __init__(self, kp, ki, kd):
        self.kp= kp
        self.ki= ki
        self.kd= kd

        self.err_acc= 0.0
        self.err_last= 0.0

        self.last= 0.0

    def update(self, err):
        err_diff= err - self.err_last
        self.err_last= err

        self.err_acc+= err

        vp= self.kp * err
        vi= self.ki * self.err_acc
        vd= self.kd * err_diff

        self.last= vp + vi + vd

class AntennaArray(object):
    speed_of_light= 299792458.0

    def __init__(self, antennas, fft_len, fq_low, fq_high):
        self.fft_len= fft_len

        self.frequencies= np.linspace(fq_low, fq_high, self.fft_len)
        self.wavelengths= self.speed_of_light/self.frequencies

        self.antenna_count= len(antennas)
        self.edges_count= math.factorial(self.antenna_count - 1)

        self.ant_phase_err_comps= list(
            PIDController(0.40, 0.6, 0.03)
            for ant in antennas
        )

        self.ant_sample_err_comps= list(
            PIDController(1.5, 2.5, 0.06)
            for ant in antennas
        )

        self.effect_mat= self.gen_effect_mat(self.edges_count, self.antenna_count)
        self.inv_effect_mat= self.gen_inv_effect_mat(self.effect_mat)

        self.noise_points= list()

    def find_peaks(self, magnitudes):
        testwidths= np.linspace(14, 18, 5)

        peaks= signal.find_peaks_cwt(magnitudes, testwidths)

        # TODO fix corner cases
        peaks= list(p for p in peaks if (p>50 and p<950))

        return(peaks)

    def expand_peaks(self, peaks, magnitudes):
        expanded= list()

        for peak in peaks:
            thr_mag= 0.5 * magnitudes[peak]

            plow= peak
            phigh= peak

            while magnitudes[plow] > thr_mag and plow > 0:
                plow-= 1

            while magnitudes[phigh] > thr_mag and phigh < len(magnitudes):
                phigh+= 1

            expanded.append((plow, phigh))

        return(expanded)

    def gen_effect_mat(self, edges_count, antenna_count):
        effect_mat= np.zeros((edges_count, antenna_count))

        # construct a matrix that simulates the
        # behavior of the backend.
        # Given a vector of antenna phases x
        #  effect_mat @ x
        # will output the phase differences
        for (i, (a, b)) in zip(it.count(0), it.combinations(range(antenna_count), 2)):
            effect_mat[i, a]= 1
            effect_mat[i, b]= -1

        return(effect_mat)

    def gen_inv_effect_mat(self, effect_mat):
        # As the errors will be relative:
        # assume that the first source phase is correct
        # and reduce the effect matrix
        red_effect_mat= effect_mat[0:,1:]

        # With 6 edges for 3 errors (assuming 4 antennas)
        # the calculation is overdefined.
        # To counter that errors[0:3] are added to errors[3:6]
        # to form a new three element error vector.
        # The same operations are performed on the effect matrix
        rect_effect_mat= red_effect_mat[0:3,] + red_effect_mat[3:,]

        # Invert the matrix to get from edge errors > antenna errors
        inv_effect_mat= np.linalg.inv(rect_effect_mat)

        return(inv_effect_mat)

    def ant_to_edge_errors(self, ant_err):
        return(self.effect_mat @ ant_err)

    def edge_to_ant_errors(self, edge_err):
        np_edge_err= np.array(edge_err)

        # We want to solve for 3 errors (assuming 4 antennas)
        # For that we only need thre variables.
        # The first step is to reduce the 6 input errors to 3
        red_edge_err= np_edge_err[:3] + np_edge_err[3:]

        # The error for the first antenna is assumed to be zero
        # only the other errors will be set
        ant_err= np.zeros(self.antenna_count)

        ant_err[1:]= self.inv_effect_mat @ red_edge_err

        return(ant_err)

    def calc_edge_errors(self, edge_frame):
        checkpoints= np.fromiter(
            (edge_frame[a:b].mean() for (a, b) in self.noise_points if a<b),
            np.float32
        )

        if len(checkpoints) < 3:
            return((0,0))

        phase_error= checkpoints.mean()

        pairs= zip(checkpoints[:-1], checkpoints[1:])

        sample_error= sum(b-a for (a, b) in pairs) / len(checkpoints)

        return((-phase_error, -3*sample_error))

    def compensate_edge_errors(self, edge_frame, phase_offset, sample_offset):
        shift_start= phase_offset - sample_offset/2
        shift_end= phase_offset + sample_offset/2

        comp= np.linspace(shift_start, shift_end, self.fft_len)

        return(remainder(edge_frame + comp))

    def process_edge_frameset(self, phases, magnitude):
        ant_phase_comps= np.fromiter((c.last for c in self.ant_phase_err_comps), np.float32)
        ant_sample_comps= np.fromiter((c.last for c in self.ant_sample_err_comps), np.float32)

        edge_phase_comps= self.ant_to_edge_errors(ant_phase_comps)
        edge_sample_comps= self.ant_to_edge_errors(ant_sample_comps)

        edge_phase_errors= np.zeros(len(edge_phase_comps))
        edge_sample_errors= np.zeros(len(edge_sample_comps))

        edges_properties= zip(it.count(0), phases, edge_phase_comps, edge_sample_comps)

        for (i, edge_frame, edge_ph_comp, edge_sa_comp) in edges_properties:
            edge_frame_compensated= self.compensate_edge_errors(edge_frame, edge_ph_comp, edge_sa_comp)

            compensated_raw= edge_frame_compensated.astype(np.float32).tobytes()
            sys.stdout.buffer.write(compensated_raw)

            (edge_phase_errors[i], edge_sample_errors[i])= self.calc_edge_errors(edge_frame_compensated)

        mag_raw= magnitude.astype(np.float32).tobytes()
        sys.stdout.buffer.write(mag_raw)

        ant_phase_errors= self.edge_to_ant_errors(edge_phase_errors)
        ant_sample_errors= self.edge_to_ant_errors(edge_sample_errors)

        for (comp, err) in zip(self.ant_phase_err_comps, ant_phase_errors):
            comp.update(err)

        for (comp, err) in zip(self.ant_sample_err_comps, ant_sample_errors):
            comp.update(err)

    def read_frameset(self, fd):
        frames= list()

        for ei in range(self.edges_count + 1):
            raw_frame= fd.read(4 * self.fft_len)
            np_frame= np.frombuffer(raw_frame, np.float32)

            if len(np_frame) != self.fft_len:
                raise(Exception('Failed to read frame'))

            frames.append(np_frame)

        return((frames[:-1], frames[-1]))

    def find_noisepoints(self, magnitudes):
        inv_mag= 1/(magnitudes + 0.001)

        peaks= self.find_peaks(inv_mag)

        self.noise_points= self.expand_peaks(peaks, inv_mag)

antennas= [
    ( 0.0,   0.0),
    (-0.353, 0.545),
    ( 0.34,  0.545),
    ( 0.0,  -0.664)
]

antarr= AntennaArray(antennas, 1024, 100e6, 102e6)

for frameset_num in it.count(0):
    (phases, magnitude)= antarr.read_frameset(sys.stdin.buffer)

    antarr.process_edge_frameset(phases, magnitude)

    if (frameset_num % 100) == 0:
        antarr.find_noisepoints(magnitude)
