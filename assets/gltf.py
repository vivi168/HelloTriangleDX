# Convert a GLTF packed with gltfpack to binary format
# Position are assumed to be quantized to uint16
# Normals quantized to int8 and normalized
# UVs quantized to uint16 and normalized
import json
import os
import sys
import base64
import struct
from mesh import Mesh, Vec2, Vec3, Vec4, Vertex, Triangle, Subset

import pdb

# 5123 -> USHORT
# 5125 -> UINT
# 5126 -> FLOAT32
# 34962 -> ARRAY_BUFFER (vertex data)
# 34963 -> ELEMENT_ARRAY_BUFFER (indices)

CHAR = 5120
USHORT = 5123

TYPE_SIZES = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}
COMPONENT_TYPE_SIZES = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}  # Size in bytes
DTYPE_MAP = {5120: 'b', 5121: 'B', 5122: 'h', 5123: 'H', 5125: 'I', 5126: 'f'}  # Struct format


# class GltfMesh(Mesh):
#     def from_file(self, filename):
#         print(filename)


# if __name__ == '__main__':
#     m = GltfMesh()
#     m.from_file(sys.argv[1])

file_rp = sys.argv[1]
file_ap = os.path.abspath(file_rp)
cwd = os.path.dirname(file_ap)
with open(file_ap, 'r') as file:
    raw = file.read()

gltf = json.loads(raw)

scene = gltf['scene']
nodes = gltf['nodes']

mesh_nodes = [i for i, n in enumerate(nodes) if 'mesh' in n]
meshes = gltf['meshes']
materials = gltf['materials']
textures = gltf['textures']
images = gltf['images']

assert(len(mesh_nodes) == len(meshes))

accessors = gltf['accessors']
buffer_views = gltf['bufferViews']
raw_buffers = gltf['buffers']
buffers = []

for buf in raw_buffers:
    uri = buf['uri']
    if uri.startswith('data:'):
        base64_data = uri.split('base64,')[-1]
        data = base64.b64decode(base64_data)
        buffers.append(data)
    else:
        with open(os.path.join(cwd, uri), 'rb') as bin_file:
            data = bin_file.read()
        buffers.append(data)

def get_values(accessor_id):
    accessor = accessors[accessor_id]
    buffer_view = buffer_views[accessor['bufferView']]
    buffer = buffers[buffer_view['buffer']]

    buffer_offset = buffer_view['byteOffset']
    accessor_offset = accessor.get('byteOffset', 0)
    total_offset = buffer_offset + accessor_offset

    type_size = TYPE_SIZES[accessor['type']]
    component_size = COMPONENT_TYPE_SIZES[accessor['componentType']]
    dtype = DTYPE_MAP[accessor['componentType']]


    byte_stride = buffer_view.get('byteStride')
    data_start = total_offset

    values = []

    if byte_stride is None:
        data_end = data_start + accessor['count'] * component_size * type_size
        data = buffer[data_start:data_end]
        values.extend(list(struct.iter_unpack('<' + dtype * type_size, data)))
    else:
        for i in range(accessor['count']):
            start = data_start + i * byte_stride
            end = start + component_size * type_size
            data = buffer[start:end]
            values.extend(list(struct.iter_unpack('<' + dtype * type_size, data)))

    return values

def get_material(material_id):
    assert(material_id is not None)

    material = materials[material_id]
    pbr_mr = material['pbrMetallicRoughness']
    base_color_texture_info = pbr_mr['baseColorTexture']

    texture = textures[base_color_texture_info['index']]
    image = images[texture['source']]
    if texture.get('sampler') is not None:
        pass

    base_color_factor = pbr_mr.get('baseColorFactor', [1, 1, 1, 1]) # TODO: default value or None?

    pdb.set_trace()


def process_mesh(i):
    mesh = meshes[i]
    node = nodes[mesh_nodes[i]]
    primitives = mesh['primitives']

    # pdb.set_trace()

    m = Mesh()
    # m.translation = node['translation']
    # m.scale = node['scale']

    istart = 0
    vstart = 0

    for prim in primitives:
        indices = get_values(prim['indices'])
        num_indices = len(indices)
        num_vertices = max(indices)[0] + 1

        triangles = [Triangle(indices[i][0], indices[i+1][0], indices[i+2][0]) for i in range(0, num_indices, 3)]
        m.tris.extend(triangles)

        material = get_material(prim.get('material'))

        subset = Subset('', istart, num_indices, vstart)
        m.subsets.append(subset)
        istart += num_indices
        vstart += num_vertices

        attributes = prim['attributes']

        if attributes.get('POSITION', None) is not None:
            idx = attributes['POSITION']
            # assert(accessors[idx]['componentType'] == USHORT)
            values = get_values(idx)
            positions = [Vec3(v[0], v[1], v[2]) for v in values]
            # for p in positions: print(p)

        if attributes.get('NORMAL', None) is not None:
            idx = attributes['NORMAL']
            # assert(accessors[idx]['componentType'] == CHAR)
            # assert(accessors[idx]['normalized'] == True)
            values = get_values(idx)
            normals = [Vec3(v[0], v[1], v[2]) for v in values]
            # pdb.set_trace()
            # for n in normals: print(n)
        else:
            normals = [Vec3() for _ in values]

        # if attributes.get('TANGENT', None) is not None:
        #     pass # TODO

        # if attributes.get('COLOR_0', None) is not None:
        #     pass # TODO

        if attributes.get('TEXCOORD_0', None) is not None:
            idx = attributes['TEXCOORD_0']
            # assert(accessors[idx]['componentType'] == USHORT)
            # assert(accessors[idx]['normalized'] == True)
            values = get_values(idx)
            uvs = [Vec2(v[0], v[1]) for v in values]
            # pdb.set_trace()
            # for u in uvs: print(u)
        else:
            uvs = [Vec2() for _ in values]

        assert len(positions) == len(normals) == len(uvs) == (num_vertices)
        for i in range(num_vertices):
            m.vertices.append(Vertex(positions[i], normals[i], uvs[i]))

    print(len(m.tris))
    print(len(m.vertices))
    print(len(m.subsets))
    for s in m.subsets: print(s)


for i in range(len(meshes)):
    print("***** process MESH {} *****".format(i))
    process_mesh(i)
