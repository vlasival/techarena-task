"""Minimal working Python submission for TechArena Solver (model 20261).

Opens office 0 with one site and enough workers to cover that site's tasks.
"""

import json
import sys
from pathlib import Path


def cover_tasks(workers, site_tasks):
    need = set(site_tasks)
    if not need:
        return []
    chosen = []
    used = set()
    while need:
        found = -1
        for wid, skills in enumerate(workers):
            if wid in used:
                continue
            if need & set(skills):
                found = wid
                break
        if found < 0:
            return None
        chosen.append(found)
        used.add(found)
        need -= set(workers[found])
    return sorted(chosen)


def minimal_solution(instance):
    sites = instance.get("sites") or []
    offices = instance.get("offices") or []
    workers = instance.get("workers") or []
    if not sites or not offices:
        return []
    office_id = 0
    max_size = int(offices[0].get("maxSize", 0))
    if max_size < 1:
        return []
    for site_id, site in enumerate(sites):
        tasks = site.get("tasks") or []
        picked = cover_tasks(workers, tasks)
        if picked is None:
            continue
        return [{"officeId": office_id, "sites": [site_id], "workers": picked}]
    return []


def main() -> int:
    input_path = Path(sys.argv[1] if len(sys.argv) > 1 else "instance.json")
    output_path = Path(sys.argv[2] if len(sys.argv) > 2 else "solution.json")
    if not input_path.is_file():
        return 1
    instance = json.loads(input_path.read_text(encoding="utf-8-sig"))
    solution = minimal_solution(instance)
    output_path.write_text(json.dumps(solution, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
