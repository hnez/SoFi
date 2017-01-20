#!/usr/bin/env python3

import sys
import numpy as np
import itertools as it

from array import array

def remainder(x1, x2=2*np.pi):
    return(x1 - x2*np.round(x1 / x2))

def sim_path(wavelength, rcv_x, rcv_y, path_x, path_y):
    dist_x= path_x - rcv_x
    dist_y= path_y - rcv_y
    
    dist= np.sqrt(dist_x**2 + dist_y**2)

    norm_phase= (dist / wavelength)%1.0
    
    return(2*np.pi*norm_phase)

def main():
    t= np.arange(0, 600, 0.1)
    c= 299792458.0

    samp_len= 2048
    
    indexes= np.array([400, 712, 913, 1323, 1419, 1648])

    spectrum= np.linspace(100e6, 102e6, samp_len)
    
    freqs= spectrum[indexes]
    wavelengths= c/freqs
    
    paths= (
        (20* np.cos( 2*np.pi*(t/60)), 20* np.sin( 2*np.pi*(t/60))),
        (15* np.cos(-2*np.pi*(t/30)), 15* np.sin(-2*np.pi*(t/30))),
        (20+10* np.cos( 2*np.pi*(t/60)), 10* np.sin( 2*np.pi*(t/60))),
        (10* np.cos( 2*np.pi*(t/60)), 20+10* np.sin( 2*np.pi*(t/60))),
        (-20+10* np.cos( 2*np.pi*(t/30)), 10* np.sin( 2*np.pi*(t/30))),
        (10* np.cos( 2*np.pi*(t/30)), -20+10* np.sin( 2*np.pi*(t/30))),
    )

    antennas= (
        (0,0),
        (-0.355, 0),
        (-0.1754, 0.3235),
        (-0.1855, 0.1585)
    )

    offsets= (
        0.1,
        0.2,
        0.3,
        0.4
    )

    edges= list()
    
    for (fq, wl, pt) in zip(freqs, wavelengths, paths):        
        sims= list(
            sim_path(wl, ant[0], ant[1], pt[0], pt[1])
            for ant in antennas
        )

        diffs= list(
            remainder((pb + oa) - (pa + ob))
            for ((pa, oa), (pb, ob))
            in it.combinations(zip(sims, offsets), 2)
        )

        edges.append(diffs)

    mags= np.ones(samp_len)
    varis= np.ones(samp_len) * 0.00001
        

    for frame in it.count(0):
        for edge in range(6):
            phases= np.zeros(samp_len)
        
            for (sid, idx) in enumerate(indexes):
                phases[idx-10:idx+10]= edges[sid][edge][frame]

            array('f', phases).tofile(sys.stdout.buffer)
            array('f', varis).tofile(sys.stdout.buffer)
            array('f', mags).tofile(sys.stdout.buffer)

if __name__ == '__main__':
    main()
