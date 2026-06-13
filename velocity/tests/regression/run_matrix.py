#!/usr/bin/env python3
import argparse
import concurrent.futures
import csv
import json
import os
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path


ANSI = {
    'reset': '\033[0m',
    'bold': '\033[1m',
    'dim': '\033[2m',
    'cyan': '\033[36m',
    'green': '\033[32m',
    'yellow': '\033[33m',
    'red': '\033[31m',
    'magenta': '\033[35m',
    'blue': '\033[34m',
}


def repo_root_from_script():
    return Path(__file__).resolve().parents[3]


def load_json(path):
    with path.open() as file:
        return json.load(file)


def select_entries(manifest, mode, key, requested):
    entries = manifest[key]
    names = requested if requested else manifest[mode]
    missing = [name for name in names if name not in entries]
    if missing:
        raise ValueError('Unknown {}: {}'.format(key, ', '.join(missing)))
    return [(name, entries[name]) for name in names]


def select_topologies(manifest, mode, requested):
    overrides = manifest.get('topologies', {})
    names = requested if requested else manifest[mode]
    selected = []
    missing = []

    for name in names:
        generated = None
        try:
            generated = parse_topology_name(name)
        except ValueError:
            pass

        if name in overrides:
            if generated is None:
                generated = {}
            generated = merge_topology(generated, overrides[name])

        if generated is None or 'topology' not in generated or 'clusters' not in generated:
            missing.append(name)
            continue

        selected.append((name, generated))

    if missing:
        raise ValueError('Unknown topologies: {}'.format(', '.join(missing)))
    return selected


def merge_topology(base, override):
    merged = dict(base)
    attrs = dict(base.get('attrs', {}))
    for key, value in override.items():
        if key == 'attrs':
            attrs.update(value)
        else:
            merged[key] = value
    if attrs:
        merged['attrs'] = attrs
    return merged


def parse_positive_int(text, name):
    value = int(text)
    if value <= 0:
        raise ValueError('{} must be > 0'.format(name))
    return value


def product(values):
    result = 1
    for value in values:
        result *= value
    return result


def capacity_with_optional_clusters(config, capacity, clusters):
    if clusters is None:
        config['clusters'] = capacity
    else:
        active_clusters = parse_positive_int(clusters, 'clusters')
        if active_clusters > capacity:
            raise ValueError('clusters {} exceed topology capacity {}'.format(active_clusters, capacity))
        config['clusters'] = active_clusters
    return config


def routing_attrs(topology_name, routing_token):
    if routing_token is None:
        return {}
    if routing_token == 'wrap':
        return {'unified_interco_{}_routing'.format(topology_name): 'wrap_minimal'}
    if routing_token in ('tree', 'minimal', 'wrap_tree', 'wrap_minimal'):
        return {'unified_interco_{}_routing'.format(topology_name): routing_token}
    raise ValueError('unsupported routing suffix {}'.format(routing_token))


