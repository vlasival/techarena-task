#!/usr/bin/env python3
"""Run the solver on the provided instances and check with the official validator."""
import argparse
import concurrent.futures
import csv
from pathlib import Path
import subprocess
import time

from validate_solution import validate


def main():
    root=Path(__file__).resolve().parent
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds',type=float,default=270)
    parser.add_argument('--jobs',type=int,default=1)
    parser.add_argument('--seed',type=int,default=712367821)
    parser.add_argument('--solver',type=Path,default=root/('solver.exe'if (root/'solver.exe').exists()else 'solver'))
    args=parser.parse_args()
    folder=root/'results'/'benchmark';folder.mkdir(parents=True,exist_ok=True)
    def run(path):
        out=folder/f'{path.parent.name}.json';start=time.monotonic()
        proc=subprocess.run([str(args.solver.resolve()),str(path),str(out),str(args.seconds),str(args.seed)],capture_output=True,text=True,timeout=args.seconds+15)
        if proc.returncode:raise RuntimeError(f'{path.parent.name}: {proc.stderr}')
        score=validate(path,out)
        if isinstance(score,str):raise RuntimeError(f'{path.parent.name}: {score}')
        return dict(instance=path.parent.name,objective=score,seconds=round(time.monotonic()-start,3),valid=True)
    paths=sorted(root.glob('instance*/instance.json'))
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs)as pool:
        rows=list(pool.map(run,paths))
    with (folder/'scores.csv').open('w',newline='')as f:
        writer=csv.DictWriter(f,fieldnames=['instance','objective','seconds','valid']);writer.writeheader();writer.writerows(rows)
    for row in rows:print(f"{row['instance']}: {row['objective']:.8f}; {row['seconds']:.3f}s; VALID")


if __name__=='__main__':main()
