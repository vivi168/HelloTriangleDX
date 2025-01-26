import json
import os
import sys
import base64
import struct
from mesh import Mesh, Vec2, Vec3, Vec4, Vertex, Triangle, Subset


# 5123 -> USHORT
# 5125 -> UINT
# 5126 -> FLOAT32
# 34962 -> ARRAY_BUFFER (vertex data)
# 34963 -> ELEMENT_ARRAY_BUFFER (indices)

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

data = json.loads(raw)
# print(data)

meshes = data['meshes']
if len(meshes) > 1:
    print("[WARNING]: only one mesh is supported")


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
        values.extend(list(struct.iter_unpack(dtype * type_size, data)))
    else:
        for i in range(accessor['count']):
            start = data_start + i * byte_stride
            end = start + component_size * type_size
            data = buffer[start:end]
            values.extend(list(struct.iter_unpack(dtype * type_size, data)))

    return values

def get_material(material_id):
    print(material_id)
    if material_id is None:
        return

mesh = meshes[0]
primitives = mesh['primitives']
accessors = data['accessors']
buffer_views = data['bufferViews']
raw_buffers = data['buffers']
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

for prim in primitives:
    print("*** INDICES ***")
    indices = get_values(prim['indices'])
    print(len(indices), min(indices), max(indices))
    triangles = [Triangle(indices[i][0], indices[i+1][0], indices[i+2][0]) for i in range(0, len(indices), 3)]
    for i in triangles: print(i)

    material = get_material(prim.get('material'))

    attributes = prim['attributes']
    if attributes.get('POSITION'):
        print("*** POSITION ***")
        values = get_values(attributes['POSITION'])
        positions = [Vec3(v[0], v[1], v[2]) for v in values]
        print(len(positions))
        for p in positions:  print(p)

    if attributes.get('NORMAL'):
        print("*** NORMAL ***")
        values = get_values(attributes['NORMAL'])
        normals = [Vec3(v[0], v[1], v[2]) for v in values]
        print(len(normals))
        for n in normals: print(n)

    # if attributes.get('TANGENT'):
    #     pass # TODO

    # if attributes.get('COLOR_0'):
    #     pass # TODO

    if attributes.get('TEXCOORD_0'):
        print("*** TEXCOORD_0 ***")
        values = get_values(attributes['TEXCOORD_0'])
        uvs = [Vec2(v[0], v[1]) for v in values]
        print(len(uvs))
        for n in uvs: print(n)
    print()