def parse_topology_name(name):
    patterns = [
        (r'^flat_C(?P<c>\d+)$', parse_flat_name),
        (r'^fat_tree_K(?P<k>\d+)_L(?P<l>\d+)(?:_C(?P<c>\d+))?$', parse_fat_tree_name),
        (r'^mesh_2d_X(?P<x>\d+)_Y(?P<y>\d+)(?:_C(?P<c>\d+))?$', parse_mesh_2d_name),
        (r'^mesh_3d_X(?P<x>\d+)_Y(?P<y>\d+)_Z(?P<z>\d+)(?:_C(?P<c>\d+))?$', parse_mesh_3d_name),
        (r'^torus_2d_X(?P<x>\d+)_Y(?P<y>\d+)(?:_(?P<routing>wrap|tree|minimal|wrap_tree|wrap_minimal))?(?:_C(?P<c>\d+))?$', parse_torus_2d_name),
        (r'^torus_3d_X(?P<x>\d+)_Y(?P<y>\d+)_Z(?P<z>\d+)(?:_(?P<routing>wrap|tree|minimal|wrap_tree|wrap_minimal))?(?:_C(?P<c>\d+))?$', parse_torus_3d_name),
        (r'^ruche_2d_X(?P<x>\d+)_Y(?P<y>\d+)_H(?P<h>\d+)(?:_C(?P<c>\d+))?$', parse_ruche_2d_name),
        (r'^ruche_3d_X(?P<x>\d+)_Y(?P<y>\d+)_Z(?P<z>\d+)_H(?P<h>\d+)(?:_C(?P<c>\d+))?$', parse_ruche_3d_name),
        (r'^hexa_mesh_X(?P<x>\d+)_Y(?P<y>\d+)(?:_C(?P<c>\d+))?$', parse_hexa_mesh_name),
        (r'^hexa_torus_X(?P<x>\d+)_Y(?P<y>\d+)(?:_(?P<routing>wrap|tree|minimal|wrap_tree|wrap_minimal))?(?:_C(?P<c>\d+))?$', parse_hexa_torus_name),
        (r'^octa_mesh_X(?P<x>\d+)_Y(?P<y>\d+)(?:_C(?P<c>\d+))?$', parse_octa_mesh_name),
        (r'^octa_torus_X(?P<x>\d+)_Y(?P<y>\d+)(?:_(?P<routing>wrap|tree|minimal|wrap_tree|wrap_minimal))?(?:_C(?P<c>\d+))?$', parse_octa_torus_name),
        (r'^ring_N(?P<n>\d+)(?:_(?P<routing>wrap|tree|minimal|wrap_tree|wrap_minimal))?(?:_C(?P<c>\d+))?$', parse_ring_name),
        (r'^tree_R(?P<r>\d+)_L(?P<l>\d+)(?:_C(?P<c>\d+))?$', parse_tree_name),
        (r'^dragonfly_G(?P<g>\d+)_A(?P<a>\d+)_P(?P<p>\d+)(?:_C(?P<c>\d+))?$', parse_dragonfly_name),
        (r'^hypercube_D(?P<d>\d+)(?:_C(?P<c>\d+))?$', parse_hypercube_name),
    ]

    for pattern, parser in patterns:
        match = re.match(pattern, name)
        if match:
            return parser(match.groupdict())
    raise ValueError('unsupported topology name {}'.format(name))


def parse_flat_name(groups):
    clusters = parse_positive_int(groups['c'], 'clusters')
    return {'topology': None, 'clusters': clusters}


def parse_fat_tree_name(groups):
    radix = parse_positive_int(groups['k'], 'fat-tree K')
    level = parse_positive_int(groups['l'], 'fat-tree L')
    down_ports = radix if level == 1 else (radix + 1) // 2
    capacity = radix if level == 1 else radix * (down_ports ** (level - 1))
    config = {
        'topology': 'fat_tree',
        'attrs': {
            'unified_interco_tree_radix': radix,
            'unified_interco_tree_level': level,
        },
    }
    return capacity_with_optional_clusters(config, capacity, groups.get('c'))


def coordinate_config(topology, dims, clusters=None, attrs=None, routing=None):
    for index, value in enumerate(dims):
        if value <= 0:
            raise ValueError('{} dimension {} must be > 0'.format(topology, index))
    config = {
        'topology': topology,
        'attrs': {
            'unified_interco_dims': list(dims),
        },
    }
    config['attrs'].update(attrs or {})
    config['attrs'].update(routing_attrs(topology, routing))
    return capacity_with_optional_clusters(config, product(dims), clusters)


def parse_mesh_2d_name(groups):
    return coordinate_config(
        'mesh_2d',
        (parse_positive_int(groups['x'], 'X'), parse_positive_int(groups['y'], 'Y')),
        groups.get('c'),
    )


def parse_mesh_3d_name(groups):
    return coordinate_config(
        'mesh_3d',
        (
            parse_positive_int(groups['x'], 'X'),
            parse_positive_int(groups['y'], 'Y'),
            parse_positive_int(groups['z'], 'Z'),
        ),
        groups.get('c'),
    )


