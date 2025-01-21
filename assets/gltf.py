import json
import sys
import base64
import struct
from mesh import Mesh, Vec2, Vec3, Vertex, Triangle, Subset


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

with open(sys.argv[1], 'r') as file:
    raw = file.read()

data = json.loads(raw)
# print(data)

meshes = data['meshes']
if len(meshes) > 1:
    print("[WARNING]: only one mesh is supported")


def issou(accessor_id):
    accessor = accessors[accessor_id]
    buffer_view = buffer_views[accessor['bufferView']]
    buffer = buffers[buffer_view['buffer']]

    buffer_offset = buffer_view['byteOffset']
    accessor_offset = accessor.get('byteOffset', 0)
    total_offset = buffer_offset + accessor_offset

    type_size = TYPE_SIZES[accessor['type']]
    component_size = COMPONENT_TYPE_SIZES[accessor['componentType']]
    dtype = DTYPE_MAP[accessor['componentType']]

    print(accessor)
    print(buffer_view)
    print(component_size)
    print(dtype)

    byte_stride = buffer_view.get('byteStride')
    data_start = total_offset

    if byte_stride is None:
        data_end = data_start + accessor['count'] * component_size * type_size
        data = buffer[data_start:data_end]
        print(list(struct.iter_unpack(dtype * type_size, data)))
    else:
        print('byte stride')
        for i in range(accessor['count']):
            start = data_start + i * byte_stride
            end = start + component_size * type_size
            data = buffer[start:end]
            print(list(struct.iter_unpack(dtype * type_size, data)))



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
        # TODO: deduce correct path
        with open(uri, 'rb') as bin_file:
            data = bin_file.read()
        buffers.append(data)

print(len(primitives)) # primitives are Subsets
for prim in primitives:
    print("*** INDICES ***")
    issou(prim['indices'])

    attributes = prim['attributes']
    if attributes.get('POSITION'):
        print("*** POSITION ***")
        issou(attributes['POSITION'])

    if attributes.get('NORMAL'):
        print("*** NORMAL ***")
        issou(attributes['NORMAL'])

    if attributes.get('TANGENT'):
        pass # TODO

    if attributes.get('COLOR_0'):
        pass # TODO

    if attributes.get('TEXCOORD_0'):
        pass # TODO
