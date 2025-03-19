# Convert a GLTF packed with gltfpack to binary format
# gltfpack.exe -i "$InFile" -o "$OutFile -noq
import json
import os
import sys
import base64
import struct
from mesh import Mesh, Vec2, Vec3, Vec4, Vertex, SkinnedVertex, Triangle, Material, Subset

import pdb

# 5121 -> UCHAR
# 5123 -> USHORT
# 5125 -> UINT
# 5126 -> FLOAT32
# 34962 -> ARRAY_BUFFER (vertex data)
# 34963 -> ELEMENT_ARRAY_BUFFER (indices)

CHAR = 5120
UCHAR = 5121
SSHORT = 5122
USHORT = 5123
FLOAT32 = 5126

TYPE_SIZES = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
COMPONENT_TYPE_SIZES = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}  # Size in bytes
DTYPE_MAP = {5120: 'b', 5121: 'B', 5122: 'h', 5123: 'H', 5125: 'I', 5126: 'f'}  # Struct format


# class GltfMesh(Mesh):
#     def from_file(self, filename):
#         print(filename)


# if __name__ == '__main__':
#     m = GltfMesh()
#     m.from_file(sys.argv[1])

file_rel_path = sys.argv[1]
skinned = False
if len(sys.argv) > 2: skinned = sys.argv[2] == '--skinned'

file_abs_path = os.path.abspath(file_rel_path)
original_filename = os.path.splitext(os.path.basename(file_abs_path))[0]
cwd = os.path.dirname(file_abs_path)
with open(file_abs_path, 'r') as file:
    raw = file.read()

gltf = json.loads(raw)

scene = gltf['scene']
nodes = gltf['nodes']

mesh_nodes = [i for i, n in enumerate(nodes) if 'mesh' in n]
skin_nodes = [i for i, n in enumerate(nodes) if 'skin' in n]
skin_indices = set([nodes[n]['skin'] for n in skin_nodes])
meshes = gltf['meshes']
skins = gltf['skins']
materials = gltf['materials']
textures = gltf['textures']
images = gltf['images']

assert(len(mesh_nodes) == len(meshes))
assert(len(skin_indices) == len(skins))

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

    # assert(base_color_texture_info is not None)
    # assert(base_color_texture_info.get('extensions') is not None)
    # tex_transform = base_color_texture_info['extensions']['KHR_texture_transform']
    # offset = tex_transform['offset']
    # scale = tex_transform['scale']

    texture = textures[base_color_texture_info['index']]
    image = images[texture['source']]
    file = image['uri'] # TODO: can be base64?
    base_color_factor = pbr_mr.get('baseColorFactor', [1, 1, 1, 1])

    # return Material(os.path.join(cwd, file), Vec2(*offset), Vec2(*scale), Vec4(*base_color_factor))
    return Material(os.path.join(cwd, file))


def process_mesh(mi):
    mesh = meshes[mi]

    primitives = mesh['primitives']

    # translation = node['translation']
    # scale = node['scale']
    # assert(translation is not None)
    # assert(scale is not None)

    # m = Mesh(translation, scale)
    m = Mesh()

    istart = 0
    vstart = 0

    for prim in primitives:
        indices = get_values(prim['indices'])
        num_indices = len(indices)
        num_vertices = max(indices)[0] + 1

        triangles = [Triangle(indices[i][0], indices[i+1][0], indices[i+2][0]) for i in range(0, num_indices, 3)]
        m.tris.extend(triangles)

        material = get_material(prim.get('material'))

        subset = Subset(material, istart, num_indices, vstart)
        m.subsets.append(subset)
        istart += num_indices
        vstart += num_vertices

        attributes = prim['attributes']
        assert(attributes.get('POSITION') is not None)
        assert(attributes.get('NORMAL') is not None)
        assert(attributes.get('TEXCOORD_0') is not None)

        # pdb.set_trace()

        idx = attributes['POSITION']
        # assert(accessors[idx]['componentType'] == USHORT)
        assert(accessors[idx]['componentType'] == FLOAT32)
        assert(accessors[idx].get('normalized', False) == False)
        values = get_values(idx)
        positions = [Vec3(v[0], v[1], v[2]) for v in values]

        idx = attributes['NORMAL']
        # assert(accessors[idx]['componentType'] == CHAR)
        assert(accessors[idx]['componentType'] == FLOAT32)
        # assert(accessors[idx]['normalized'] == True)
        assert(accessors[idx].get('normalized', False) == False)
        values = get_values(idx)
        normals = [Vec3(v[0], v[1], v[2]) for v in values]

        # if attributes.get('TANGENT', None) is not None:
        #     pass # TODO

        # if attributes.get('COLOR_0', None) is not None:
        #     pass # TODO

        idx = attributes['TEXCOORD_0']
        # assert(accessors[idx]['componentType'] == USHORT)
        assert(accessors[idx]['componentType'] == FLOAT32)
        # assert(accessors[idx]['normalized'] == True)
        assert(accessors[idx].get('normalized', False) == False)
        values = get_values(idx)
        uvs = [Vec2(v[0], v[1]) for v in values]

        assert(len(positions) == len(normals) == len(uvs) == num_vertices)

        if skinned:
            assert(attributes.get('JOINTS_0') is not None)
            assert(attributes.get('WEIGHTS_0') is not None)

            idx = attributes['JOINTS_0']
            assert(accessors[idx]['componentType'] == UCHAR)
            # assert(accessors[idx]['normalized'] == True)
            assert(accessors[idx].get('normalized', False) == False)
            joints = get_values(idx)

            idx = attributes['WEIGHTS_0']
            assert(accessors[idx]['componentType'] == UCHAR)
            assert(accessors[idx]['normalized'] == True)
            weights = get_values(idx)

            assert(len(joints) == len(weights) == num_vertices)

            for i in range(num_vertices):
                m.vertices.append(SkinnedVertex(positions[i], normals[i], uvs[i], weights[i], joints[i]))
        else:
            for i in range(num_vertices):
                m.vertices.append(Vertex(positions[i], normals[i], uvs[i]))

    return m