def parse_torus_2d_name(groups):
    return coordinate_config(
        'torus_2d',
        (parse_positive_int(groups['x'], 'X'), parse_positive_int(groups['y'], 'Y')),
        groups.get('c'),
        routing=groups.get('routing'),
    )


def parse_torus_3d_name(groups):
    return coordinate_config(
        'torus_3d',
        (
            parse_positive_int(groups['x'], 'X'),
            parse_positive_int(groups['y'], 'Y'),
            parse_positive_int(groups['z'], 'Z'),
        ),
        groups.get('c'),
        routing=groups.get('routing'),
    )


def parse_ruche_2d_name(groups):
    hop = parse_positive_int(groups['h'], 'H')
    return coordinate_config(
        'ruche_2d',
        (parse_positive_int(groups['x'], 'X'), parse_positive_int(groups['y'], 'Y')),
        groups.get('c'),
        attrs={'unified_interco_hop': hop},
    )


def parse_ruche_3d_name(groups):
    hop = parse_positive_int(groups['h'], 'H')
    return coordinate_config(
        'ruche_3d',
        (
            parse_positive_int(groups['x'], 'X'),
            parse_positive_int(groups['y'], 'Y'),
            parse_positive_int(groups['z'], 'Z'),
        ),
        groups.get('c'),
        attrs={'unified_interco_hop': hop},
    )


def parse_hexa_mesh_name(groups):
    return coordinate_config(
        'hexa_mesh',
        (parse_positive_int(groups['x'], 'X'), parse_positive_int(groups['y'], 'Y')),
        groups.get('c'),
    )


def parse_hexa_torus_name(groups):
    return coordinate_config(
        'hexa_torus',
        (parse_positive_int(groups['x'], 'X'), parse_positive_int(groups['y'], 'Y')),
        groups.get('c'),
        routing=groups.get('routing'),
    )


def parse_octa_mesh_name(groups):
    return coordinate_config(
        'octa_mesh',
        (parse_positive_int(groups['x'], 'X'), parse_positive_int(groups['y'], 'Y')),
        groups.get('c'),
    )


def parse_octa_torus_name(groups):
    return coordinate_config(
        'octa_torus',
        (parse_positive_int(groups['x'], 'X'), parse_positive_int(groups['y'], 'Y')),
        groups.get('c'),
        routing=groups.get('routing'),
    )


def parse_ring_name(groups):
    size = parse_positive_int(groups['n'], 'N')
    config = {
        'topology': 'ring',
        'attrs': {
            'unified_interco_ring_size': size,
        },
    }
    config['attrs'].update(routing_attrs('ring', groups.get('routing')))
    return capacity_with_optional_clusters(config, size, groups.get('c'))


def parse_tree_name(groups):
    radix = parse_positive_int(groups['r'], 'R')
    level = parse_positive_int(groups['l'], 'L')
    config = {
        'topology': 'tree',
        'attrs': {
            'unified_interco_tree_radix': radix,
            'unified_interco_tree_level': level,
        },
    }
    return capacity_with_optional_clusters(config, radix ** level, groups.get('c'))


def parse_dragonfly_name(groups):
    groups_count = parse_positive_int(groups['g'], 'G')
    routers_per_group = parse_positive_int(groups['a'], 'A')
    terminals_per_router = parse_positive_int(groups['p'], 'P')
    capacity = groups_count * routers_per_group * terminals_per_router
    config = {
        'topology': 'dragonfly',
        'attrs': {
            'unified_interco_dragonfly_groups': groups_count,
            'unified_interco_dragonfly_routers_per_group': routers_per_group,
            'unified_interco_dragonfly_terminals_per_router': terminals_per_router,
        },
    }
    return capacity_with_optional_clusters(config, capacity, groups.get('c'))


