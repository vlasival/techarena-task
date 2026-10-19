import argparse
import json
import sys
from collections.abc import Iterable, Sequence
from pathlib import Path

from pydantic import BaseModel, ConfigDict, Field, TypeAdapter, ValidationError, model_validator
from shapely import MultiPoint, Point


class Site(BaseModel):
	x: float
	y: float
	reward: float
	tasks: Sequence[int]


class Office(BaseModel):
	x: float
	y: float
	cost: float
	maxSize: int


class Instance(BaseModel):
	sites: Sequence[Site]
	offices: Sequence[Office]
	workers: Sequence[Sequence[int]]
	distances: Sequence[Sequence[float]]
	maxDifference: int
	balanceCoefficient: float

	@model_validator(mode='after')
	def _check_shapes(self):
		n_o, n_s = len(self.offices), len(self.sites)
		if len(self.distances) != n_o:
			raise ValueError(f"'distances' has {len(self.distances)} entries, expected one per office ({n_o})")
		for distance in self.distances:
			if len(distance) != n_s:
				raise ValueError(f"'distances' has {len(distance)} entries, expected one per site ({n_s})")
		return self

	@classmethod
	def load(cls, path: str | Path):
		with open(path, encoding='utf-8') as f:
			return cls.model_validate(json.load(f))


class OpenedOffice(BaseModel):
	model_config = ConfigDict(populate_by_name=True)

	office_id: int = Field(alias='officeId')
	sites: Sequence[int]
	workers: Sequence[int]


def load_solution(path: Path):
	with open(path, encoding='utf-8') as f:
		data = json.load(f)
	return TypeAdapter(list[OpenedOffice]).validate_python(data)


def sites_in_hull(site_coords: Sequence[Point], selected: Sequence[int], tol: float = 0.0):
	hull = MultiPoint([site_coords[i] for i in selected]).convex_hull
	if tol > 0.0:
		hull = hull.buffer(tol)
	return (i for i, point in enumerate(site_coords) if hull.covers(point))


def _office_task_count(instance: Instance, office: OpenedOffice):
	return sum(len(instance.sites[site].tasks) for site in office.sites)


def _validate_indices(instance: Instance, solution: Iterable[OpenedOffice]):
	n_o, n_s, n_w = len(instance.offices), len(instance.sites), len(instance.workers)
	opened: set[int] = set()
	sites: set[int] = set()
	workers: set[int] = set()
	for office in solution:
		if not (0 <= office.office_id < n_o):
			return 'Invalid office index'
		if office.office_id in opened:
			return 'Duplicate office'
		opened.add(office.office_id)
		for site in office.sites:
			if not (0 <= site < n_s):
				return 'Invalid site index'
			if site in sites:
				return 'Duplicate site'
			sites.add(site)
		for worker in office.workers:
			if not (0 <= worker < n_w):
				return 'Invalid worker index'
			if worker in workers:
				return 'Duplicate worker'
			workers.add(worker)
	return None


def _validate_capacity(instance: Instance, solution: Iterable[OpenedOffice]):
	for office in solution:
		if len(office.sites) > instance.offices[office.office_id].maxSize:
			return 'Office capacity exceeded'
	return None


def _validate_balance(instance: Instance, solution: Sequence[OpenedOffice]):
	for office1 in solution:
		for office2 in solution:
			if abs(len(office1.sites) - len(office2.sites)) > instance.maxDifference:
				return 'Site-count imbalance'
	return None


def _validate_skills(instance: Instance, solution: Iterable[OpenedOffice]):
	for office in solution:
		for site in office.sites:
			for task in instance.sites[site].tasks:
				if not any(task in instance.workers[worker] for worker in office.workers):
					return 'Missing skills'
	return None


def _validate_geometry(instance: Instance, solution: Iterable[OpenedOffice]):
	site_coords = [Point(site.x, site.y) for site in instance.sites]
	office_coords = [Point(office.x, office.y) for office in instance.offices]
	n_sites = len(site_coords)
	for office in solution:
		for other in solution:
			if office != other:
				hull1 = sites_in_hull([*site_coords, *office_coords], [*office.sites, n_sites + office.office_id])
				hull2 = sites_in_hull([*site_coords, *office_coords], [*other.sites, n_sites + other.office_id])
				if not set(hull1).isdisjoint(set(hull2)):
					return 'Hull conflict'
	return None


def _compute_objective(instance: Instance, solution: Sequence[OpenedOffice]):
	reward = sum(instance.sites[site].reward for office in solution for site in office.sites)
	distance = sum(instance.distances[office.office_id][site] for office in solution for site in office.sites)
	cost = sum(instance.offices[office.office_id].cost for office in solution)
	if solution:
		task_loads = [_office_task_count(instance, office) for office in solution]
		imbalance = max(task_loads) - min(task_loads)
	else:
		imbalance = 0
	return reward - distance - cost - instance.balanceCoefficient * imbalance


def validate_solution(instance: Instance, solution: Sequence[OpenedOffice]):
	for func in (
		_validate_indices,
		_validate_capacity,
		_validate_balance,
		_validate_skills,
		_validate_geometry,
	):
		if error := func(instance, solution):
			return error
	return _compute_objective(instance, solution)


def validate(instance_path: Path, solution_path: Path):
	try:
		instance = Instance.load(instance_path)
		solution = load_solution(solution_path)
	except FileNotFoundError:
		return 'File not found'
	except json.JSONDecodeError:
		return 'Invalid JSON'
	except ValidationError:
		return 'Validation error'
	return validate_solution(instance, solution)


def main():
	parser = argparse.ArgumentParser(description='Validate a JSON solution')
	parser.add_argument('instance', type=Path, help='path to the instance JSON file')
	parser.add_argument('solution', type=Path, help='path to the solution JSON file')
	args = parser.parse_args()

	result = validate(args.instance, args.solution)
	if isinstance(result, str):
		print('FAIL')
	print(result)


if __name__ == '__main__':
	main()
