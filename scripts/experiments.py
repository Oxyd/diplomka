#!/usr/bin/env python3

from pathlib import Path
import argparse
import json
import subprocess
import sys
import threading
import queue

configs = {
  '100-0.05': (100, 0.05),
  '200-0.05': (200, 0.05),
  '500-0.05': (500, 0.05),

  '100-0.01': (100, 0.01),
  '200-0.01': (200, 0.01),
  '500-0.01': (500, 0.01),

  '100-0.1': (100, 0.1),
  '200-0.1': (200, 0.1),
  '500-0.1': (500, 0.1)
}

def make_scenario(map_info, map_path, num_agents, obstacles_prob):
  '''Create a scenario for the given map.'''

  tiles = int(map_info['passable_tiles'])
  if tiles > 20000: return None

  return {
    'map': str(map_path),
    'obstacles': {
      'mode': 'random',
      'tile_probability': obstacles_prob,
      'obstacle_movement': {
        'move_probability': {
          'distribution': 'normal',
          'parameters': [5, 1]
        }
      }
    },
    'agent_settings': {
      'random_agents': min(int(num_agents), tiles)
    },
    'agents': []
  }

def make_relative(path, relative_to):
  '''Make path relative to the other given path.'''

  path = path.resolve().parts
  relative_to = relative_to.resolve().parts

  i = 0
  while i < max(len(path), len(relative_to)) and path[i] == relative_to[i]:
    i += 1

  result = Path()
  for _ in range(i, len(relative_to)): result /= '..'

  return result.joinpath(*path[i :])

def substitute_scenario(solver_args, scenario_path):
  '''Replace {} in solver_args with scenario_path and return the resulting
  list.
  '''

  def subst(arg):
    if arg == '{}': return str(scenario_path)
    else: return arg

  return list(map(subst, solver_args))

jobs = queue.Queue()
timeout = None

def worker():
  '''Worker thread that will run experiments off the job queue.'''

  while True:
    item = jobs.get()
    if item is None: break

    (name, args, result_path, info) = item

    print('{} ...'.format(name))

    try:
      result = subprocess.run(args,
                              stdout=subprocess.PIPE,
                              universal_newlines=True,
                              timeout=timeout)
      result_data = json.loads(result.stdout)
      completed = True

      print('... {} done'.format(name))
    except subprocess.TimeoutExpired:
      print('... {} timed out '.format(name))

      result_data = {}
      completed = False

    result_path.open(mode='w').write(json.dumps(
      {'map_info': info, 'result': result_data, 'completed': completed},
      indent=2
    ))

    jobs.task_done()


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('maps', help='Path to maps to run experiments on')
  parser.add_argument('scenarios',
                      help='Where to put resulting scenarios')
  parser.add_argument('agents', help='Number of agents to place')
  parser.add_argument('obstacles',
                      help='Percent of tiles to fill with obstacles')
  parser.add_argument('solver', nargs=argparse.REMAINDER, metavar='solver args',
                      help='Solver invokation command line. {} is replaced by '
                      + 'the scenario path')
  parser.add_argument('--timeout', type=float,
                      help='Time, in seconds, to run the solver for')
  parser.add_argument('--threads', type=int, default=1,
                      help='How many threads to use for running the experiments')
  parser.add_argument('--dry', action='store_true')

  args = parser.parse_args()

  maps = Path(args.maps)
  num_agents = args.agents
  obstacle_prob = args.obstacles
  scenarios = Path(args.scenarios)
  solver_args = args.solver
  dry = args.dry

  global timeout
  if 'timeout' in args:
    timeout = args.timeout
  else:
    timeout = None

  for f in maps.glob('*.json'):
    map_path = f.parent / (f.stem + '.map')
    if not map_path.exists():
      print('No matching map file for {}'.format(f.name))
      continue

    info = json.load(f.open())

    if not info['connected']:
      print('{} not connected; skipping'.format(f.stem))
      continue

    conf_name = '{}-agents-{}-obst'.format(num_agents, obstacle_prob)
    scenario_dir = scenarios / conf_name
    scenario_dir.mkdir(parents=True, exist_ok=True)
    scenario = make_scenario(info, make_relative(map_path, scenario_dir),
                             num_agents, obstacle_prob)
    if scenario is None: continue

    scenario_path = scenario_dir / (f.stem + '.json')
    scenario_path.open(mode='w').write(json.dumps(scenario, indent=2))
    result_path = scenario_dir / (f.stem + '.result.json')

    jobs.put((f.stem + ' ' + conf_name,
              substitute_scenario(solver_args, scenario_path.resolve()),
              result_path,
              info))

  if dry:
    return

  print('Starting worker threads')

  threads = [threading.Thread(target=worker) for _ in range(args.threads)]
  for t in threads: t.start()

  jobs.join()

  for _ in range(args.threads): jobs.put(None)
  for t in threads: t.join()

if __name__ == '__main__':
  main()
