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
    def __init__(self, ant_a, ant_b):
        self.pos_a= ant_a['pos']
        self.pos_b= ant_b['pos']
        self.vector= self.pos_b - self.pos_a
        (self.distance, self.angle)= self.vector.polar()

        self.frame= -1

    def process(self, frame, phases, variances, mag_sqs):
        self.frame= frame

        samples= zip(phases, variances, mag_sqs)

        for (num, phase, variance, mag_sq) in samples:
            pass

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

        antennas.append(antenna)

    for (ant_a, ant_b) in it.combinations(enumerate(antennas), 2):
        antenna_pairs.append(AntennaPair(ant_a[1], ant_b[1]))

    fft_len= int(config['Common']['fft_len'])

    for frame in it.count(0):
        for pair in antenna_pairs:
            phases= array('f')
            variances= array('f')
            mag_sq= array('f')

            phases.fromfile(sys.stdin, fft_len)
            variances.fromfile(sys.stdin, fft_len)
            mag_sq.fromfile(sys.stdin, fft_len)

            pair.process(frame, phases, variances, mag_sq)

if __name__ == '__main__':
    config= ConfigParser()

    config.read('cheapodoa.ini')

    main(config)
