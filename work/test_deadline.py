import json,math,random,subprocess,time,sys
from pathlib import Path
sys.path.insert(0,str(Path.cwd()))
from validate_solution import validate
root=Path.cwd();folder=root/'work/deadline';folder.mkdir(exist_ok=True)
rows=[]
for index,(n,m,k,limit,multi)in enumerate([(120,4,3,.01,False),(5000,20,5,.01,False),(2500,30,5,.15,False),(900,15,8,.3,True),(1000,8,1,.35,False)]):
 rng=random.Random(1234+index)
 sites=[dict(x=rng.random(),y=rng.random(),reward=100,tasks=[rng.randrange(k)for _ in range(20 if index==4 else rng.randrange(1,9))])for _ in range(n)]
 offices=[dict(x=rng.random(),y=rng.random(),cost=5,maxSize=max(30,n//2))for _ in range(m)]
 workers=[rng.sample(range(k),rng.randint(1,k))if multi else [i%k] for i in range(m*k*2)]
 distances=[[50*math.hypot(s['x']-o['x'],s['y']-o['y'])for s in sites]for o in offices]
 d=dict(sites=sites,offices=offices,workers=workers,distances=distances,maxDifference=0 if index%2 else n,balanceCoefficient=100)
 inp=folder/f'{index}.json';inp.write_text(json.dumps(d));out=folder/f'{index}_solution.json'
 started=time.monotonic();p=subprocess.run([str(root/'work/solver-final'),str(inp),str(out),str(limit)],capture_output=True,text=True,timeout=limit+3)
 elapsed=time.monotonic()-started;score=validate(inp,out)
 assert p.returncode==0 and not isinstance(score,str),(index,p.returncode,p.stderr,score)
 assert elapsed<limit+1.5,(index,elapsed,limit)
 row=dict(case=index,sites=n,offices=m,multi_skill_workers=multi,budget=limit,wall_seconds=elapsed,valid=True);rows.append(row);print(row,flush=True)
(folder/'results.json').write_text(json.dumps(rows,indent=2))