def parse_hypercube_name(groups):
    dims = parse_positive_int(groups['d'], 'D')
    config = {
        'topology': 'hypercube',
        'attrs': {
            'unified_interco_hypercube_dims': dims,
        },
    }
    return capacity_with_optional_clusters(config, 1 << dims, groups.get('c'))


def py_literal(value):
    if isinstance(value, str):
        return repr(value)
    if value is None:
        return 'None'
    if isinstance(value, bool):
        return 'True' if value else 'False'
    if isinstance(value, (int, float)):
        return str(value)
    if isinstance(value, list):
        return '[' + ', '.join(py_literal(item) for item in value) + ']'
    if isinstance(value, dict):
        items = ', '.join('{}: {}'.format(py_literal(key), py_literal(val)) for key, val in value.items())
        return '{' + items + '}'
    raise TypeError('Unsupported architecture value: {!r}'.format(value))


def arch_assignment(name, value, width=52):
    return '        self.{:<{}} = {}'.format(name, width, value)


def write_arch(path, topology_name, topology):
    attrs = topology.get('attrs', {})
    topology_kind = topology.get('topology')
    clusters = topology['clusters']

    unified_interco = 'None' if topology_kind is None else repr(topology_kind)
    unified_topology = repr(topology_kind or 'flat')

    lines = [
        'class VelocityArch:',
        '',
        '    def __init__(self):',
        arch_assignment('num_cluster', clusters),
        arch_assignment('cluster_num_lane', 32),
        arch_assignment('cluster_lane_width', 4),
        arch_assignment('dma_reg_offset', '0x00000100'),
        arch_assignment('dma_reg_size', '0x00000100'),
        arch_assignment('dma_bus_width', 16),
        arch_assignment('dma_read_buffer_size', 4096),
        arch_assignment('dma_write_buffer_size', 4096),
        arch_assignment('dma_max_inflight_txn', 16),
        arch_assignment('dma_base_latency', 1),
        arch_assignment('dma_cluster_stride', '0x00010000'),
        arch_assignment('unified_interco', unified_interco),
        arch_assignment('unified_interco_topology', unified_topology),
        arch_assignment('unified_interco_link_latency', 1),
        arch_assignment('unified_interco_link_width', 'self.dma_bus_width'),
        arch_assignment('unified_interco_link_pending_size', 'self.dma_write_buffer_size'),
        arch_assignment('unified_interco_router_pending_size', 'self.dma_write_buffer_size'),
    ]

    for attr_name, attr_value in attrs.items():
        lines.append(arch_assignment(attr_name, py_literal(attr_value)))

    lines.extend([
        arch_assignment('cluster_tcdm_base', '0x00000000'),
        arch_assignment('cluster_tcdm_size', '0x00100000'),
        arch_assignment('cluster_stack_base', '0x10000000'),
        arch_assignment('cluster_stack_size', '0x00020000'),
        arch_assignment('cluster_zomem_base', '0x18000000'),
        arch_assignment('cluster_zomem_size', '0x00020000'),
        arch_assignment('cluster_reg_base', '0x20000000'),
        arch_assignment('cluster_reg_size', '0x00000200'),
        arch_assignment('instruction_mem_base', '0x80000000'),
        arch_assignment('instruction_mem_size', '0x00010000'),
        arch_assignment('soc_register_base', '0x70000000'),
        arch_assignment('soc_register_size', '0x00010000'),
        arch_assignment('soc_register_eoc', '0x70000000'),
        '',
    ])

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('\n'.join(lines))


def env_prologue(repo_root):
    ccache = shlex.quote(str(repo_root / '.ccache'))
    return '\n'.join([
        'set -e',
        'export CCACHE_DIR="${CCACHE_DIR:-' + ccache + '}"',
        'mkdir -p "${CCACHE_DIR}"',
    ])


def run_shell(command, repo_root, log_path, timeout):
    log_path.parent.mkdir(parents=True, exist_ok=True)
    script = env_prologue(repo_root) + '\n' + command + '\n'

    with log_path.open('w', errors='replace') as log:
        log.write('[matrix] command:\n')
        log.write(command)
        log.write('\n\n')
        log.flush()
        proc = subprocess.run(
            ['bash', '-c', script],
            cwd=str(repo_root),
            stdout=log,
            stderr=subprocess.STDOUT,
            timeout=timeout,
        )
        return proc.returncode


