import numpy as np
import itertools as it

class SimplexOptim(object):
    walk_factors= (-0.95, 0.5, 2.05)

    def __init__(self, limits_bottom, limits_top):
        self.ltop= limits_top
        self.lbottom= limits_bottom

        simplex_dim= len(limits_top) + 1

        self.simplex= list(np.array(self.lbottom) for i in range(simplex_dim))

        for (n, top) in enumerate(self.ltop):
            self.simplex[n+1][n]= top

    def points_mean(self, points):
        return(sum(points)/len(points))

    def points_expand(self, pa, pb, f):
        newp= (1-f)*pa + f*pb

        for (i, b, t) in zip(it.count(0), self.lbottom, self.ltop):
            w= t-b

            newp[i]= ((newp[i] - b) % w) + b

        return(newp)

    def points_rank(self, points, score_fn):
        ratings= list(zip(points, map(score_fn, points)))

        ranking= sorted(ratings, key=lambda a: a[1])
        points= list(rank[0] for rank in ranking)

        return(points)

    def optimize_hop(self, score_fn):
        points= self.points_rank(self.simplex, score_fn)

        #print('Simplex:')
        #for pnt in points:
        #    print(','.join(str(num) for num in pnt))

        mid= self.points_mean(points[1:])
        pivot= points[0]

        expands= list(self.points_expand(mid, pivot, f) for f in self.walk_factors)

        new_points= self.points_rank(expands, score_fn)

        self.simplex= [new_points[-1]] + points[1:]

        return(self.points_mean(self.simplex))
