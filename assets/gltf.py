# Convert a GLTF packed with gltfpack to custom binary format
# gltfpack.exe -i "$InFile" -o "$OutFile -noq
# python3 gltf.py -i "$InFile" [--skinned]
import json
import os
import base64
import struct
import argparse
from raw import RawImage

from typing import List

MAX_PATH = 260
SKIP_TEXTURES = True

def pack_string(s):
    assert(len(s) < MAX_PATH)
    return bytes(s.ljust(MAX_PATH, "\0"), "utf-16le")

class Vec2:
    def __init__(self, x=0, y=0):
        self.x = x
        self.y = y

    def pack_u16(self):
        return struct.pack("<HH", self.x, self.y)

    def pack_f32(self):
        return struct.pack("<ff", self.x, self.y)

    def __str__(self):
        return "{:.6f} {:.6f}".format(self.x, self.y)

    def __eq__(self, other):
        return self.x == other.x and self.y == other.y


class Vec3:
    def __init__(self, x=0, y=0, z=0):
        self.x = x
        self.y = y
        self.z = z

    def pack_s8(self):
        return struct.pack("<bbb", self.x, self.y, self.z)

    def pack_u16(self):
        return struct.pack("<HHH", self.x, self.y, self.z)

    def pack_f32(self):
        return struct.pack("<fff", self.x, self.y, self.z)

    def __str__(self):
        return "{:.6f} {:.6f} {:.6f}".format(self.x, self.y, self.z)

    def __eq__(self, other):
        return self.x == other.x and self.y == other.y and self.z == other.z


class Vec4:
    def __init__(self, x=0, y=0, z=0, w=0):
        self.x = x
        self.y = y
        self.z = z
        self.w = w

    def pack(self):
        return struct.pack("<ffff", self.x, self.y, self.z, self.w)

    def __str__(self):
        return "{:.6f} {:.6f} {:.6f} {:.6f}".format(self.x, self.y, self.z, self.w)

    def __eq__(self, other):
        return self.x == other.x and self.y == other.y and self.z == other.z and self.w == other.w

MAX_WEIGHTS = 4
class Vertex:
    def __init__(self, p=Vec3(), n=Vec3(), u=Vec2(), w = (), ji = ()):
        self.position = p
        self.normal = n
        self.uv = u

        assert len(w) == len(ji) <= MAX_WEIGHTS
        self.weights = w
        self.joint_indices = ji

    def pack_positions(self):
        return self.position.pack_f32()

    def pack_normals(self):
        return self.normal.pack_f32()

    def pack_uvs(self):
        return self.uv.pack_f32()

    def pack_weights_and_indices(self):
        padded_weights = list(self.weights) + [0.0] * (MAX_WEIGHTS - len(self.weights))
        padded_indices = list(self.joint_indices) + [0] * (MAX_WEIGHTS - len(self.joint_indices))
        weights_data = struct.pack("<BBBB", *padded_weights[:MAX_WEIGHTS])
        indices_data = struct.pack("<BBBB", *padded_indices[:MAX_WEIGHTS])

        return weights_data + indices_data

    def __str__(self):
        return "({}) ({}) ({})".format(self.position, self.normal, self.uv)

    def __eq__(self, other):
        return self.position == other.position and self.normal == other.normal and self.uv == other.uv


class Triangle:
    def __init__(self, vi1=0, vi2=0, vi3=0):
        self.vertIndices = [vi1, vi2, vi3]

    def pack(self):
        return struct.pack("<III", self.vertIndices[0], self.vertIndices[1], self.vertIndices[2])

    def __str__(self):
        return "{} {} {}".format(self.vertIndices[0], self.vertIndices[1], self.vertIndices[2])

class Texture:
    def __init__(self, sourcepath:str|None=None, color=Vec4(1, 1, 1, 1)):
        if sourcepath is not None:
            self.sourcepath = sourcepath
            self.filename = "{}.raw".format(os.path.splitext(os.path.basename(self.sourcepath))[0])
        else:
            self.color = (round(color.x * 255),
                          round(color.y * 255),
                          round(color.z * 255),
                          round(color.w * 255))
            self.sourcepath = None
            self.filename = "{}-{}-{}-{}.raw".format(*self.color)

    def pack(self):
        return pack_string(self.filename)

    def convert(self, out_folder):
        # TODO: use DDS
        # or at least allow exporting 2, 3 or 4 channels image...
        if self.sourcepath is not None:
            m = RawImage(filename=self.sourcepath)
        else:
            m = RawImage(color=self.color)
        with open(os.path.join(out_folder, self.filename), "wb") as f:
            f.write(m.pack())

