# Convert a GLTF packed with gltfpack to binary format
# gltfpack.exe -i "$InFile" -o "$OutFile -noq
# python3 gltf.py -i "$InFile" [--skinned]
import json
import os
import sys
import base64
import struct
from mesh import Mesh, Vec2, Vec3, Vec4, Vertex, SkinnedVertex, Triangle, Material, Subset, Skin, Animation, StaticTransforms
import argparse

CHAR = 5120
UCHAR = 5121
SSHORT = 5122
USHORT = 5123
FLOAT32 = 5126

TYPE_SIZES = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
COMPONENT_TYPE_SIZES = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}  # Size in bytes
DTYPE_MAP = {5120: 'b', 5121: 'B', 5122: 'h', 5123: 'H', 5125: 'I', 5126: 'f'}  # Struct format
NORMALIZE_FACTOR = {5120: 127.0, 5121: 255.0, 5122: 32767.0, 5123: 65535.0}

class GLTFConvert:
    def __init__(self, file_rel_path, skinned):
        file_abs_path = os.path.abspath(file_rel_path)
        self.original_filename = os.path.splitext(os.path.basename(file_abs_path))[0]
        self.cwd = os.path.dirname(file_abs_path)
        self.skinned = skinned

        with open(file_abs_path, 'r') as file:
            raw = file.read()

        self.gltf = json.loads(raw)
        self.nodes = self.gltf['nodes']
        self.mesh_nodes = [i for i, n in enumerate(self.nodes) if 'mesh' in n]
        self.skin_nodes = [i for i, n in enumerate(self.nodes) if 'skin' in n]
        self.skin_indices = list(set([self.nodes[n]['skin'] for n in self.skin_nodes]))
        self.meshes = self.gltf['meshes']
        self.skins = self.gltf.get('skins', [])
        self.animations = self.gltf.get('animations', [])
        self.materials = self.gltf['materials']
        self.textures = self.gltf['textures']
        self.images = self.gltf['images']

        assert(len(self.mesh_nodes) == len(self.meshes))
        assert(len(self.skin_indices) == len(self.skins))

        self.accessors = self.gltf['accessors']
        self.buffer_views = self.gltf['bufferViews']
        self.buffers = []
        self.construct_buffers()

    def construct_buffers(self):
        raw_buffers = self.gltf['buffers']

        for buf in raw_buffers:
            uri = buf['uri']
            if uri.startswith('data:'):
                base64_data = uri.split('base64,')[-1]
                data = base64.b64decode(base64_data)
                self.buffers.append(data)
            else:
                with open(os.path.join(self.cwd, uri), 'rb') as bin_file:
                    data = bin_file.read()
                self.buffers.append(data)

    def get_values(self, accessor_id, normalize=False):
        accessor = self.accessors[accessor_id]
        buffer_view = self.buffer_views[accessor['bufferView']]
        buffer = self.buffers[buffer_view['buffer']]

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

    def get_material(self, material_id):
        assert(material_id is not None)

        material = self.materials[material_id]
        pbr_mr = material['pbrMetallicRoughness']
        base_color_texture_info = pbr_mr['baseColorTexture']

        texture = self.textures[base_color_texture_info['index']]
        image = self.images[texture['source']]
        file = image['uri'] # TODO: can be base64?
        # base_color_factor = pbr_mr.get('baseColorFactor', [1, 1, 1, 1]) # TODO: for PBR

        return Material(os.path.join(self.cwd, file))

    def process_mesh(self, mi):
        mesh = self.meshes[mi]

        primitives = mesh['primitives']
        m = Mesh()

        istart = 0
        vstart = 0

        for prim in primitives:
            indices = self.get_values(prim['indices'])
            num_indices = len(indices)
            num_vertices = max(indices)[0] + 1

            triangles = [Triangle(indices[i][0], indices[i+1][0], indices[i+2][0]) for i in range(0, num_indices, 3)]
            m.tris.extend(triangles)

            material = self.get_material(prim.get('material'))

            subset = Subset(material, istart, num_indices, vstart)
            m.subsets.append(subset)
            istart += num_indices
            vstart += num_vertices

            attributes = prim['attributes']
            assert(attributes.get('POSITION') is not None)
            assert(attributes.get('NORMAL') is not None)
            assert(attributes.get('TEXCOORD_0') is not None)

            idx = attributes['POSITION']
            assert(self.accessors[idx]['componentType'] == FLOAT32)
            assert(self.accessors[idx].get('normalized', False) == False)
            values = self.get_values(idx)
            positions = [Vec3(v[0], v[1], v[2]) for v in values]

            idx = attributes['NORMAL']
            assert(self.accessors[idx]['componentType'] == FLOAT32)
            assert(self.accessors[idx].get('normalized', False) == False)
            values = self.get_values(idx)
            normals = [Vec3(v[0], v[1], v[2]) for v in values]

            # if attributes.get('TANGENT', None) is not None:
            #     pass # TODO

            # if attributes.get('COLOR_0', None) is not None:
            #     pass # TODO

            idx = attributes['TEXCOORD_0']
            assert(self.accessors[idx]['componentType'] == FLOAT32)
            assert(self.accessors[idx].get('normalized', False) == False)
            values = self.get_values(idx)
            uvs = [Vec2(v[0], v[1]) for v in values]

            assert(len(positions) == len(normals) == len(uvs) == num_vertices)

            if self.skinned:
                assert(attributes.get('JOINTS_0') is not None)
                assert(attributes.get('WEIGHTS_0') is not None)

                idx = attributes['JOINTS_0']
                assert(self.accessors[idx]['componentType'] == UCHAR)
                assert(self.accessors[idx].get('normalized', False) == False)
                joints = self.get_values(idx)

                idx = attributes['WEIGHTS_0']
                assert(self.accessors[idx]['componentType'] == UCHAR)
                assert(self.accessors[idx]['normalized'] == True)
                weights = self.get_values(idx)

                assert(len(joints) == len(weights) == num_vertices)

                for i in range(num_vertices):
                    m.vertices.append(SkinnedVertex(positions[i], normals[i], uvs[i], weights[i], joints[i]))
            else:
                for i in range(num_vertices):
                    m.vertices.append(Vertex(positions[i], normals[i], uvs[i]))

        return m


    def construct_bone_hierarchy(self, si):
        skin = self.skins[si]
        skeleton_index = skin['skeleton']
        pairs = {}

        stack = [(skeleton_index, -1)]

        while stack:
            node_idx, parent_idx = stack.pop()
            assert(pairs.get(node_idx) is None)
            pairs[node_idx] = parent_idx

            if "children" in self.nodes[node_idx]:
                for child_idx in self.nodes[node_idx]["children"]:
                    stack.append((child_idx, node_idx))

        return pairs


    def keyframes_for(self, ai):
        animation = self.animations[ai]
        samplers = animation['samplers']
        channels = animation['channels']

        sampler_input_accessors = set([s['input'] for s in samplers])
        sampler_output_accessors = set([s['output'] for s in samplers])
        sampler_input_values = {}
        sampler_output_values = {}
        for s in sampler_input_accessors:
            if s not in sampler_input_values:
                sampler_input_values[s] = self.get_values(s)
        for s in sampler_output_accessors:
            if s not in sampler_output_values:
                normalized = self.accessors[s].get('normalized', False)
                sampler_output_values[s] = self.get_values(s, normalized)

        nodes_keyframes = {}
        target_nodes = set([c['target']['node'] for c in channels])

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

        return nodes_keyframes

    def convert(self):
        for i, ni in enumerate(self.mesh_nodes):
            node = self.nodes[ni]
            filename = "{}_mesh_{}.mesh".format(self.original_filename, i+1)
            print("mesh #{}: {}".format(i+1, filename), end="")

            if self.skinned:
                skin_idx = self.skin_indices.index(node['skin'])
                print(" (-> skin #{})".format(skin_idx+1))
            else:
                print()

            m = self.process_mesh(node['mesh'])
            m.pack(filename)
            m.convert_textures()

        ############################################################################
        #
        # SKINNED
        #
        ############################################################################

        if not self.skinned:
            return

        nodes_static_transforms = {}
        missing_bones = set()

        # skins
        for i in self.skin_indices:
            bh = self.construct_bone_hierarchy(i)
            root_bone = self.skins[i]['skeleton']
            joints = self.skins[i]['joints']
            inverse_bind_matrices = self.get_values(self.skins[i]['inverseBindMatrices'])

            filename = "{}_skin_{}.skin".format(self.original_filename, i+1)
            s = Skin(root_bone, joints, inverse_bind_matrices, bh)
            s.pack(filename)
            print("skin #{}: {}".format(i+1, filename))

            for ni in bh.keys():
                t = {}
                t['translation'] = Vec3(*self.nodes[ni].get('translation', [0, 0, 0]))
                t['rotation'] = Vec4(*self.nodes[ni].get('rotation', [0, 0, 0, 1]))
                t['scale'] = Vec3(*self.nodes[ni].get('scale', [1, 1, 1]))

                nodes_static_transforms[ni] = t

        # animations
        for ai in range(len(self.animations)):
            animation_keyframes = self.keyframes_for(ai)
            nodes_keyframe_transforms = {}
            for node_index, kf in animation_keyframes.items():
                local_transform = {}

                for time, kf_transforms in kf.items():
                    t = {}
                    t['translation'] = Vec3(*kf_transforms.get('translation', self.nodes[node_index].get('translation', [0, 0, 0])))
                    t['rotation'] = Vec4(*kf_transforms.get('rotation', self.nodes[node_index].get('rotation', [0, 0, 0, 1])))
                    t['scale'] = Vec3(*kf_transforms.get('scale', self.nodes[node_index].get('scale', [1, 1, 1])))

                    local_transform[time] = t
                nodes_keyframe_transforms[node_index] = local_transform

            filename = "{}_animation_{}.anim".format(self.original_filename, ai+1)
            a = Animation(nodes_keyframe_transforms)
            a.pack(filename)
            print("animation #{}: {} -- [{}]".format(ai+1, filename, self.animations[ai].get('name', 'noname')))

            missing = set(bh.keys()) - set(nodes_keyframe_transforms.keys())
            if len(missing) > 0:
                missing_bones.update(missing)

        if len(missing_bones) > 0:
            filename = "{}_transforms.bin".format(self.original_filename)
            print("transforms: {}".format(filename))
            st = StaticTransforms(missing_bones, nodes_static_transforms)
            st.pack(filename)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Convert a GLTF model")
    parser.add_argument("-i", "--infile", required=True, help="GLTF input file")
    # parser.add_argument("-o", "--outfile", help="Output path")
    parser.add_argument("-s", "--skinned", action="store_true", help="Skinned model")

    args = parser.parse_args()

    g = GLTFConvert(args.infile, args.skinned)
    g.convert()
