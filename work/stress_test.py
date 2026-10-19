"""Independent randomized validity checks using the supplied official validator."""
import concurrent.futures
import json
import math
from pathlib import Path
import random
import subprocess
import sys

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT))
from validate_solution import validate

def generate(seed):
    rng=random.Random(seed)
    n=rng.randint(5,45);m=rng.randint(1,6);k=rng.randint(1,7)
    sites=[dict(x=rng.random(),y=rng.random(),reward=rng.uniform(.05,2),tasks=[rng.randrange(k)for _ in range(rng.randrange(5))])for _ in range(n)]
    offices=[dict(x=rng.random(),y=rng.random(),cost=rng.uniform(0,1),maxSize=rng.randrange(n+1))for _ in range(m)]
    if seed%4==0:
        for p in sites+offices:p['x']=round(p['x']*4);p['y']=0
    workers=[[rng.randrange(k)]for _ in range(rng.randrange(2,18))]
    if seed%3==0:workers=[rng.sample(range(k),rng.randrange(k+1))for _ in workers]
    distances=[[math.hypot(s['x']-o['x'],s['y']-o['y'])*(1+.1*rng.random())for s in sites]for o in offices]
    return dict(sites=sites,offices=offices,workers=workers,distances=distances,maxDifference=seed%5,balanceCoefficient=rng.choice([0,.01,1,100]))

def check(seed):
    folder=ROOT/'work'/'stress'/str(seed);folder.mkdir(parents=True,exist_ok=True)
    d=generate(seed)
    if seed==64:
        d['workers']=[[i]for i in range(70)];d['sites'][0]['tasks']=list(range(70))
    if seed==65:d['sites']=[];d['distances']=[[]for _ in d['offices']]
    if seed==66:d['offices']=[];d['distances']=[]
    (folder/'instance.json').write_text(json.dumps(d))
    p=subprocess.run([str(ROOT/'work'/'solver-final'),'instance.json','solution.json','.12',str(seed)],cwd=folder,capture_output=True,text=True,timeout=10)
    assert p.returncode==0,(seed,p.stderr)
    score=validate(folder/'instance.json',folder/'solution.json')
    assert not isinstance(score,str),(seed,score,p.stderr)
    return score

if __name__=='__main__':
    with concurrent.futures.ThreadPoolExecutor(max_workers=4)as pool:
        scores=list(pool.map(check,range(67)))
    print(f'PASS: {len(scores)} randomized/edge-case instances; all accepted by the official validator.')