class Material:
    def __init__(self, base_color_texture:Texture, metallic_roughness_texture:Texture, normal_map_texture:Texture):
       self.base_color_texture = base_color_texture
       self.metallic_roughness_texture = metallic_roughness_texture
       self.normal_map_texture = normal_map_texture

    def pack(self):
        return self.base_color_texture.pack()

    def convert_textures(self, out_folder):
        self.base_color_texture.convert(out_folder)


class Subset:
    def __init__(self, material, istart=0, icount=0):
        self.material: Material = material
        self.istart = istart
        self.icount = icount

    def __str__(self):
        return "{} {}".format(self.istart, self.icount)

    def pack(self):
        assert(self.istart % 3 == 0)
        assert(self.icount % 3 == 0)
        data = struct.pack("<II", self.istart, self.icount)
        pointer = struct.pack("<Q", 0)  # pointer to material
        return data + self.material.pack() + pointer


class Mesh:
    def __init__(self, translation=Vec3(), scale=Vec3()):
        self.cwd = ""
        self.vertices: List[Vertex] = []
        self.tris: List[Triangle] = []
        self.subsets: List[Subset] = []
        self.skinned = False
        self.parent_node = -1

        self.translation = translation
        self.scale = scale

    def pack(self, outfile):
        data = bytearray()

        # print(len(self.vertices), len(self.tris) * 3, len(self.subsets))
        # TODO: add parent_node and bounding box
        data += struct.pack("<III", len(self.vertices), len(self.tris) * 3, len(self.subsets))

        for t in self.tris:
            data += t.pack()

        for s in self.subsets:
            data += s.pack()

        positions_data = bytearray()
        normals_data = bytearray()
        uvs_data = bytearray()
        weights_and_indices = bytearray()

        for v in self.vertices:
            # data += v.pack()
            positions_data += v.pack_positions()
            normals_data += v.pack_normals()
            uvs_data += v.pack_uvs()

            if self.skinned:
                weights_and_indices += v.pack_weights_and_indices()

        data += (positions_data + normals_data + uvs_data + weights_and_indices)

        with open(outfile, "wb") as f:
            f.write(data)

    def convert_textures(self, out_folder):
        if SKIP_TEXTURES:
            return

        for s in self.subsets:
            m = s.material
            m.convert_textures(out_folder)


class Skin:
    def __init__(self, root_bone, joints, matrices, hierarchy):
        self.root_bone = root_bone
        self.joints = joints
        self.inverse_bind_matrices = matrices
        self.bone_hierarchy = hierarchy

    def pack(self, outfile):
        # header
        num_bones = len(self.bone_hierarchy)
        num_joints = len(self.joints)

        assert len(self.joints) == len(self.inverse_bind_matrices)
        header = struct.pack("<III", self.root_bone, num_bones, num_joints)

        matrices = bytearray()
        for m in self.inverse_bind_matrices:
            matrices += struct.pack("<16f", *m)

        child_bones = struct.pack("<{}i".format(num_bones), *self.bone_hierarchy.keys())
        parent_bones = struct.pack("<{}i".format(num_bones), *self.bone_hierarchy.values())
        joint_bones = struct.pack("<{}i".format(num_joints), *self.joints)

        data = header + child_bones + parent_bones + joint_bones + matrices

        with open(outfile, "wb") as f:
            f.write(data)


class Animation:
    def __init__(self, keyframes):
        self.keyframes = keyframes

    def pack(self, outfile):
        # header
        num_animated_bones = struct.pack("<I", len(self.keyframes))

        keyframes_data = bytearray()
        for ni, keyframes in self.keyframes.items():
            # print(ni, len(keyframes))
            keyframes_data += struct.pack("<ii", ni, len(keyframes))
            for time, transform in keyframes.items():
                # print(time, transform['scale'], transform['translation'], transform['rotation'])
                keyframes_data += (
                    struct.pack("<f", time)
                    + transform["scale"].pack_f32()
                    + transform["translation"].pack_f32()
                    + transform["rotation"].pack()
                )

        data = num_animated_bones + keyframes_data
        with open(outfile, "wb") as f:
            f.write(data)


