#!/usr/bin/env python3

from array import array
import itertools as it
import numpy as np
import math
import sys

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
        side_a= np.angle(edge_frame[76:120].mean())
        side_b= np.angle(edge_frame[340:350].mean())
        side_c= np.angle(edge_frame[524:550].mean())
        side_d= np.angle(edge_frame[930:940].mean())

        phase_error= (side_a + side_b + side_c + side_d) / 4
        sample_error= ((side_b - side_a) + (side_c - side_b) + (side_d - side_c))/3

        return((-phase_error, -sample_error))

    def compensate_edge_errors(self, edge_frame, phase_offset, sample_offset):
        cfunc= np.exp(1j* np.linspace(
            phase_offset - sample_offset/2, phase_offset + sample_offset/2, self.fft_len
        ))

        return(edge_frame * cfunc)


    def process_edge_frameset(self, frames):
        ant_phase_comps= np.fromiter((c.last for c in self.ant_phase_err_comps), np.float32)
        ant_sample_comps= np.fromiter((c.last for c in self.ant_sample_err_comps), np.float32)

        edge_phase_comps= self.ant_to_edge_errors(ant_phase_comps)
        edge_sample_comps= self.ant_to_edge_errors(ant_sample_comps)

        edge_phase_errors= np.zeros(len(edge_phase_comps))
        edge_sample_errors= np.zeros(len(edge_sample_comps))

        edges_properties= zip(it.count(0), frames, edge_phase_comps, edge_sample_comps)

        for (i, edge_frame, edge_ph_comp, edge_sa_comp) in edges_properties:
            edge_frame_compensated= self.compensate_edge_errors(edge_frame, edge_ph_comp, edge_sa_comp)

            compensated_raw= edge_frame_compensated.astype(np.complex64).tobytes()
            sys.stdout.buffer.write(compensated_raw)

            (edge_phase_errors[i], edge_sample_errors[i])= self.calc_edge_errors(edge_frame_compensated)

        ant_phase_errors= self.edge_to_ant_errors(edge_phase_errors)
        ant_sample_errors= self.edge_to_ant_errors(edge_sample_errors)

        for (comp, err) in zip(self.ant_phase_err_comps, ant_phase_errors):
            comp.update(err)

        for (comp, err) in zip(self.ant_sample_err_comps, ant_sample_errors):
            comp.update(err)

    def read_frameset(self, fd):
        frameset= list()

        for ei in range(self.edges_count):
            raw_frame= fd.read(8 * self.fft_len)
            np_frame= np.frombuffer(raw_frame, np.complex64)

            if len(np_frame) != self.fft_len:
                raise(Exception('Failed to read frame'))

            frameset.append(np_frame)

        return(frameset)

antennas= [
    ( 0.0,   0.0),
    (-0.353, 0.545),
    ( 0.34,  0.545),
    ( 0.0,  -0.664)
]

antarr= AntennaArray(antennas, 1024, 100e6, 102e6)

for frameset_num in it.count(0):
    frameset= antarr.read_frameset(sys.stdin.buffer)

    antarr.process_edge_frameset(frameset)