def build_hardware(topology_name, arch_path, repo_root, log_dir, timeout):
    log_path = log_dir / '{}__hw.log'.format(topology_name)
    command = 'make hw cfg={}'.format(shlex.quote(str(arch_path)))
    start = time.time()
    try:
        returncode = run_shell(command, repo_root, log_path, timeout)
        status = 'PASS' if returncode == 0 else 'HW_FAIL'
    except subprocess.TimeoutExpired:
        returncode = -1
        status = 'HW_TIMEOUT'
    return {
        'status': status,
        'returncode': returncode,
        'seconds': '{:.1f}'.format(time.time() - start),
        'log': str(log_path),
    }


def cmake_arg(test, app_path):
    args = ['-DSRC_DIR={}'.format(app_path)]
    for key, value in test.get('cmake_defs', {}).items():
        args.append('-D{}={}'.format(key, value))
    return ' '.join(args)


def run_test_cell(test_name, test, topology_name, repo_root, run_dir, timeout, sim_lock, queued_commands, run_target):
    app_path = repo_root / test['app']
    build_dir = run_dir / 'sw_builds' / topology_name / test_name
    log_path = run_dir / 'logs' / '{}__{}.log'.format(topology_name, test_name)
    cmake = cmake_arg(test, app_path)
    target = run_target or test.get('run_target', 'run')
    if target not in ('run', 'rund'):
        raise ValueError('Unsupported run target for {}: {}'.format(test_name, target))
    run_command = 'timeout {} make {} sw_build_dir={}'.format(
        int(timeout),
        target,
        shlex.quote(str(build_dir)),
    )
    if sim_lock is not None:
        sim_lock.parent.mkdir(parents=True, exist_ok=True)
        run_command = 'flock {} {}'.format(shlex.quote(str(sim_lock)), run_command)

    command = '\n'.join([
        'make sw app={} sw_build_dir={} sw_cmake_arg={}'.format(
            shlex.quote(str(app_path)),
            shlex.quote(str(build_dir)),
            shlex.quote(cmake),
        ),
        run_command,
    ])

    start = time.time()
    try:
        returncode = run_shell(command, repo_root, log_path, timeout * queued_commands)
        text = Path(log_path).read_text(errors='replace')
        passed = returncode == 0 and re.search(test['pass_regex'], text) is not None
        status = 'PASS' if passed else 'FAIL'
    except subprocess.TimeoutExpired:
        returncode = -1
        status = 'TIMEOUT'

    return {
        'test': test_name,
        'topology': topology_name,
        'suite': test.get('suite', ''),
        'status': status,
        'returncode': returncode,
        'seconds': '{:.1f}'.format(time.time() - start),
        'comment': test.get('comment', ''),
        'log': str(log_path),
    }


def color_enabled(mode):
    if mode == 'always':
        return True
    if mode == 'never':
        return False
    return sys.stdout.isatty()


def colorize(enabled, code, text):
    if not enabled:
        return text
    return ANSI[code] + text + ANSI['reset']


def truncate(value, width):
    if len(value) <= width:
        return value
    if width <= 1:
        return value[:width]
    return value[:width - 1] + '~'


