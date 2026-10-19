"""Offline upper bound: relax geometry; supports the singleton skills in the supplied data."""
import json
import sys
from collections import Counter
import numpy as np
from scipy.optimize import Bounds, LinearConstraint, milp
from scipy.sparse import coo_matrix

def solve(path, seconds=30):
    d=json.load(open(path)); n=len(d['sites']); m=len(d['offices'])
    skills=sorted({t for s in d['sites'] for t in s['tasks']}|{t for w in d['workers'] for t in w})
    assert all(len(w)<=1 for w in d['workers'])
    supply=Counter(t for w in d['workers'] for t in w); K=len(skills)
    nx=n*m; iz=nx; iu=iz+m*K; imax=iu+m; imin=imax+1; nv=imin+1
    c=np.zeros(nv); ub=np.ones(nv); ub[imax:]=sum(len(s['tasks']) for s in d['sites'])
    for o in range(m):
        for s in range(n):c[o*n+s]=d['distances'][o][s]-d['sites'][s]['reward']
        c[iu+o]=d['offices'][o]['cost']
    c[imax]=d['balanceCoefficient'];c[imin]=-d['balanceCoefficient']
    rows=[];cols=[];vals=[];limits=[]
    def row(entries,limit):
        i=len(limits)
        for j,v in entries: rows.append(i);cols.append(j);vals.append(v)
        limits.append(limit)
    for s in range(n):row([(o*n+s,1)for o in range(m)],1)
    row([(imin,1),(imax,-1)],0)
    for o in range(m):
        row([(o*n+s,1)for s in range(n)]+[(iu+o,-d['offices'][o]['maxSize'])],0)
        for k,t in enumerate(skills):
            row([(iz+o*K+k,1),(iu+o,-1)],0)
            for s in range(n):
                if t in d['sites'][s]['tasks']:row([(o*n+s,1),(iz+o*K+k,-1)],0)
        row([(o*n+s,len(d['sites'][s]['tasks']))for s in range(n)]+[(imax,-1)],0)
        row([(o*n+s,-len(d['sites'][s]['tasks']))for s in range(n)]+[(imin,1),(iu+o,ub[imax])],ub[imax])
        for p in range(m):
            if o!=p:row([(o*n+s,1)for s in range(n)]+[(p*n+s,-1)for s in range(n)]+[(iu+o,n),(iu+p,n)],d['maxDifference']+2*n)
    for k,t in enumerate(skills):row([(iz+o*K+k,1)for o in range(m)],supply[t])
    A=coo_matrix((vals,(rows,cols)),shape=(len(limits),nv)).tocsc()
    integ=np.zeros(nv);integ[iz:iu+m]=1
    r=milp(c,integrality=integ,bounds=Bounds(np.zeros(nv),ub),constraints=LinearConstraint(A,-np.inf,np.array(limits)),options={'time_limit':seconds,'mip_rel_gap':.000001})
    print(json.dumps({'instance':path,'relaxed_objective':-r.fun if r.fun is not None else None,'upper_bound':-r.mip_dual_bound if r.mip_dual_bound is not None else None,'status':r.message}),flush=True)

if __name__=='__main__':solve(sys.argv[1],float(sys.argv[2])if len(sys.argv)>2 else 30)
