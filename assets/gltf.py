# Convert a GLTF packed with gltfpack to binary format
# gltfpack.exe -i "$InFile" -o "$OutFile -noq
import json
import os
import sys
import base64
import struct
from collections import defaultdict, deque
from mesh import Mesh, Vec2, Vec3, Vec4, Vertex, SkinnedVertex, Triangle, Material, Subset, Skin, Animation

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
NORMALIZE_FACTOR = {5120: 127.0, 5121: 255.0, 5122: 32767.0, 5123: 65535.0}

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
skin_indices = list(set([nodes[n]['skin'] for n in skin_nodes]))
meshes = gltf['meshes']
skins = gltf.get('skins', [])
animations = gltf.get('animations', [])
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

def get_values(accessor_id, normalize=False):
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

    if normalize:
        factor = NORMALIZE_FACTOR[accessor['componentType']]
        return [[e / factor for e in t] for t in values]

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
            normalized = accessors[s].get('normalized', False)
            sampler_output_values[s] = get_values(s, normalized)

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

def convert():
    for i, ni in enumerate(mesh_nodes):
        node = nodes[ni]
        print("***** process MESH {} *****".format(i))
        m = process_mesh(node['mesh'])
        filename = "{}_mesh_{}.mesh".format(original_filename, i+1)

        skin_node = node['skin']
        skin_idx = skin_indices.index(skin_node)

        m.pack(filename)
        m.convert_textures()
        print("mesh: {} (skin: {})".format(filename, skin_idx + 1))

        m3d_m = m # TODO: remove later

    ############################################################################
    #
    # SKINNED
    #
    ############################################################################

    if not skinned:
        return

    nodes_static_transforms = {}

    # skins
    for i in skin_indices:
        bh = construct_bone_hierarchy(i)
        m3d_bh = bh # TODO: remove later

        root_bone = skins[i]['skeleton']
        joints = skins[i]['joints']
        inverse_bind_matrices = get_values(skins[i]['inverseBindMatrices'])

        filename = "{}_skin_{}.skin".format(original_filename, i+1)
        s = Skin(root_bone, joints, inverse_bind_matrices, bh)
        s.pack(filename)
        print("skin: {}".format(filename))

        for ni in bh.keys():
            t = {}
            t['translation'] = Vec3(*nodes[ni].get('translation', [0, 0, 0]))
            t['rotation'] = Vec4(*nodes[ni].get('rotation', [0, 0, 0, 1]))
            t['scale'] = Vec3(*nodes[ni].get('scale', [1, 1, 1]))

        nodes_static_transforms[ni] = t

    # animations
    for ai in range(len(animations)):
        num_keyframes, animation_keyframes = keyframes_for(ai)
        nodes_keyframe_transforms = {}
        for node_index, kf in animation_keyframes.items():
            local_transform = {}

            for time, kf_transforms in kf.items():
                t = {}
                t['translation'] = Vec3(*kf_transforms.get('translation', nodes[node_index].get('translation', [0, 0, 0])))
                t['rotation'] = Vec4(*kf_transforms.get('rotation', nodes[node_index].get('rotation', [0, 0, 0, 1])))
                t['scale'] = Vec3(*kf_transforms.get('scale', nodes[node_index].get('scale', [1, 1, 1])))

                local_transform[time] = t
            nodes_keyframe_transforms[node_index] = local_transform

        filename = "{}_animation_{}.anim".format(original_filename, ai+1)
        a = Animation(num_keyframes, nodes_keyframe_transforms)
        a.pack(filename)
        print("animation: {} -- [{}]".format(filename, animations[ai].get('name', 'noname')))

        m3d_kf = nodes_keyframe_transforms # TODO: remove later


    # pdb.set_trace()
    # TODO: remove later.
    # import numpy as np
    # print("***************m3d-File-Header***************")
    # print("#Materials {}".format(len(m3d_m.subsets)))
    # print("#Vertices {}".format(len(m3d_m.vertices)))
    # print("#Triangles {}".format(len(m3d_m.tris)))
    # print("#Bones {}".format(len(m3d_bh)))
    # print("#AnimationClips {}".format(1))

    # print("\n***************Materials*********************")
    # for i, s in enumerate(m3d_m.subsets):
    #     print("Name: material_{}".format(i))
    #     print("Diffuse: 1 1 1")
    #     print("Fresnel0: 0.05 0.05 0.05")
    #     print("Roughness: 0.5")
    #     print("AlphaClip: 0")
    #     print("MaterialTypeName: Skinned")
    #     print("DiffuseMap: upBody_diff.dds")
    #     print("NormalMap: upBody_norm.dds")

    # print("\n***************SubsetTable*******************")
    # for i, s in enumerate(m3d_m.subsets):
    #     print("SubsetID: {} VertexStart: {} VertexCount: {} FaceStart: {} FaceCount: {}".format(i, s.vstart, len(m3d_m.vertices), s.istart, int(s.icount/3)))
    # print("\n***************Vertices**********************")
    # for i, v in enumerate(m3d_m.vertices):
    #     print("Position: {}".format(v.position))
    #     print("Tangent: {:.6f} {:.6f} {:.6f} {:.6f}".format(0, 0, 0, 1))
    #     print("Normal: {}".format(v.normal))
    #     print("Tex-Coords: {}".format(v.uv))
    #     padded_weights = list(v.weights) + [0.0] * (4 - len(v.weights))
    #     padded_indices = list(v.joint_indices) + [0] * (4 - len(v.joint_indices))
    #     print("BlendWeights: {:.6f} {:.6f} {:.6f} {:.6f}".format(padded_weights[0]/255, padded_weights[1]/255, padded_weights[2]/255, padded_weights[3]/255))
    #     print("BlendIndices: {} {} {} {}\n".format(padded_indices[0], padded_indices[1], padded_indices[2], padded_indices[3]))
    # print("\n***************Triangles*********************")
    # for t in m.tris:
    #     print(t)
    # print("\n***************BoneOffsets*******************")
    # ibms = get_values(skins[0]['inverseBindMatrices'])
    # for i, bo in enumerate(ibms):
    #     matrix = (np.array(bo).reshape(4, 4))
    #     print("BoneOffset{}".format(i), " ".join(f"{val:.6f}" for row in matrix for val in row))
    # print("\n***************BoneHierarchy*****************")
    # joints = skins[0]['joints']
    # for c, p in sorted(m3d_bh.items(), key=lambda item: joints.index(item[0])):
    #     print("ParentIndexOfBone{}: {}".format(joints.index(c), joints.index(p) if p in joints else p))
    # print("\n***************AnimationClips****************")
    # print("AnimationClip Take1")
    # print("{")
    # for c, kfs in sorted(m3d_kf.items(), key=lambda item: joints.index(item[0])):
    #     print("\tBone{} #Keyframes: {}".format(joints.index(c), len(kfs)))
    #     print("\t{")
    #     for t, kf in kfs.items():
    #         print("\t\tTime: {} Pos: {} Scale: {} Quat: {}".format(t, kf['translation'], kf['scale'], kf['rotation']))
    #     print("\t}\n")
    # print("}")


if __name__ == '__main__':
    convert()

    # when dumping skin, also print as reference the names of the bones