class ProgressDisplay:
    def __init__(self, tests, topologies, mode, color_mode):
        if mode == 'auto':
            mode = 'bar' if sys.stdout.isatty() else 'line'
        self.mode = mode
        self.use_color = color_enabled(color_mode)
        self.tests = [name for name, _ in tests]
        self.total = len(topologies)
        self.rows = {}
        self.rendered = False
        for name in self.tests:
            self.rows[name] = {
                'current': '-',
                'status': 'WAIT',
                'done': 0,
                'pass': 0,
                'fail': 0,
            }

    def start_hardware(self, topology_name):
        for test_name in self.tests:
            row = self.rows[test_name]
            row['current'] = topology_name
            row['status'] = 'HW'
        self.render()

    def start_test(self, test_name, topology_name):
        row = self.rows[test_name]
        row['current'] = topology_name
        row['status'] = 'RUN'
        self.render()

    def finish_test(self, test_name, topology_name, status):
        row = self.rows[test_name]
        row['current'] = topology_name
        row['done'] += 1
        if status == 'PASS':
            row['pass'] += 1
            row['status'] = 'PASS'
        else:
            row['fail'] += 1
            row['status'] = status
        self.render()

    def finish(self):
        if self.mode == 'bar' and self.rendered:
            sys.stdout.write('\n')
            sys.stdout.flush()

    def progress_bar(self, done, width):
        filled = int((done * width) / self.total) if self.total else width
        empty = width - filled
        bar = '#' * filled + '-' * empty
        if not self.use_color:
            return '[' + bar + ']'
        if done == self.total:
            code = 'green'
        elif done == 0:
            code = 'yellow'
        else:
            code = 'blue'
        return '[' + colorize(self.use_color, code, bar) + ']'

    def format_line(self, test_name):
        row = self.rows[test_name]
        pct = int((row['done'] * 100) / self.total) if self.total else 100
        test_text = colorize(self.use_color, 'cyan', truncate(test_name, 22).ljust(22))
        topo_text = colorize(self.use_color, 'magenta', truncate(row['current'], 28).ljust(28))
        status_code = 'green' if row['status'] == 'PASS' else 'red' if row['status'] not in ('WAIT', 'HW', 'RUN') else 'yellow'
        status_text = colorize(self.use_color, status_code, row['status'].ljust(8))
        pass_text = colorize(self.use_color, 'green', 'PASS:{}'.format(row['pass']))
        fail_text = colorize(self.use_color, 'red', 'FAIL:{}'.format(row['fail']))
        return '[{}] [{}] {} {:3d}% [{} | {}] {}'.format(
            test_text,
            topo_text,
            self.progress_bar(row['done'], 28),
            pct,
            pass_text,
            fail_text,
            status_text,
        )

    def render(self):
        if self.mode == 'off':
            return
        lines = [self.format_line(test_name) for test_name in self.tests]
        if self.mode == 'bar':
            if self.rendered:
                sys.stdout.write('\033[{}A'.format(len(lines)))
            for line in lines:
                sys.stdout.write('\r\033[K' + line + '\n')
            self.rendered = True
            sys.stdout.flush()
            return

        for line in lines:
            sys.stdout.write(line + '\n')
        sys.stdout.flush()