class StaticTransforms:
    def __init__(self, indices, transforms):
        self.transforms = transforms
        self.missing_indices = indices

    def pack(self, outfile):
        # TODO: only pack non identity
        transform_data = bytearray()
        num_missing_indices = len(self.missing_indices)
        indices = struct.pack("<I", num_missing_indices) + struct.pack(
            "<{}i".format(num_missing_indices), *self.missing_indices
        )

        for ni in self.missing_indices:
            transform = self.transforms[ni]
            transform_data += (
                transform["scale"].pack_f32() + transform["translation"].pack_f32() + transform["rotation"].pack()
            )

        data = indices + transform_data

        with open(outfile, "wb") as f:
            f.write(data)


CHAR = 5120
UCHAR = 5121
SSHORT = 5122
USHORT = 5123
FLOAT32 = 5126

TYPE_SIZES = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
COMPONENT_TYPE_SIZES = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}  # Size in bytes
DTYPE_MAP = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}  # Struct format
NORMALIZE_FACTOR = {5120: 127.0, 5121: 255.0, 5122: 32767.0, 5123: 65535.0}


class GLTFConvert:
    def __init__(self, file_rel_path, skinned):
        file_abs_path = os.path.abspath(file_rel_path)
        self.original_filename = os.path.splitext(os.path.basename(file_abs_path))[0]
        self.cwd = os.path.dirname(file_abs_path)
        self.skinned = skinned

        with open(file_abs_path, "r") as file:
            raw = file.read()

        self.gltf = json.loads(raw)
        self.nodes = self.gltf["nodes"]
        self.mesh_nodes = [i for i, n in enumerate(self.nodes) if "mesh" in n]
        self.skin_nodes = [i for i, n in enumerate(self.nodes) if "skin" in n]
        self.mesh_parent_nodes = {}
        for i, n in enumerate(self.nodes):
            if "children" in n:
                for mn in self.mesh_nodes:
                    if mn in n['children']:
                        self.mesh_parent_nodes[mn] = i

        self.skin_indices = list(set([self.nodes[n]["skin"] for n in self.skin_nodes]))
        self.meshes = self.gltf["meshes"]
        self.skins = self.gltf.get("skins", [])
        self.animations = self.gltf.get("animations", [])
        self.materials = self.gltf["materials"]
        self.textures = self.gltf.get("textures", [])
        self.images = self.gltf.get("images", [])

        assert len(self.mesh_nodes) == len(self.meshes)
        assert len(self.skin_indices) == len(self.skins)

        self.accessors = self.gltf["accessors"]
        self.buffer_views = self.gltf["bufferViews"]
        self.buffers = []
        self.construct_buffers()

    def construct_buffers(self):
        raw_buffers = self.gltf["buffers"]

        for buf in raw_buffers:
            uri = buf["uri"]
            if uri.startswith("data:"):
                base64_data = uri.split("base64,")[-1]
                data = base64.b64decode(base64_data)
                self.buffers.append(data)
            else:
                with open(os.path.join(self.cwd, uri), "rb") as bin_file:
                    data = bin_file.read()
                self.buffers.append(data)

    def get_values(self, accessor_id, normalize=False):
        accessor = self.accessors[accessor_id]
        buffer_view = self.buffer_views[accessor["bufferView"]]
        buffer = self.buffers[buffer_view["buffer"]]

        buffer_offset = buffer_view["byteOffset"]
        accessor_offset = accessor.get("byteOffset", 0)
        total_offset = buffer_offset + accessor_offset

        type_size = TYPE_SIZES[accessor["type"]]
        component_size = COMPONENT_TYPE_SIZES[accessor["componentType"]]
        dtype = DTYPE_MAP[accessor["componentType"]]

        byte_stride = buffer_view.get("byteStride")
        data_start = total_offset

        values = []

        if byte_stride is None:
            data_end = data_start + accessor["count"] * component_size * type_size
            data = buffer[data_start:data_end]
            values.extend(list(struct.iter_unpack("<" + dtype * type_size, data)))
        else:
            for i in range(accessor["count"]):
                start = data_start + i * byte_stride
                end = start + component_size * type_size
                data = buffer[start:end]
                values.extend(list(struct.iter_unpack("<" + dtype * type_size, data)))

        if normalize:
            factor = NORMALIZE_FACTOR[accessor["componentType"]]
            return [[e / factor for e in t] for t in values]

        return values

    def get_material(self, material_id):
        assert material_id is not None

        material = self.materials[material_id]
        pbr_mr = material["pbrMetallicRoughness"]

        base_color_texture_info = pbr_mr.get("baseColorTexture", None)
        if base_color_texture_info is not None:
            texture = self.textures[base_color_texture_info["index"]]
            image = self.images[texture["source"]]
            file = image["uri"]  # TODO: can be base64?
            base_color_texture = Texture(sourcepath=os.path.join(self.cwd, file))
        else:
            base_color_texture = Texture(color=Vec4(*pbr_mr.get("baseColorFactor", [1, 1, 1, 1])))

        metallic_roughness_texture_info = pbr_mr.get("metallicRoughnessTexture", None)
        if metallic_roughness_texture_info is not None:
            texture = self.textures[metallic_roughness_texture_info["index"]]
            image = self.images[texture["source"]]
            file = image["uri"]  # TODO: can be base64?
            metallic_roughness_texture = Texture(sourcepath=os.path.join(self.cwd, file))
        else:
            metallic_factor = pbr_mr.get("metallicFactor", 1)
            roughness_factor = pbr_mr.get("roughnessFactor", 1)
            # The metalness values are sampled from the B channel
            # The roughness values are sampled from the G channel
            metallic_roughness_texture = Texture(color=Vec4(1, roughness_factor, metallic_factor, 1))

        normal_map_texture_info = material.get("normalTexture", None)
        if normal_map_texture_info is not None:
            texture = self.textures[normal_map_texture_info["index"]]
            image = self.images[texture["source"]]
            file = image["uri"]  # TODO: can be base64?
            normal_map_texture = Texture(sourcepath=os.path.join(self.cwd, file))
        else:
            normal_map_texture = Texture(color=Vec4(0, 0, 1, 1))
        # import pdb; pdb.set_trace()

        return Material(base_color_texture, metallic_roughness_texture, normal_map_texture)

    def process_mesh(self, mi, skinned):
        mesh = self.meshes[mi]

        primitives = mesh["primitives"]
        m = Mesh()
        m.skinned = skinned

        istart = 0
        vstart = 0

        for prim in primitives:
            indices = self.get_values(prim["indices"])
            num_indices = len(indices)
            num_vertices = max(indices)[0] + 1

            triangles = [
                Triangle(indices[i + 0][0] + vstart,
                         indices[i + 1][0] + vstart,
                         indices[i + 2][0] + vstart) for i in range(0, num_indices, 3)
            ]
            m.tris.extend(triangles)

            material = self.get_material(prim.get("material"))

            subset = Subset(material, istart, num_indices)
            m.subsets.append(subset)
            istart += num_indices
            vstart += num_vertices

            attributes = prim["attributes"]
            assert attributes.get("POSITION") is not None
            assert attributes.get("NORMAL") is not None

            idx = attributes["POSITION"]
            assert self.accessors[idx]["componentType"] == FLOAT32
            assert self.accessors[idx].get("normalized", False) == False
            values = self.get_values(idx)
            positions = [Vec3(v[0], v[1], v[2]) for v in values]

            idx = attributes["NORMAL"]
            assert self.accessors[idx]["componentType"] == FLOAT32
            assert self.accessors[idx].get("normalized", False) == False
            values = self.get_values(idx)
            normals = [Vec3(v[0], v[1], v[2]) for v in values]

            # if attributes.get('TANGENT', None) is not None:
            #     pass # TODO compute if not present

            idx = attributes.get("TEXCOORD_0", None)
            if idx is not None:
                assert self.accessors[idx]["componentType"] == FLOAT32
                assert self.accessors[idx].get("normalized", False) == False
                values = self.get_values(idx)
                uvs = [Vec2(v[0], v[1]) for v in values]
            else:
                uvs = [Vec2(0, 0) for i in range(num_vertices)]

            assert len(positions) == len(normals) == len(uvs) == num_vertices

            if skinned:
                assert attributes.get("JOINTS_0") is not None
                assert attributes.get("WEIGHTS_0") is not None

                idx = attributes["JOINTS_0"]
                assert self.accessors[idx]["componentType"] == UCHAR
                assert self.accessors[idx].get("normalized", False) == False
                joints = self.get_values(idx)

                idx = attributes["WEIGHTS_0"]
                assert self.accessors[idx]["componentType"] == UCHAR
                assert self.accessors[idx]["normalized"] == True
                weights = self.get_values(idx)

                assert len(joints) == len(weights) == num_vertices

                for i in range(num_vertices):
                    m.vertices.append(Vertex(positions[i], normals[i], uvs[i], weights[i], joints[i]))
            else:
                for i in range(num_vertices):
                    m.vertices.append(Vertex(positions[i], normals[i], uvs[i]))

        return m

    def construct_bone_hierarchy(self, si):
        skin = self.skins[si]
        skeleton_index = skin["skeleton"]
        pairs = {}

        stack = [(skeleton_index, -1)]

        while stack:
            node_idx, parent_idx = stack.pop()
            assert pairs.get(node_idx) is None
            pairs[node_idx] = parent_idx

            if "children" in self.nodes[node_idx]:
                for child_idx in self.nodes[node_idx]["children"]:
                    stack.append((child_idx, node_idx))

        return pairs

    def keyframes_for(self, ai):
        animation = self.animations[ai]
        samplers = animation["samplers"]
        channels = animation["channels"]

        sampler_input_accessors = set([s["input"] for s in samplers])
        sampler_output_accessors = set([s["output"] for s in samplers])
        sampler_input_values = {}
        sampler_output_values = {}
        for s in sampler_input_accessors:
            if s not in sampler_input_values:
                sampler_input_values[s] = self.get_values(s)
        for s in sampler_output_accessors:
            if s not in sampler_output_values:
                normalized = self.accessors[s].get("normalized", False)
                sampler_output_values[s] = self.get_values(s, normalized)

        nodes_keyframes = {}
        target_nodes = set([c["target"]["node"] for c in channels])

        for node_idx in target_nodes:
            channels = [channel for channel in animation["channels"] if channel["target"]["node"] == node_idx]
            trans_channels = [channel for channel in channels if channel["target"]["path"] == "translation"]
            rot_channels = [channel for channel in channels if channel["target"]["path"] == "rotation"]
            scale_channels = [channel for channel in channels if channel["target"]["path"] == "scale"]

            keyframes = {}

            # TODO: dry these 3 ifs
            if len(trans_channels) > 0:
                assert len(trans_channels) == 1
                sampler_i = trans_channels[0]["sampler"]
                sampler = samplers[sampler_i]
                inputs = sampler_input_values[sampler["input"]]
                outputs = sampler_output_values[sampler["output"]]

                assert len(inputs) == len(outputs)
                for i in range(len(inputs)):
                    keyframes[inputs[i][0]] = {"translation": outputs[i]}

            if len(rot_channels) > 0:
                assert len(rot_channels) == 1
                sampler_i = rot_channels[0]["sampler"]
                sampler = samplers[sampler_i]
                inputs = sampler_input_values[sampler["input"]]
                outputs = sampler_output_values[sampler["output"]]

                assert len(inputs) == len(outputs)
                for i in range(len(inputs)):
                    key = inputs[i][0]
                    if key in keyframes:
                        keyframes[key]["rotation"] = outputs[i]
                    else:
                        keyframes[key] = {"rotation": outputs[i]}

            if len(scale_channels) > 0:
                assert len(scale_channels) == 1
                sampler_i = scale_channels[0]["sampler"]
                sampler = samplers[sampler_i]
                inputs = sampler_input_values[sampler["input"]]
                outputs = sampler_output_values[sampler["output"]]

                assert len(inputs) == len(outputs)
                for i in range(len(inputs)):
                    key = inputs[i][0]
                    if key in keyframes:
                        keyframes[key]["scale"] = outputs[i]
                    else:
                        keyframes[key] = {"scale": outputs[i]}

            nodes_keyframes[node_idx] = keyframes

        return nodes_keyframes

    def convert(self):
        out_meshes = []
        out_skinned_meshes = []
        out_animations = []
        out_static_transforms = None

        skin_filename = lambda i: "{}_skin_{}.skin".format(self.original_filename, i)
        outfile = lambda f: os.path.join(self.original_filename, f)
        def write_model_file():
            buf = "base_dir: {}\n".format(self.original_filename)
            buf += '#mesh: {}\n'.format(len(out_meshes))
            buf += '#skinned_mesh: {}\n'.format(len(out_skinned_meshes))
            buf += '#animations: {}\n'.format(len(out_animations))
            buf += 'static_transforms: {}\n'.format(out_static_transforms)

            for m in out_meshes:
                buf += m + '\n'

            if self.skinned:
                for m in out_skinned_meshes:
                    buf += "{};{}".format(m[0], m[1]) + '\n'
                for a in out_animations:
                    buf += "{};{}".format(a[0], a[1]) + '\n'

            with open("{}.mdl".format(self.original_filename), 'w') as f:
                f.write(buf)

        if not os.path.exists(self.original_filename):
            os.makedirs(self.original_filename)

        for i, ni in enumerate(self.mesh_nodes):
            node = self.nodes[ni]
            filename = "{}_mesh_{}.mesh".format(self.original_filename, i + 1)
            print("mesh #{}: {}".format(i + 1, filename), end="")

            skinned = self.skinned and node.get("skin") is not None

            if skinned:
                skin_idx = self.skin_indices.index(node["skin"])
                out_skinned_meshes.append([filename, skin_filename(skin_idx + 1)])
                print(" (-> skin #{})".format(skin_idx + 1))
            else:
                out_meshes.append(filename)
                print()

            m = self.process_mesh(node["mesh"], skinned)
            if self.skinned:
                m.parent_node = self.mesh_parent_nodes[ni]
            m.pack(outfile(filename))
            m.convert_textures(self.original_filename)

        if not self.skinned:
            write_model_file()
            return

        nodes_static_transforms = {}
        missing_bones = set()
        bh = {}

        # skins
        for i in self.skin_indices:
            bh = self.construct_bone_hierarchy(i)
            root_bone = self.skins[i]["skeleton"]
            joints = self.skins[i]["joints"]
            inverse_bind_matrices = self.get_values(self.skins[i]["inverseBindMatrices"])

            filename = skin_filename(i + 1)
            s = Skin(root_bone, joints, inverse_bind_matrices, bh)
            s.pack(outfile(filename))
            print("skin #{}: {}".format(i + 1, filename))

            for ni in bh.keys():
                t = {}
                t["translation"] = Vec3(*self.nodes[ni].get("translation", [0, 0, 0]))
                t["rotation"] = Vec4(*self.nodes[ni].get("rotation", [0, 0, 0, 1]))
                t["scale"] = Vec3(*self.nodes[ni].get("scale", [1, 1, 1]))

                nodes_static_transforms[ni] = t

        # animations
        for ai in range(len(self.animations)):
            animation_keyframes = self.keyframes_for(ai)
            nodes_keyframe_transforms = {}
            for node_index, kf in animation_keyframes.items():
                local_transform = {}

                for time, kf_transforms in kf.items():
                    t = {}
                    t["translation"] = Vec3(
                        *kf_transforms.get("translation", self.nodes[node_index].get("translation", [0, 0, 0]))
                    )
                    t["rotation"] = Vec4(
                        *kf_transforms.get("rotation", self.nodes[node_index].get("rotation", [0, 0, 0, 1]))
                    )
                    t["scale"] = Vec3(*kf_transforms.get("scale", self.nodes[node_index].get("scale", [1, 1, 1])))

                    local_transform[time] = t
                nodes_keyframe_transforms[node_index] = local_transform

            filename = "{}_animation_{}.anim".format(self.original_filename, ai + 1)
            a = Animation(nodes_keyframe_transforms)
            a.pack(outfile(filename))
            anim_name = self.animations[ai].get("name", "noname {}".format(ai))
            out_animations.append([filename, anim_name])
            print("animation #{}: {} -- [{}]".format(ai + 1, filename, anim_name))

            missing = set(bh.keys()) - set(nodes_keyframe_transforms.keys())
            if len(missing) > 0:
                missing_bones.update(missing)

        # wonder how does this behave with multiple different skins...
        if len(missing_bones) > 0:
            filename = "{}_transforms.bin".format(self.original_filename)
            print("transforms: {}".format(filename))
            st = StaticTransforms(missing_bones, nodes_static_transforms)
            st.pack(outfile(filename))
            out_static_transforms = filename

        write_model_file()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert a GLTF model")
    parser.add_argument("-i", "--infile", required=True, help="GLTF input file")
    parser.add_argument("-s", "--skinned", action="store_true", help="Skinned model")

    args = parser.parse_args()

    g = GLTFConvert(args.infile, args.skinned)
    g.convert()
