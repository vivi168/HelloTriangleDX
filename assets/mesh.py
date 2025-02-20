import struct
import os
from raw import RawImage

from typing import List

MAX_TEXTURE_NAME_LEN = 64

class Vec2:
    def __init__(self, x=0, y=0):
        self.x = x
        self.y = y

    def pack_u16(self):
        return struct.pack('<HH', self.x, self.y)

    def pack_f32(self):
        return struct.pack('<ff', self.x, self.y)

    def __str__(self):
        return '{:.6f} {:.6f}'.format(self.x, self.y)

    def __eq__(self, other):
        return self.x == other.x and self.y == other.y


class Vec3:
    def __init__(self, x=0, y=0, z=0):
        self.x = x
        self.y = y
        self.z = z

    def pack_s8(self):
        return struct.pack('<bbb', self.x, self.y, self.z)

    def pack_u16(self):
        return struct.pack('<HHH', self.x, self.y, self.z)

    def pack_f32(self):
        return struct.pack('<fff', self.x, self.y, self.z)

    def __str__(self):
        return '{:.6f} {:.6f} {:.6f}'.format(self.x, self.y, self.z)

    def __eq__(self, other):
        return self.x == other.x and self.y == other.y and self.z == other.z


class Vec4:
    def __init__(self, x=0, y=0, z=0, w=0):
        self.x = x
        self.y = y
        self.z = z
        self.w = w

    def pack(self):
        return struct.pack('<ffff', self.x, self.y, self.z, self.w)

    def __str__(self):
        return '{:.6f} {:.6f} {:.6f} {:.6f}'.format(self.x, self.y, self.z, self.w)

    def __eq__(self, other):
        return self.x == other.x and self.y == other.y and self.z == other.z and self.w == other.w


class Vertex:
    def __init__(self, p=Vec3(), n=Vec3(), u=Vec2()):
        self.position = p
        self.normal = n
        self.uv = u

    def pack(self):
        # positions (UINT16): p * scale + translation
        # normals (SNORM8): n / 128.0
        # color (UNORM8): n / 255.0
        # uvs (UNORM16): t / 65535.0  * scale + translation (of subset)
        # return self.position.pack_u16() + self.normal.pack_s8() + Vec4().pack() + self.uv.pack_u16()
        return self.position.pack_f32() + self.normal.pack_f32() + Vec4().pack() + self.uv.pack_f32()

    def __str__(self):
        return '({}) ({}) ({})'.format(self.position, self.normal, self.uv)

    def __eq__(self, other):
        return self.position == other.position and self.normal == other.normal and self.uv == other.uv


class Triangle:
    def __init__(self, vi1=0, vi2=0, vi3=0):
        self.vertIndices = [vi1, vi2, vi3]

    def pack(self):
        return struct.pack('<HHH', self.vertIndices[0], self.vertIndices[1], self.vertIndices[2])

    def __str__(self):
        return '{} {} {}'.format(
            self.vertIndices[0], self.vertIndices[1], self.vertIndices[2])

class Material:
    def __init__(self, texture, offset=Vec2(), scale=Vec2(), base_color_factor=Vec4(1,1,1,1)):
        self.offset = offset
        self.scale = scale
        self.base_color_factor = base_color_factor

        raw_texture = '{}.raw'.format(os.path.splitext(os.path.basename(texture))[0])
        if len(raw_texture) > MAX_TEXTURE_NAME_LEN - 1: # null terminated
            exit('Texture name too long: {} {}/{}'.format(raw_texture, len(raw_texture)), MAX_TEXTURE_NAME_LEN)
        self.original_texture_name = texture
        self.texture_name = raw_texture

    def pack(self):
        # data = self.offset.pack_f32() + self.scale.pack_f32()
        # return data + bytes(self.texture_name.ljust(MAX_TEXTURE_NAME_LEN, '\0'), 'ascii')
        return bytes(self.texture_name.ljust(MAX_TEXTURE_NAME_LEN, '\0'), 'ascii')

    def convert_texture(self):
        m = RawImage(self.original_texture_name, None)
        with open(self.texture_name, 'wb') as f:
            f.write(m.pack())

class Subset:
    def __init__(self, material, istart=0, icount=0, vstart=0):
        self.material:Material = material
        self.istart = istart
        self.icount = icount
        self.vstart = vstart # offset in the vertex array

    def __str__(self):
        return '{} {}/{} ({})'.format(self.istart, self.icount, self.vstart, self.material.texture_name)

    def pack(self):
        data = struct.pack('<IIII', self.istart, self.icount, self.vstart, 0)
        pointer = struct.pack('<Q', 0) # pointer to material
        return data + self.material.pack() + pointer

class Mesh:
    def __init__(self, translation=Vec3(), scale=Vec3()):
        self.cwd = ""
        self.vertices: List[Vertex] = []
        self.tris: List[Triangle] = []
        self.subsets: List[Subset] = []

        self.translation = translation
        self.scale = scale

    def pack(self, outfile):
        data = bytearray()

        print('*** Header ***')
        print(len(self.vertices), len(self.tris) * 3, len(self.subsets))
        data += struct.pack('<III', len(self.vertices), len(self.tris) * 3, len(self.subsets))

        print('*** Vertices ***')
        for v in self.vertices:
            # print(v)
            data += v.pack()

        print('*** Triangles ***')
        for t in self.tris:
            # print(t)
            data += t.pack()

        print('*** Subsets ***')
        for s in self.subsets:
            # print(s)
            data += s.pack()

        with open(outfile, 'wb') as f:
            f.write(data)

    def convert_textures(self):
        for s in self.subsets:
            m = s.material
            print("{} -> {}".format(m.original_texture_name, m.texture_name))
            m.convert_texture()