def write_detail(rows, path):
    fields = ['test', 'topology', 'suite', 'status', 'returncode', 'seconds', 'comment', 'log']
    with path.open('w', newline='') as file:
        writer = csv.DictWriter(file, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def write_matrix(rows, tests, topologies, path):
    by_cell = {(row['test'], row['topology']): row['status'] for row in rows}
    fields = ['test'] + [name for name, _ in topologies]
    with path.open('w', newline='') as file:
        writer = csv.DictWriter(file, fieldnames=fields)
        writer.writeheader()
        for test_name, _ in tests:
            row = {'test': test_name}
            for topology_name, _ in topologies:
                row[topology_name] = by_cell.get((test_name, topology_name), 'NOT_RUN')
            writer.writerow(row)


def sort_rows(rows, tests, topologies):
    test_order = {name: index for index, (name, _) in enumerate(tests)}
    topology_order = {name: index for index, (name, _) in enumerate(topologies)}
    return sorted(rows, key=lambda row: (test_order[row['test']], topology_order[row['topology']]))


def main():
    default_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description='Run topology-by-test Velocity matrix regressions.')
    parser.add_argument('--mode', choices=['smoke', 'full', 'debug'], default='smoke')
    parser.add_argument('--jobs', type=int, default=0, help='Parallel test-type workers. 0 means one worker per selected test row.')
    parser.add_argument('--timeout', type=int, default=1800, help='Per simulator command timeout in seconds')
    parser.add_argument('--topology', action='append', help='Run one topology column; may be repeated')
    parser.add_argument('--test', action='append', help='Run one test row; may be repeated')
    parser.add_argument('--topologies', type=Path, default=default_dir / 'topologies.json')
    parser.add_argument('--tests', type=Path, default=default_dir / 'tests.json')
    parser.add_argument('--results', type=Path, default=default_dir / 'results')
    parser.add_argument('--progress', choices=['auto', 'bar', 'line', 'off'], default='auto')
    parser.add_argument('--color', choices=['auto', 'always', 'never'], default='auto')
    parser.add_argument('--run-target', choices=['run', 'rund'], help='Override the make target used for simulator execution')
    parser.add_argument(
        '--no-sim-lock',
        action='store_true',
        help='Allow concurrent simulator invocations. By default gvsoc runs are serialized because the generated target is shared.',
    )
    args = parser.parse_args()

    repo_root = repo_root_from_script()
    topology_manifest = load_json(args.topologies)
    test_manifest = load_json(args.tests)
    topologies = select_topologies(topology_manifest, args.mode, args.topology)
    tests = select_entries(test_manifest, args.mode, 'tests', args.test)
    row_workers = len(tests) if args.jobs <= 0 else max(1, min(args.jobs, len(tests)))
    queued_commands = max(2, len(tests) + 1)

    timestamp = time.strftime('%Y%m%d_%H%M%S')
    run_dir = args.results / 'matrix' / args.mode / timestamp
    arch_dir = run_dir / 'arch'
    log_dir = run_dir / 'logs'
    sim_lock = None if args.no_sim_lock else run_dir / 'locks' / 'gvsoc.lock'
    run_dir.mkdir(parents=True, exist_ok=True)
    log_dir.mkdir(parents=True, exist_ok=True)

    progress = ProgressDisplay(tests, topologies, args.progress, args.color)
    progress.render()

    rows = []
    for topology_name, topology in topologies:
        arch_path = arch_dir / 'velocity_arch_{}.py'.format(topology_name)
        write_arch(arch_path, topology_name, topology)
        progress.start_hardware(topology_name)
        hw = build_hardware(topology_name, arch_path, repo_root, log_dir, args.timeout)

        if hw['status'] != 'PASS':
            for test_name, test in tests:
                rows.append({
                    'test': test_name,
                    'topology': topology_name,
                    'suite': test.get('suite', ''),
                    'status': hw['status'],
                    'returncode': hw['returncode'],
                    'seconds': hw['seconds'],
                    'comment': topology.get('comment', ''),
                    'log': hw['log'],
                })
                progress.finish_test(test_name, topology_name, hw['status'])
            continue

        with concurrent.futures.ThreadPoolExecutor(max_workers=row_workers) as executor:
            futures = {}
            for test_name, test in tests:
                progress.start_test(test_name, topology_name)
                future = executor.submit(
                    run_test_cell,
                    test_name,
                    test,
                    topology_name,
                    repo_root,
                    run_dir,
                    args.timeout,
                    sim_lock,
                    queued_commands,
                    args.run_target,
                )
                futures[future] = test_name

            for future in concurrent.futures.as_completed(futures):
                row = future.result()
                rows.append(row)
                progress.finish_test(row['test'], row['topology'], row['status'])

    progress.finish()

    rows = sort_rows(rows, tests, topologies)
    detail = run_dir / 'detail.csv'
    matrix = run_dir / 'matrix.csv'
    write_detail(rows, detail)
    write_matrix(rows, tests, topologies, matrix)

    failed = [row for row in rows if row['status'] != 'PASS']
    print('[matrix] detail={}'.format(detail))
    print('[matrix] matrix={}'.format(matrix))
    print('[matrix] passed={} failed={}'.format(len(rows) - len(failed), len(failed)))
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
