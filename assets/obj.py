# Export as -Z forward, Y up
import parse
import os
import argparse
from mesh import Mesh, Vec2, Vec3, Vertex, Triangle, Subset

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

class ObjMesh(Mesh):
    def from_file(self, filename):
        self.cwd = os.path.dirname(os.path.abspath(filename))
        positions = []
        normals = []
        uvs = []
        faces = []

        subset = None
        start = 0

        materials = {}

        with open(filename) as rawfile:
            while True:
                line = rawfile.readline()
                if not line:
                    break

                if line.startswith('mtllib'):
                    mtl_filename = ' '.join(line.split(' ')[1:]).strip()
                    materials |= self.read_materials(mtl_filename)

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
                    mtl_name = ' '.join(line.split(' ')[1:]).strip()
                    self.subsets.append(Subset(materials[mtl_name], start))
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
        materials = {}

        cur_mtl_name = None
        cur_mtl_tex = None

        with open(os.path.join(self.cwd, filename)) as mtl_file:
             for line in mtl_file:
                if line.startswith('newmtl'):
                    cur_mtl_name = ' '.join(line.split(' ')[1:]).strip()

                elif line.startswith('map_Kd'):
                    cur_mtl_tex = ' '.join(line.split(' ')[1:]).strip()

                if cur_mtl_name is not None and cur_mtl_tex is not None:
                    materials[cur_mtl_name] = cur_mtl_tex
                    cur_mtl_name = None
                    cur_mtl_tex = None

        return materials

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Convert obj mesh file')
    parser.add_argument('infile', help='Input file path')
    parser.add_argument('outfile', help='Output file path (optional)', nargs='?')

    args = parser.parse_args()

    if args.outfile == None:
        outfile = os.path.splitext(os.path.basename(args.infile))[0] + ".objb"
    else:
        outfile = args.outfile

    m = ObjMesh()
    m.from_file(args.infile)
    m.pack(outfile)
    m.convert_textures()
