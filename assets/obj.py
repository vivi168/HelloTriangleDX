# Export as -Z forward, Y up
# usemtl format -> usemtl image.bmp
import numpy as np
import parse
import struct
import sys
import os
import argparse
from PIL import Image

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
    def __init__(self, vertIndices=[]):
        self.vertIndices = vertIndices

    def pack(self):
        return struct.pack('<HHH', self.vertIndices[0], self.vertIndices[2], self.vertIndices[1])

    def __str__(self):
        return '{} {} {}'.format(
            self.vertIndices[0], self.vertIndices[1], self.vertIndices[2])


class ObjVert:
    def __init__(self, vi=0, ni=0, ti=0):
        self.position = vi - 1
        self.normal = ni - 1
        self.uv = ti - 1

    def __str__(self):
        return '{}/{}/{}'.format(self.position, self.normal, self.uv)


class ObjFace:
    def __init__(self, v0=ObjVert(), v1=ObjVert(), v2=ObjVert()):
        self.v0 = v0
        self.v1 = v1
        self.v2 = v2

    def __str__(self):
        return '{} {} {}'.format(self.v0, self.v1, self.v2)

MAX_TEXTURE_NAME_LEN = 64
class Subset:
    def __init__(self, texture='', start=0):
        self.start = start
        self.count = 0
        raw_texture = '{}.raw'.format(os.path.splitext(os.path.basename(texture))[0])
        if len(raw_texture) > MAX_TEXTURE_NAME_LEN - 1:
            exit('Texture name too long: {} {}/19'.format(raw_texture, len(raw_texture)))
        self.texture_name = raw_texture[:MAX_TEXTURE_NAME_LEN - 1] # limit to MAX_TEXTURE_NAME_LEN chars

    def __str__(self):
        return '{} {} ({})'.format(self.start * 3, self.count * 3, self.texture_name)

    def pack(self):
        data = struct.pack('<II', self.start * 3, self.count * 3)
        pointer = struct.pack('<Q', 0)
        return data + bytes(self.texture_name.ljust(MAX_TEXTURE_NAME_LEN, '\0'), 'ascii') + pointer

class Mesh:
    def __init__(self):
        self.cwd = ""
        self.vertices = []
        self.tris = []
        self.subsets = []

    def from_file(self, filename):
        self.cwd = os.path.dirname(os.path.abspath(filename))
        positions = []
        normals = []
        uvs = []
        faces = []

        subset = None
        start = 0

        materials = []

        with open(filename) as rawfile:
            while True:
                line = rawfile.readline()
                if not line:
                    break

                if line.startswith('mtllib'):
                    mtl_filename = ' '.join(line.split(' ')[1:]).strip()
                    materials.append(self.read_materials(mtl_filename))

                elif line.startswith('v '):
                    data = parse.search('v {px:g} {py:g} {pz:g}', line)

                    px = data['px']
                    py = data['py']
                    pz = data['pz']
                    positions.append(Vec3(px, py, -pz))

                elif line.startswith('vt '):
                    data = parse.search('vt {tu:g} {tv:g}', line)
                    uvs.append(Vec2(data['tu'], 1 - data['tv']))

                elif line.startswith('vn '):
                    data = parse.search('vn {nx:g} {ny:g} {nz:g}', line)
                    nx = data['nx']
                    ny = data['ny']
                    nz = data['nz']
                    normals.append(Vec3(nx, ny, -nz))

                elif line.startswith('usemtl'):
                    # TODO: read MTL and find the real name of the texture.
                    # so we don't need to butcher the obj file
                    data = parse.search('usemtl {:S}', line)
                    self.subsets.append(Subset(data[0], start))
                    subset = len(self.subsets) - 1

                elif line.startswith('f '):
                    data = parse.search('f {v0_vi:d}/{v0_vti:d}/{v0_vni:d} {v1_vi:d}/{v1_vti:d}/{v1_vni:d} {v2_vi:d}/{v2_vti:d}/{v2_vni:d}', line)
                    v0 = ObjVert(data['v0_vi'], data['v0_vni'], data['v0_vti'])
                    v1 = ObjVert(data['v1_vi'], data['v1_vni'], data['v1_vti'])
                    v2 = ObjVert(data['v2_vi'], data['v2_vni'], data['v2_vti'])

                    if subset == None: exit('Error - no subset')

                    faces.append(ObjFace(v0, v1, v2))
                    self.subsets[subset].count += 1
                    start += 1

        def find_or_add_vertex(v):
            try:
                i = self.vertices.index(v)
            except ValueError:
                self.vertices.append(v)
                i = len(self.vertices) - 1
            return i

        for f in faces:
            # ---- v0
            v0 = Vertex(positions[f.v0.position], normals[f.v0.normal], uvs[f.v0.uv])
            v0_i = find_or_add_vertex(v0)

            # ---- v1
            v1 = Vertex(positions[f.v1.position], normals[f.v1.normal], uvs[f.v1.uv])
            v1_i = find_or_add_vertex(v1)

            # ---- v2
            v2 = Vertex(positions[f.v2.position], normals[f.v2.normal], uvs[f.v2.uv])
            v2_i = find_or_add_vertex(v2)

            self.tris.append(Triangle([v0_i, v1_i, v2_i]))

    def read_materials(self, filename):
        materials = []

        cur_mtl_name = None
        cur_mtl_tex = None

        with open(os.path.join(self.cwd, filename)) as mtl_file:
             for line in mtl_file:
                print(line)

                if line.startswith('newmtl'):
                    cur_mtl_name = ' '.join(line.split(' ')[1:]).strip()

                elif line.startswith('map_Kd'):
                    cur_mtl_tex = ' '.join(line.split(' ')[1:]).strip()

                if cur_mtl_name is not None and cur_mtl_tex is not None:
                    materials.append({ 'name': cur_mtl_name, 'texture': cur_mtl_tex })
                    cur_mtl_name = None
                    cur_mtl_tex = None

        return materials

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


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Convert obj mesh file')
    parser.add_argument('infile', help='Input file path')
    parser.add_argument('outfile', help='Output file path (optional)', nargs='?')

    args = parser.parse_args()

    if args.outfile == None:
        outfile = os.path.splitext(os.path.basename(args.infile))[0] + ".objb"
    else:
        outfile = args.outfile

    m = Mesh()
    m.from_file(args.infile)
    m.pack(outfile)
