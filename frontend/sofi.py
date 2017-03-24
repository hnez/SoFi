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

    def __init__(self, antennas, fft_len, dir_len, fq_low, fq_high):
        self.fft_len= fft_len
        self.dir_len= dir_len

        self.frequencies= np.linspace(fq_low, fq_high, self.fft_len)
        self.wavelengths= self.speed_of_light/self.frequencies

        self.antennas= antennas
        self.antenna_count= len(antennas)
        self.edges_count= math.factorial(self.antenna_count - 1)

        # Initilize the per antenna offset compensation PID controllers.
        # The kp, ki, kd were determined by guessing and
        # looking at the controller behavior.
        # I guess they could use some optimization
        self.ant_phase_err_comps= list(
            PIDController(0.40, 0.6, 0.03)
            for ant in antennas
        )

        self.ant_sample_err_comps= list(
            PIDController(1.5, 2.5, 0.06)
            for ant in antennas
        )

        # Matricies that describe the effect of
        # Taking the phase differeces between antennas
        # to get from antenna->edges. (effect_mat)
        # And a somewhat inverse matrix that
        # describes the edges->antenna way.
        self.effect_mat= self.gen_effect_mat(self.edges_count, self.antenna_count)
        self.inv_effect_mat= self.gen_inv_effect_mat(self.effect_mat)

        # These points will be used to compensate
        # offsets between the receivers.
        # Each point will be a 2-tuple that
        # stores the start and end in the frame
        self.noise_points= list()

        self.signal_points= list()

        self.position_matrix_cache= list()

        self.active_point_range= range(64, fft_len-256)

    def gen_position_matrix(self, wavelength):
        edge_dxy= list(
            (bx-ax, by-ay)
            for ((ax, ay), (bx, by))
            in it.combinations(self.antennas, 2)
        )

        edge_distances= np.fromiter(
            (dx*dx + dy*dy for (dx, dy) in edge_dxy),
            np.float64
        )

        edge_angles=  np.fromiter(
            (math.atan2(dy, dx) for (dx, dy) in edge_dxy),
            np.float64
        )

        rel_wl= 2 * math.pi * edge_distances / wavelength

        test_angles= np.linspace(-math.pi, math.pi, self.dir_len)

        pos_mat= np.zeros((self.dir_len, self.edges_count))

        for (idx, test_angle) in enumerate(test_angles):
            rel_angles= edge_angles + test_angle

            pos_mat[idx, :]= rel_wl * np.sin(rel_angles)

        return(pos_mat)

    def get_position_matrix(self, wl_start, wl_end):
        wl_mid= (wl_start + wl_end)/2

        for (wls, wle, pmat) in self.position_matrix_cache:
            if wl_mid >= wls and wl_mid <= wle:
                return(pmat)

        pmat= self.gen_position_matrix(wl_mid)

        self.position_matrix_cache.append((wl_start, wl_end, pmat))

        return(pmat)

    def get_direction_info(self, phases, idx_start, idx_end):
        phase_vector= np.fromiter(
            (ph[idx_start:idx_end].mean() for ph in phases),
            np.float64
        )

        wl_start= self.wavelengths[idx_start]
        wl_end= self.wavelengths[idx_end]

        pmat= self.get_position_matrix(wl_start, wl_end)

        return(pmat @ phase_vector)

    def find_peaks(self, magnitudes):
        testwidths= np.linspace(14, 18, 5)

        peaks= signal.find_peaks_cwt(magnitudes, testwidths)

        # TODO fix corner cases
        peaks= list(
            p
            for p in peaks
            if p in self.active_point_range
        )

        return(peaks)

    def expand_peaks(self, peaks, magnitudes):
        expanded= list()

        for peak in peaks:
            thr_mag= 0.5 * magnitudes[peak]

            plow= peak
            phigh= peak

            while magnitudes[plow] > thr_mag and plow in self.active_point_range:
                plow-= 1

            while magnitudes[phigh] > thr_mag and phigh in self.active_point_range:
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
        noise_point_vals= np.fromiter(
            (edge_frame[a:b].mean() for (a, b) in self.noise_points if a<b),
            np.float32
        )

        if len(noise_point_vals) < 3:
            return((0,0))

        # The phase error is the mena offset of the
        # phases from zero
        phase_error= noise_point_vals.mean()

        sample_error= sum(
            (bv - av)
            for (av, bv)
            in zip(noise_point_vals[:-1], noise_point_vals[1:])
        ) / (len(noise_point_vals) - 1)

        return((-phase_error, -sample_error))

    def compensate_edge_errors(self, edge_frame, phase_offset, sample_offset):
        shift_start= phase_offset - sample_offset/2
        shift_end= phase_offset + sample_offset/2

        comp= np.linspace(shift_start, shift_end, self.fft_len)

        return(remainder(edge_frame + comp))

    def process_edge_frameset(self, phases, magnitude):
        # Take the per antenna compensation factors
        # that were calculated in the previous frames
        ant_phase_comps= np.fromiter((c.last for c in self.ant_phase_err_comps), np.float32)
        ant_sample_comps= np.fromiter((c.last for c in self.ant_sample_err_comps), np.float32)

        # Calculate the per edge compensation factors
        # from the per antenna factors
        edge_phase_comps= self.ant_to_edge_errors(ant_phase_comps)
        edge_sample_comps= self.ant_to_edge_errors(ant_sample_comps)

        # These Arrays will be used to store
        # the errors in the current frame
        edge_phase_errors= np.zeros(len(edge_phase_comps))
        edge_sample_errors= np.zeros(len(edge_sample_comps))

        # Zip together all the parameters tha will be needed
        # in the per edge loop
        edges_properties= zip(it.count(0), phases, edge_phase_comps, edge_sample_comps)

        compensated_phases= list()

        for (i, edge_frame, edge_ph_comp, edge_sa_comp) in edges_properties:
            edge_frame_compensated= self.compensate_edge_errors(edge_frame, edge_ph_comp, edge_sa_comp)

            efc_raw= edge_frame_compensated.astype(np.float32).tobytes()
            sys.stdout.buffer.write(efc_raw)

            compensated_phases.append(edge_frame_compensated)

            # Calculate and store the current errors
            # For later analysis
            (edge_phase_errors[i], edge_sample_errors[i])= self.calc_edge_errors(edge_frame_compensated)


        for (st, en) in ((390, 420), (251, 261), (380, 388), (507, 517), (635, 645), (760, 770)):
            dir_info= self.get_direction_info(compensated_phases, st, en)
            dir_raw= dir_info.astype(np.float32).tobytes()
            sys.stdout.buffer.write(dir_raw)

        # Output the current magnitude as is
        mag_raw= magnitude.astype(np.float32).tobytes()
        sys.stdout.buffer.write(mag_raw)

        # Determine the per antenna errors from the
        # per edge errors. This uses a Matrix that
        # inverts the effect of calculating the phase differences
        ant_phase_errors= self.edge_to_ant_errors(edge_phase_errors)
        ant_sample_errors= self.edge_to_ant_errors(edge_sample_errors)

        # Update the PID controllers
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

    def find_signalpoints(self, magnitudes):
        peaks= self.find_peaks(magnitudes)

        self.signal_points= self.expand_peaks(peaks, magnitudes)

    def find_noisepoints(self, magnitudes):
        inv_mag= 1/(magnitudes + 0.001)

        peaks= self.find_peaks(inv_mag)

        new_points= self.expand_peaks(peaks, inv_mag)

        if len(new_points) >=3:
            self.noise_points= new_points

antennas= [
    ( 0.0,  0.0),
    ( 0.0,  0.35),
    ( 0.42, 0.35),
    ( 0.42,  0.0)
]

antarr= AntennaArray(antennas, 1024, 1024, 99.0, 101.0)

for frameset_num in it.count(0):
    (phases, magnitude)= antarr.read_frameset(sys.stdin.buffer)

    antarr.process_edge_frameset(phases, magnitude)

    if (frameset_num % 100) == 10:
        antarr.find_noisepoints(magnitude)

        print('Noisepoints:', file=sys.stderr)
        for (nps, npe) in antarr.noise_points:
            print('{}MHz - {}MHz'.format(
                antarr.frequencies[nps] / 1e6,
                antarr.frequencies[npe] / 1e6
            ), file=sys.stderr)


        antarr.find_signalpoints(magnitude)

        print('Signalpoints:', file=sys.stderr)
        for (nps, npe) in antarr.signal_points:
            print('{} - {}'.format(
                nps,
                npe
            ), file=sys.stderr)