def construct_bone_hierarchy(si):
    skin = skins[si]
    skeleton_index = skin['skeleton']
    pairs = {}

    stack = [(skeleton_index, -1)]

    while stack:
        node_idx, parent_idx = stack.pop()
        assert(pairs.get(node_idx) is None)
        pairs[node_idx] = parent_idx

        if "children" in nodes[node_idx]:
            for child_idx in nodes[node_idx]["children"]:
                stack.append((child_idx, node_idx))

    return pairs

for i, ni in enumerate(mesh_nodes):
    node = nodes[ni]
    print("***** process MESH {} *****".format(i))
    m = process_mesh(node['mesh'])
    filename = "{}_mesh_{}.m3d".format(original_filename, i+1)
    # TODO: assign skin index to mesh
    m.pack(filename)
    m.convert_textures()
    print(filename)

################################################################################
#
# SKINNED
#
################################################################################

if skinned:
    animations = gltf['animations']

    def keyframes_for(ai):
        animation = animations[ai]
        samplers = animation['samplers']
        channels = animation['channels']

        sampler_input_accessors = set([s['input'] for s in samplers])
        sampler_output_accessors = set([s['output'] for s in samplers])
        sampler_input_values = {}
        sampler_output_values = {}
        for s in sampler_input_accessors:
            if s not in sampler_input_values:
                sampler_input_values[s] = get_values(s)
        for s in sampler_output_accessors:
            if s not in sampler_output_values:
                # TODO: values may be normalized (rotation => quaternion)
                sampler_output_values[s] = get_values(s)


        nodes_keyframes = {}
        target_nodes = set([c['target']['node'] for c in channels])

        num_keyframes = []

        for node_idx in target_nodes:
            channels = [channel for channel in animation['channels'] if channel['target']['node'] == node_idx]
            trans_channels = [channel for channel in channels if channel['target']['path'] == 'translation']
            rot_channels = [channel for channel in channels if channel['target']['path'] == 'rotation']
            scale_channels = [channel for channel in channels if channel['target']['path'] == 'scale']

            keyframes = {}

            # TODO: dry these 3 ifs
            if len(trans_channels) > 0:
                assert(len(trans_channels) == 1)
                sampler_i = trans_channels[0]['sampler']
                sampler = samplers[sampler_i]
                inputs = sampler_input_values[sampler['input']]
                outputs = sampler_output_values[sampler['output']]

                assert(len(inputs) == len(outputs))
                for i in range(len(inputs)):
                    keyframes[inputs[i][0]] = { 'translation': outputs[i] }

            if len(rot_channels) > 0:
                assert(len(rot_channels) == 1)
                sampler_i = rot_channels[0]['sampler']
                sampler = samplers[sampler_i]
                inputs = sampler_input_values[sampler['input']]
                outputs = sampler_output_values[sampler['output']]

                assert(len(inputs) == len(outputs))
                for i in range(len(inputs)):
                    key = inputs[i][0]
                    if key in keyframes:
                        keyframes[key]['rotation'] = outputs[i]
                    else:
                        keyframes[key] = { 'rotation': outputs[i] }

            if len(scale_channels) > 0:
                assert(len(scale_channels) == 1)
                sampler_i = scale_channels[0]['sampler']
                sampler = samplers[sampler_i]
                inputs = sampler_input_values[sampler['input']]
                outputs = sampler_output_values[sampler['output']]

                assert(len(inputs) == len(outputs))
                for i in range(len(inputs)):
                    key = inputs[i][0]
                    if key in keyframes:
                        keyframes[key]['scale'] = outputs[i]
                    else:
                        keyframes[key] = { 'scale': outputs[i] }

            nodes_keyframes[node_idx] = keyframes
            num_keyframes.append(len(keyframes))

        # assert(len(set(num_keyframes)) == 1)
        return num_keyframes, nodes_keyframes

    for i in skin_indices:
        bh = construct_bone_hierarchy(i)

        num_keyframes, animation_keyframes = keyframes_for(0)
        local_node_transforms = {}

        joints = skins[i]['joints']
        extra = set(bh.keys()) - set(joints)

        # list of nodes whose direct parent are not in joint list
        parents_not_joint = {}
        for joint in joints:
            chain = []
            parent = bh.get(joint)

            while parent in extra:
                chain.append(parent)
                parent = bh.get(parent)

            if chain:
                parents_not_joint[joint] = chain

        pdb.set_trace()
        for node_index in joints:
            local_transform = {}
            parent_transform = {}

            # TODO: compute the parent transform here
            if node_index in parents_not_joint:
                pdb.set_trace()

            # TODO: here node_index is potentially not in animation_keyframes so use node transform.
            # there is thus only one keyframe
            if node_index not in animation_keyframes:
                pdb.set_trace()

            kf = animation_keyframes[node_index]
            for time, kf_transforms in kf.items():
                t = {}
                t['translation'] = Vec3(*kf_transforms.get('translation', nodes[node_index].get('translation', [0, 0, 0])))
                t['rotation'] = Vec4(*kf_transforms.get('rotation', nodes[node_index].get('rotation', [0, 0, 0, 1])))
                t['scale'] = Vec3(*kf_transforms.get('scale', nodes[node_index].get('scale', [1, 1, 1])))

                local_transform[time] = t
            local_node_transforms[node_index] = local_transform
        pdb.set_trace()
