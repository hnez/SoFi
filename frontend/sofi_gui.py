#!/usr/bin/env python3

'''
    Copyright 2016 Leonard Göhrs
    This file is part of cheapodoa.
    Chepodoa is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    Chepodoa is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with Chepodoa.  If not, see <http://www.gnu.org/licenses/>.
'''

from configparser import ConfigParser
from array import array
import itertools as it
import sys
import math

class Vector(object):
    def __init__(self, x, y):
        self.x= x
        self.y= y

    def length(self):
        return (math.sqrt(self * self))

    def angle(self):
        return (math.atan2(self.y, self.x))

    def polar(self):
        return (self.length(), self.angle())

    def __add__(self, other):
        return (Vector(self.x + other.x, self.y + other.y))

    def __sub__(self, other):
        return (Vector(self.x - other.x, self.y - other.y))

    def __mul__(self, other):
        return (self.x*other.x + self.y*other.y)

    def __neg__(self):
        return (Vector(-self.x, -self.y))

class AntennaPair(object):
    def __init__(self, ant_a, ant_b, lambda_map):
        self.ant_a= ant_a
        self.ant_b= ant_a

        self.lambda_map= lambda_map

        self.vector= ant_b['pos'] - ant_a['pos']
        (self.distance, self.angle)= self.vector.polar()

        print('{} -> {}:'.format(ant_a['name'], ant_b['name']))
        print(' dst: {}m'.format(self.distance))
        print(' ang: {}'.format(math.degrees(self.angle)))
        
    def process(self, phases, variances, mag_sqs):
        samples= zip(it.count(0), phases, variances, mag_sqs)

        out_dist= array('f')

        for (num, phase, variance, mag_sq) in samples:            
            dist= phase/math.pi * self.lambda_map[num]

            out_dist.append(dist)
            
        return(out_dist)

def gen_lambda_map(freq_center, freq_sample, fft_len):
    freq_min= freq_center - freq_sample/2
    freq_max= freq_center + freq_sample/2
    c= 299792458.0
    
    def num_to_freq(num):
        fft_half= fft_len//2
        num_fft_trans= fft_half + (num if num < fft_half else num-fft_len)
        rpos= num_fft_trans / fft_len

        return(freq_min * (1-rpos) + freq_max * rpos)

    def freq_to_lambda(freq):
        return(c/freq)

    lambda_map= list(
        freq_to_lambda(num_to_freq(num))
        for num in range(fft_len)
    )
    
    return(lambda_map)

def main(config):
    antennas= list()
    antenna_pairs= list()
    
    for ant_idx in it.count(1):
        antname= 'Antenna {}'.format(ant_idx)
        antenna= dict()

        if antname not in config:
            break

        pos_x= float(config[antname]['position_x'])
        pos_y= float(config[antname]['position_y'])

        antenna['pos']= Vector(pos_x, pos_y)
        antenna['name']= antname

        antennas.append(antenna)

    fft_len= int(config['Common']['fft_len'])
    center_freq= float(config['Common']['center_freq'])
    sampling_freq= float(config['Common']['sampling_freq'])
    
    lambda_map= gen_lambda_map(center_freq, sampling_freq, fft_len)
        
    for (ant_a, ant_b) in it.combinations(antennas, 2):
        antenna_pairs.append(AntennaPair(ant_a, ant_b, lambda_map))

    
    fo= open('tsto.bin', 'wb')
    
    for frame in it.count(0):
        for pair in antenna_pairs:
            phases= array('f')
            variances= array('f')
            mag_sq= array('f')

            phases.fromfile(sys.stdin.buffer, fft_len)
            variances.fromfile(sys.stdin.buffer, fft_len)
            mag_sq.fromfile(sys.stdin.buffer, fft_len)

            rp= pair.process(phases, variances, mag_sq)
            rp.tofile(fo)
            
if __name__ == '__main__':
    config= ConfigParser()
    config.read('sofi.ini')
    
    main(config)
