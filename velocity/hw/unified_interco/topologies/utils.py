import math


def sanitize_name(name: str) -> str:
    return name.lower().replace('-', '_').replace(' ', '_')


def topology_attr_key(name: str) -> str:
    name = sanitize_name(name)
    if name.startswith('2d_'):
        return name[3:] + '_2d'
    if name.startswith('3d_'):
        return name[3:] + '_3d'
    if name.startswith('h_hop_'):
        return name[6:]
    return name


def get_int_attr(arch, names: list[str], default: int | None = None) -> int:
    for name in names:
        if hasattr(arch, name):
            value = getattr(arch, name)
            if isinstance(value, bool) or not isinstance(value, int):
                raise ValueError(f'Architecture attribute {name} must be an integer')
            return value
    if default is None:
        raise ValueError(f'Missing required architecture attribute, tried: {", ".join(names)}')
    return default


def get_bool_attr(arch, names: list[str], default: bool = False) -> bool:
    for name in names:
        if hasattr(arch, name):
            value = getattr(arch, name)
            if not isinstance(value, bool):
                raise ValueError(f'Architecture attribute {name} must be a bool')
            return value
    return default


def get_dims(arch, topology_name: str, ndim: int, default: tuple[int, ...] | None = None) -> tuple[int, ...]:
    key = topology_attr_key(topology_name)
    family = key.rsplit('_', 1)[0] if key.endswith(('_2d', '_3d')) else key
    candidates = [
        f'unified_interco_{key}_dims',
        f'unified_interco_{family}_dims',
        'unified_interco_dims',
    ]

    for name in candidates:
        if hasattr(arch, name):
            return normalize_dims(getattr(arch, name), ndim, name)

    dim_names = ['x', 'y', 'z', 'w']
    scalar_candidates = [f'unified_interco_dim_{axis}' for axis in dim_names[:ndim]]
    if all(hasattr(arch, name) for name in scalar_candidates):
        return normalize_dims([getattr(arch, name) for name in scalar_candidates], ndim, 'unified_interco_dim_*')

    if default is not None:
        return normalize_dims(default, ndim, 'default')

    return infer_dims(arch.num_cluster, ndim)


def normalize_dims(value, ndim: int, attr_name: str) -> tuple[int, ...]:
    if not isinstance(value, (list, tuple)):
        raise ValueError(f'Architecture attribute {attr_name} must be a list or tuple')
    if len(value) != ndim:
        raise ValueError(f'Architecture attribute {attr_name} must have {ndim} dimensions, got {len(value)}')

    dims = []
    for dim in value:
        if isinstance(dim, bool) or not isinstance(dim, int) or dim <= 0:
            raise ValueError(f'Architecture attribute {attr_name} must contain positive integers')
        dims.append(dim)
    return tuple(dims)


def infer_dims(num_cluster: int, ndim: int) -> tuple[int, ...]:
    if num_cluster <= 0:
        raise ValueError('Cannot infer topology dimensions for num_cluster <= 0')
    if ndim <= 0:
        raise ValueError('Cannot infer topology dimensions for ndim <= 0')

    dims = [1] * ndim
    while product(dims) < num_cluster:
        index = min(range(ndim), key=lambda dim: dims[dim])
        dims[index] += 1
    return tuple(dims)


def product(values) -> int:
    result = 1
    for value in values:
        result *= value
    return result


def ceil_log2(value: int) -> int:
    if value <= 1:
        return 0
    return math.ceil(math.log2(value))


def coord_name(prefix: str, coord: tuple[int, ...]) -> str:
    return prefix + '_' + '_'.join(str(value) for value in coord)


def index_to_coord(index: int, dims: tuple[int, ...]) -> tuple[int, ...]:
    coord = []
    for dim in dims:
        coord.append(index % dim)
        index //= dim
    return tuple(coord)


def coord_to_index(coord: tuple[int, ...], dims: tuple[int, ...]) -> int:
    index = 0
    stride = 1
    for value, dim in zip(coord, dims):
        if value < 0 or value >= dim:
            raise ValueError(f'Coordinate {coord} is outside dimensions {dims}')
        index += value * stride
        stride *= dim
    return index


def iter_coords(dims: tuple[int, ...]):
    total = product(dims)
    for index in range(total):
        yield index_to_coord(index, dims)
