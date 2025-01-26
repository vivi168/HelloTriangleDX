import struct
import os
from raw import RawImage

from typing import List

MAX_TEXTURE_NAME_LEN = 64

class Vec2:
    def __init__(self, x=0, y=0):
        self.x = x
        self.y = y

    def pack(self):
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

    def pack(self):
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
    def __init__(self, p=Vec3(), n=Vec3(), t=Vec2()):
        self.position = p
        self.normal = n
        self.color = Vec4(1, 0, 1, 1)
        self.uv = t

    def pack(self):
        return self.position.pack() + self.normal.pack() + self.color.pack() + self.uv.pack()

    def __str__(self):
        return '({}) ({}) ({})'.format(self.position, self.normal, self.uv)

    def __eq__(self, other):
        return self.position == other.position and self.normal == other.normal and self.uv == other.uv


class Triangle:
    def __init__(self, vi1=0, vi2=0, vi3=0):
        self.vertIndices = [vi1, vi2, vi3]

    def pack(self):
        return struct.pack('<HHH', self.vertIndices[0], self.vertIndices[2], self.vertIndices[1])

    def __str__(self):
        return '{} {} {}'.format(
            self.vertIndices[0], self.vertIndices[1], self.vertIndices[2])


class Subset:
    def __init__(self, texture='', istart=0, icount = 0, vstart=0):
        self.start = istart
        self.count = icount
        self.vstart =vstart
        raw_texture = '{}.raw'.format(os.path.splitext(os.path.basename(texture))[0])
        if len(raw_texture) > MAX_TEXTURE_NAME_LEN - 1:
            exit('Texture name too long: {} {}/{}'.format(raw_texture, len(raw_texture)), MAX_TEXTURE_NAME_LEN)
        self.original_texture_name = texture
        self.texture_name = raw_texture[:MAX_TEXTURE_NAME_LEN - 1]

    def __str__(self):
        return '{} {} {}({})'.format(self.start, self.count, self.vstart, self.texture_name)

    def pack(self):
        data = struct.pack('<II', self.start, self.count)
        pointer = struct.pack('<Q', 0)
        return data + bytes(self.texture_name.ljust(MAX_TEXTURE_NAME_LEN, '\0'), 'ascii') + pointer

    def convert_texture(self):
        m = RawImage(self.original_texture_name, None)
        with open(self.texture_name, 'wb') as f:
            f.write(m.pack())


class Mesh:
    def __init__(self):
        self.cwd = ""
        self.vertices: List[Vertex] = []
        self.tris: List[Triangle] = []
        self.subsets: List[Subset] = []

    def pack(self, outfile):
        data = bytearray()

        print('*** Header ***')
        print(len(self.vertices), len(self.tris) * 3, len(self.subsets))
        data += struct.pack('<III', len(self.vertices), len(self.tris) * 3, len(self.subsets))

        print('*** Vertices ***')
        for v in self.vertices:
            print(v)
            data += v.pack()

        print('*** Triangles ***')
        for t in self.tris:
            print(t)
            data += t.pack()

        print('*** Subsets ***')
        for s in self.subsets:
            print(s)
            data += s.pack()

        with open(outfile, 'wb') as f:
            f.write(data)

    def convert_textures(self):
        for s in self.subsets:
            print("{} -> {}".format(s.original_texture_name, s.texture_name))
            s.convert_texture()
