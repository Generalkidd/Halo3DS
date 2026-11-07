#!/usr/bin/env python3
"""Cook exact ordered BSP block bounds and ideal camera-side evidence."""

import argparse
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import struct


SCHEMA = 1
TRIANGLES_PER_BLOCK = 32
MAX_VERTICES = 65535
MAX_INDICES = 131072 * 3
VERTEX = struct.Struct('<8f')
INDEX = struct.Struct('<H')


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def triangle_normal(a, b, c):
    u = tuple(y - x for x, y in zip(a, b))
    v = tuple(y - x for x, y in zip(a, c))
    return (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])


def coordinate_bounds(vectors):
    return tuple((min(axis), max(axis)) for axis in zip(*vectors))


def normal_cone(normals):
    """Use the first winding normal as an exact axis and bound cos(angle)^2."""
    axis = normals[0]
    axis_length = dot(axis, axis)
    if not axis_length:
        return None
    floor = Fraction(1)
    for normal in normals:
        aligned = dot(axis, normal)
        # Degenerate, orthogonal and opposing normals keep the block uncertain.
        if aligned <= 0:
            return None
        floor = min(floor, aligned * aligned / (axis_length * dot(normal, normal)))
    return {'axis': [str(x) for x in axis], 'cosine_squared_lower_bound': str(floor)}


def camera_side_bounds(eye, positions, normals):
    lower = upper = Fraction(0)
    for i, ((p_min, p_max), (n_min, n_max)) in enumerate(zip(positions, normals)):
        products = [n * (eye[i] - p) for n in (n_min, n_max) for p in (p_min, p_max)]
        lower += min(products)
        upper += max(products)
    return lower, upper


def cook(vertex_bytes, index_bytes, eye=None):
    if (not vertex_bytes or len(vertex_bytes) % VERTEX.size
            or len(vertex_bytes) // VERTEX.size > MAX_VERTICES):
        raise ValueError('expected 1..65535 complete 32-byte native BSP vertices')
    if (not index_bytes or len(index_bytes) % (3 * INDEX.size)
            or len(index_bytes) // INDEX.size > MAX_INDICES):
        raise ValueError('expected 1..131072 complete uint16 triangles')
    vertices = []
    for vertex in VERTEX.iter_unpack(vertex_bytes):
        if not all(math.isfinite(value) for value in vertex[:3]):
            raise ValueError('non-finite native position')
        # Binary32 to Python float is exact. Fraction preserves that value
        # through the entire bounds/cone calculation.
        vertices.append(tuple(map(Fraction, vertex[:3])))
    indices = [index for (index,) in INDEX.iter_unpack(index_bytes)]
    if max(indices) >= len(vertices):
        raise ValueError('triangle index outside the supplied vertex space')

    blocks = []
    block_indices = TRIANGLES_PER_BLOCK * 3
    for start in range(0, len(indices), block_indices):
        corners = [vertices[index] for index in indices[start:start + block_indices]]
        normals = [triangle_normal(*corners[i:i + 3]) for i in range(0, len(corners), 3)]
        position_bounds = coordinate_bounds(corners)
        normal_bounds = coordinate_bounds(normals)
        cone = normal_cone(normals)
        block = {
            'first_index': start, 'index_count': len(corners),
            'position_bounds': [[str(lo), str(hi)] for lo, hi in position_bounds],
            'normal_bounds': [[str(lo), str(hi)] for lo, hi in normal_bounds],
            'normal_cone': cone,
        }
        if eye is not None:
            low, high = camera_side_bounds(eye, position_bounds, normal_bounds)
            side = 'positive' if low > 0 else 'negative' if high < 0 else 'uncertain'
            block['ideal_camera_side'] = side if cone else 'uncertain'
            block['ideal_side_bounds'] = [str(low), str(high)]
        blocks.append(block)
    return {
        'schema': SCHEMA,
        'source': {
            'vertex_count': len(vertices), 'index_count': len(indices),
            'vertices_sha256': hashlib.sha256(vertex_bytes).hexdigest(),
            'indices_sha256': hashlib.sha256(index_bytes).hexdigest(),
        },
        'eye': [str(value) for value in eye] if eye is not None else None,
        'blocks': blocks,
    }


def read_bounded(path, limit):
    with path.open('rb') as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f'{path} exceeds the supported byte limit')
    return data


def read_eye(path):
    data = read_bounded(path, 260)
    if len(data) != 260 or data[:8] != b'H2DSCAM1':
        raise ValueError('unsupported midpoint camera record')
    _, kind, width, height, depth_format, _ = struct.unpack_from('<6I', data, 8)
    eye = struct.unpack_from('<3f', data, 21 * 4)
    if kind not in (0, 1) or (width, height, depth_format) != (240, 400, 3) or not all(map(math.isfinite, eye)):
        raise ValueError('unsupported native target or rasterizer camera')
    return tuple(map(Fraction, eye))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('vertices', type=Path, help='native BSP vertex bytes, eight little-endian floats per vertex')
    parser.add_argument('indices', type=Path, help='matching full uint16 little-endian triangle list')
    parser.add_argument('--camera', type=Path, help='optional H2DSCAM1 record for ideal camera-side bounds')
    parser.add_argument('--verify', type=Path, help='check an existing report against the exact source')
    args = parser.parse_args()
    try:
        eye = read_eye(args.camera) if args.camera else None
        report = cook(read_bounded(args.vertices, VERTEX.size * MAX_VERTICES),
                      read_bounded(args.indices, INDEX.size * MAX_INDICES), eye)
        if args.verify:
            if json.loads(read_bounded(args.verify, 32 * 1024 * 1024)) != report:
                raise ValueError('report differs from the exact source, camera or schema')
            print('BSP block proof verified against supplied native inputs')
        else:
            print(json.dumps(report, separators=(',', ':')))
    except (OSError, ValueError) as error:
        parser.exit(2, f'BSP block proof failed: {error}\n')


if __name__ == '__main__':
    main()
