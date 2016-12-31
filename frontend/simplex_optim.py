import numpy as np
import itertools as it

from random import Random

import sys

class SimplexOptim(object):
    walk_factors= (-1.05, 0.45, 1.95)

    def __init__(self, limits_bottom, limits_top):
        self.ltop= np.array(limits_top)
        self.lbottom= np.array(limits_bottom)

        simplex_dim= len(limits_top) + 1

        self.simplex= list(np.array(self.lbottom) for i in range(simplex_dim))

        for (n, top) in enumerate(self.ltop):
            self.simplex[n+1][n]= top

        self.seed= Random(0)

    def points_mean(self, points):
        return(sum(points)/len(points))

    def points_expand(self, pa, pb, f):
        newp= (1-f)*pa + f*pb

        w= self.ltop - self.lbottom
        newp= ((newp - self.lbottom) % w) + self.lbottom

        return(newp)

    def points_rank(self, points, score_fn):
        ratings= list(zip(points, map(score_fn, points)))

        ranking= sorted(ratings, key=lambda a: a[1])
        points= list(rank[0] for rank in ranking)

        return(points)

    def noise_point(self, mid):
        new= mid.copy()

        nidx= self.seed.randrange(len(new))
        width= (self.ltop - self.lbottom)[nidx]
        spread= width/4

        new[nidx]+= self.seed.uniform(-spread, spread)

        return(new)

    def optimize_hop(self, score_fn):
        points= self.points_rank(self.simplex, score_fn)

        #print('Simplex:', file=sys.stderr)
        #for pnt in points:
        #    print(','.join(str(num) for num in pnt), file=sys.stderr)

        mid= self.points_mean(points[1:])
        pivot= points[0]

        expands= list(self.points_expand(mid, pivot, f) for f in self.walk_factors)
        expands.append(self.noise_point(mid))

        new_points= self.points_rank(expands, score_fn)

        self.simplex= [new_points[-1]] + points[1:]

        return(self.points_mean(self.simplex))
