#!/usr/bin/env python3
"""Geometry / skeleton export for Baroque .IGB files (Wavefront OBJ).

Usage: python -I tools/igb_geom.py FILE.IGB OUT.obj
Writes OUT.obj (all igGeometry meshes; 'v' lines carry RGB vertex colour as the optional
4th..6th values), OUT_skel.obj (bone joints as points + parent/child 'l' segments) when an
igSkeleton is present, and OUT_skin.json (per-mesh bone weights/indices + bone table).

Vertex layout (inferred from the data, see IGB_FORMAT.md): igVertexArray1_1._vdata is a memory
block of 20 igMemoryRef slots; stream 0 = position (vec3f), 1 = normal (vec3f), 2 = colour
(RGBA8, PS2 alpha), 11 = texcoord 0 (vec2f); the remaining streams are unused in the shipped files
(asserted by --stats).
"""
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import igb  # noqa: E402

STREAM_POS, STREAM_NRM, STREAM_COL, STREAM_UV0 = 0, 1, 2, 11
PRIM_TRIANGLE_STRIP = 4  # igGeometryAttr._primType; every shipped file uses 4 with strip lengths
PRIM_TRIANGLES = 3


def mat_identity():
    return [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]


def mat_mul(a, b):
    """Row-vector convention (Alchemy stores translation in m[12..14]): result = a * b."""
    r = [0.0] * 16
    for i in range(4):
        for j in range(4):
            r[i * 4 + j] = sum(a[i * 4 + k] * b[k * 4 + j] for k in range(4))
    return r


def xform_point(m, p):
    x, y, z = p
    return (m[0] * x + m[4] * y + m[8] * z + m[12],
            m[1] * x + m[5] * y + m[9] * z + m[13],
            m[2] * x + m[6] * y + m[10] * z + m[14])


def xform_normal(m, n):
    x, y, z = n
    v = (m[0] * x + m[4] * y + m[8] * z, m[1] * x + m[5] * y + m[9] * z, m[2] * x + m[6] * y + m[10] * z)
    l = math.sqrt(sum(c * c for c in v)) or 1.0
    return tuple(c / l for c in v)


def mat_inverse(m):
    """General 4x4 inverse (Gauss-Jordan)."""
    a = [list(m[i * 4:i * 4 + 4]) + [1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]
    for c in range(4):
        p = max(range(c, 4), key=lambda r: abs(a[r][c]))
        if abs(a[p][c]) < 1e-12:
            return None
        a[c], a[p] = a[p], a[c]
        f = a[c][c]
        a[c] = [v / f for v in a[c]]
        for r in range(4):
            if r != c and a[r][c]:
                g = a[r][c]
                a[r] = [rv - g * cv for rv, cv in zip(a[r], a[c])]
    return [a[i][4 + j] for i in range(4) for j in range(4)]


def floats(mem, n):
    return struct.unpack_from('<%df' % n, mem.data, 0)


class Mesh:
    def __init__(self, name):
        self.name = name
        self.positions = []
        self.normals = []
        self.colors = []
        self.uvs = []
        self.faces = []          # (i0, i1, i2) local indices
        self.weights = []        # per vertex [4 floats]
        self.bone_indices = []   # per vertex [4 ints]
        self.texture = None
        self.geometry_slot = None
        self.vertex_array_slot = None
        self.prim_type = None


def strips_to_tris(lengths, offset=0):
    tris = []
    base = offset
    for n in lengths:
        for k in range(n - 2):
            a, b, c = base + k, base + k + 1, base + k + 2
            if a == b or b == c or a == c:
                continue
            tris.append((a, c, b) if k & 1 else (a, b, c))
        base += n
    return tris


def decode_vertex_array(va):
    """Return dict of streams from an igVertexArray1_1 object."""
    n = va.fields['_numVerts']
    vd = va.fields['_vdata']
    out = dict(n=n, streams={})
    if vd is None or vd.refs is None:
        raise igb.IGBError('vertex array without stream table')
    for i, m in enumerate(vd.refs):
        if m is None:
            continue
        if n == 0:
            continue
        if i == STREAM_POS:
            assert m.size == 12 * n, (m.size, n)
            f = floats(m, 3 * n)
            out['pos'] = [f[j:j + 3] for j in range(0, 3 * n, 3)]
        elif i == STREAM_NRM:
            assert m.size == 12 * n, (m.size, n)
            f = floats(m, 3 * n)
            out['nrm'] = [f[j:j + 3] for j in range(0, 3 * n, 3)]
        elif i == STREAM_COL:
            assert m.size == 4 * n, (m.size, n)
            out['col'] = [tuple(m.data[j:j + 4]) for j in range(0, 4 * n, 4)]
        elif i == STREAM_UV0:
            assert m.size == 8 * n, (m.size, n)
            f = floats(m, 2 * n)
            out['uv'] = [f[j:j + 2] for j in range(0, 2 * n, 2)]
        else:
            out['streams'][i] = m
    bw = va.fields.get('_blendWeights')
    bi = va.fields.get('_blendIndices')
    if bw is not None and bw.size == 16 * n:
        f = floats(bw, 4 * n)
        out['weights'] = [f[j:j + 4] for j in range(0, 4 * n, 4)]
    if bi is not None and bi.size == 4 * n:
        out['bones'] = [tuple(bi.data[j:j + 4]) for j in range(0, 4 * n, 4)]
    return out


def geometry_faces(ga, n):
    pt = ga.fields.get('_primType')
    off = ga.fields.get('_offset', 0) or 0
    lengths = None
    for key in ('_stripLengths', '_primLengths'):
        pl = ga.fields.get(key)
        if pl is not None:
            ld = pl.fields['_lengthData']
            cnt = pl.fields['_numStrips']
            lengths = list(struct.unpack_from('<%dI' % cnt, ld.data, 0))
            break
    idx = ga.fields.get('_indexArray')
    if idx is not None:
        raise igb.IGBError('indexed geometry not supported (igGeometryAttr._indexArray set)')
    if pt == PRIM_TRIANGLE_STRIP:
        if lengths is None:
            lengths = [n - off]
        return strips_to_tris(lengths, off)
    if pt == PRIM_TRIANGLES:
        return [(off + i, off + i + 1, off + i + 2) for i in range(0, n - off - 2, 3)]
    raise igb.IGBError('unsupported primitive type %r' % pt)


def node_matrix(node):
    """Local matrix of a scene-graph node, if it carries one (igTransform / igActor._transform)."""
    for key in ('_m', '_matrix', '_transform', '_localTransform'):
        v = node.fields.get(key)
        if isinstance(v, list) and len(v) == 16:
            return list(v)
    return None


def texture_name(attr_list):
    if attr_list is None or attr_list.fields.get('_data') is None or attr_list.fields['_data'].refs is None:
        return None
    for a in attr_list.fields['_data'].refs:
        if a is None:
            continue
        if a.cls == 'igTextureBindAttr':
            t = a.fields.get('_texture')
            if isinstance(t, igb.IgExternal):
                return t.fields.get('_key') or t.fields.get('_entryName')
            if t is not None and hasattr(t, 'fields'):
                img = t.fields.get('_image')
                if img is not None and hasattr(img, 'fields'):
                    return img.fields.get('_pNameString')
    return None


def parent_map(f):
    """child slot -> parent node, from every node's _childList / _transformGraph / _skinnedGraph."""
    pm = {}
    for o in f.objects:
        kids = []
        cl = o.fields.get('_childList')
        if isinstance(cl, igb.IgObject) and cl.fields.get('_data') is not None and cl.fields['_data'].refs:
            kids.extend(cl.fields['_data'].refs)
        for key in ('_transformGraph', '_skinnedGraph'):
            if isinstance(o.fields.get(key), igb.IgObject):
                kids.append(o.fields[key])
        for k in kids:
            if isinstance(k, igb.IgObject):
                pm.setdefault(k.slot, o)
    return pm


def inherited_texture(node, pm):
    """Nearest igTextureBindAttr on the node or its ancestors (texture state is inherited down the graph)."""
    seen = set()
    while node is not None and node.slot not in seen:
        seen.add(node.slot)
        t = texture_name(node.fields.get('_attributes')) if node.meta.is_a('igAttrSet') else None
        if t:
            return t
        node = pm.get(node.slot)
    return None


def collect_meshes(f):
    """Walk every scene graph in the file and return a list of Mesh in world (bind) space."""
    meshes = []
    visited = set()
    pm = parent_map(f)

    def visit(node, m, tex=None):
        if node is None or not isinstance(node, igb.IgObject):
            return
        key = node.slot
        if key in visited:
            return
        visited.add(key)
        lm = node_matrix(node)
        if lm is not None:
            m = mat_mul(lm, m)
        if node.meta.is_a('igAttrSet'):
            attrs = node.fields.get('_attributes')
            tex = texture_name(attrs) or tex or inherited_texture(node, pm)
            if attrs is not None and attrs.fields.get('_data') is not None and attrs.fields['_data'].refs:
                for a in attrs.fields['_data'].refs:
                    if not isinstance(a, igb.IgObject):
                        continue
                    if a.meta.is_a('igGeometryAttr'):
                        meshes.append(build_mesh(node, a, m, tex))
                    elif a.meta.is_a('igGeometrySetAttr'):
                        gl = a.fields.get('_geometryAttrList')
                        if gl is not None and gl.fields.get('_data') is not None and gl.fields['_data'].refs:
                            for ga in gl.fields['_data'].refs:
                                if isinstance(ga, igb.IgObject) and ga.meta.is_a('igGeometryAttr'):
                                    meshes.append(build_mesh(node, ga, m, tex))
            tg = node.fields.get('_transformGraph')
            if tg is not None:
                visit(tg, m, tex)
        # children
        cl = node.fields.get('_childList')
        if cl is not None and hasattr(cl, 'fields') and cl.fields.get('_data') is not None and cl.fields['_data'].refs:
            for c in cl.fields['_data'].refs:
                visit(c, m, tex)
        # actor appearance -> skins -> skinned graphs
        app = node.fields.get('_appearance')
        if app is not None and isinstance(app, igb.IgObject):
            for k, v in app.fields.items():
                if isinstance(v, igb.IgObject) and v.meta.is_a('igObjectList') and v.fields.get('_data') is not None \
                        and v.fields['_data'].refs:
                    for item in v.fields['_data'].refs:
                        if isinstance(item, igb.IgObject) and item.meta.is_a('igSkin'):
                            visit(item.fields.get('_skinnedGraph'), m, tex)
                        else:
                            visit(item, m, tex)
        sg = node.fields.get('_skinnedGraph')
        if sg is not None:
            visit(sg, m, tex)

    roots = []
    for si in f.find('igSceneInfo'):
        roots.append(si.fields.get('_sceneGraph'))
        sgl = si.fields.get('_sceneGraphList')
        if sgl is not None and sgl.fields.get('_data') is not None and sgl.fields['_data'].refs:
            roots.extend(sgl.fields['_data'].refs)
    for r in roots:
        visit(r, mat_identity())
    # geometry not reachable from a scene graph (e.g. referenced only from other files)
    for g in f.find('igGeometry'):
        if g.slot not in visited:
            visit(g, mat_identity())
    return meshes


def build_mesh(node, ga, m, tex):
    va = ga.fields['_vertexArray']
    d = decode_vertex_array(va)
    n = d['n']
    mesh = Mesh(node.fields.get('_name') or 'geometry%d' % node.slot)
    mesh.geometry_slot = node.slot
    mesh.vertex_array_slot = va.slot
    mesh.texture = tex
    mesh.prim_type = ga.fields.get('_primType')
    pos = d.get('pos') or [(0.0, 0.0, 0.0)] * n
    mesh.positions = [xform_point(m, p) for p in pos]
    if 'nrm' in d:
        mesh.normals = [xform_normal(m, v) for v in d['nrm']]
    mesh.colors = d.get('col', [])
    mesh.uvs = d.get('uv', [])
    mesh.weights = d.get('weights', [])
    mesh.bone_indices = d.get('bones', [])
    # drop zero-area strip bridges (duplicated vertices with identical positions)
    mesh.faces = [t for t in geometry_faces(ga, n)
                  if not (pos[t[0]] == pos[t[1]] or pos[t[1]] == pos[t[2]] or pos[t[0]] == pos[t[2]])]
    return mesh


def skeleton_info(f):
    sks = f.find('igSkeleton')
    if not sks:
        return None
    sk = sks[0]
    bl = sk.fields['_boneInfoList']
    bones = []
    for b in bl.fields['_data'].refs:
        bones.append(dict(name=b.fields['_name'], parent=b.fields['_parentIdx'], bmIdx=b.fields['_bmIdx'],
                          flags=b.fields['_flags']))
    tr = sk.fields.get('_boneTranslationArray')
    if tr is not None and tr.size == 12 * len(bones):
        fl = floats(tr, 3 * len(bones))
        for i, b in enumerate(bones):
            b['translation'] = fl[3 * i:3 * i + 3]
    inv = sk.fields.get('_invJointArray')
    jc = sk.fields.get('_jointCount', 0)
    inv_mats = []
    if inv is not None and inv.size == 64 * jc:
        fl = floats(inv, 16 * jc)
        inv_mats = [list(fl[16 * i:16 * i + 16]) for i in range(jc)]
    # world joint positions: bind matrix = inverse(inverse-bind); translation row m[12..14]
    for b in bones:
        bi = b['bmIdx']
        if 0 <= bi < len(inv_mats):
            bm = mat_inverse(inv_mats[bi])
            b['bind_world'] = bm[12:15] if bm else None
    # fall back to accumulating local translations for bones without a matrix
    def world(i, seen=()):
        b = bones[i]
        if b.get('bind_world'):
            return b['bind_world']
        t = b.get('translation', (0.0, 0.0, 0.0))
        if b['parent'] < 0 or i in seen:
            return tuple(t)
        p = world(b['parent'], seen + (i,))
        return tuple(a + c for a, c in zip(p, t))
    for i, b in enumerate(bones):
        b['world'] = tuple(world(i))
    return dict(skeleton_slot=sk.slot, joint_count=jc, bones=bones, inverse_bind=inv_mats)


def export_obj(f, out_path):
    meshes = collect_meshes(f)
    base = os.path.splitext(out_path)[0]
    with open(out_path, 'w', encoding='utf-8') as o:
        o.write('# %s: %d meshes, exported by tools/igb_geom.py\n' % (os.path.basename(f.path), len(meshes)))
        o.write('mtllib %s.mtl\n' % os.path.basename(base))
        vbase = 1
        tex_names = {}
        for mi, me in enumerate(meshes):
            o.write('o %s_%d\n' % (me.name.replace(' ', '_'), me.geometry_slot))
            if me.texture:
                mt = 'tex_' + ''.join(ch if ch.isalnum() else '_' for ch in str(me.texture).split()[0])
                tex_names[mt] = me.texture
                o.write('usemtl %s\n' % mt)
            has_c = len(me.colors) == len(me.positions)
            for i, p in enumerate(me.positions):
                if has_c:
                    c = me.colors[i]
                    o.write('v %.6f %.6f %.6f %.4f %.4f %.4f\n' % (p[0], p[1], p[2], c[0] / 255.0, c[1] / 255.0, c[2] / 255.0))
                else:
                    o.write('v %.6f %.6f %.6f\n' % p)
            has_t = len(me.uvs) == len(me.positions)
            has_n = len(me.normals) == len(me.positions)
            for uv in me.uvs:
                o.write('vt %.6f %.6f\n' % (uv[0], 1.0 - uv[1]))
            for nn in me.normals:
                o.write('vn %.6f %.6f %.6f\n' % nn)
            for (a, b, c) in me.faces:
                idx = []
                for k in (a, b, c):
                    v = vbase + k
                    if has_t and has_n:
                        idx.append('%d/%d/%d' % (v, v, v))
                    elif has_t:
                        idx.append('%d/%d' % (v, v))
                    elif has_n:
                        idx.append('%d//%d' % (v, v))
                    else:
                        idx.append('%d' % v)
                o.write('f %s\n' % ' '.join(idx))
            vbase += len(me.positions)
    with open(base + '.mtl', 'w', encoding='utf-8') as o:
        for mt, tex in tex_names.items():
            o.write('newmtl %s\nKd 1 1 1\n# texture %s\n\n' % (mt, tex))
    skel = skeleton_info(f)
    if skel:
        with open(base + '_skel.obj', 'w', encoding='utf-8') as o:
            o.write('# skeleton of %s: %d bones, %d joint matrices\n' % (os.path.basename(f.path), len(skel['bones']), skel['joint_count']))
            for b in skel['bones']:
                o.write('# bone %s parent=%d bmIdx=%d flags=%d\n' % (b['name'], b['parent'], b['bmIdx'], b['flags']))
                o.write('v %.6f %.6f %.6f\n' % tuple(b['world']))
            for i, b in enumerate(skel['bones']):
                if b['parent'] >= 0:
                    o.write('l %d %d\n' % (b['parent'] + 1, i + 1))
                else:
                    o.write('p %d\n' % (i + 1))
    skin = dict(file=os.path.basename(f.path), meshes=[], skeleton=skel)
    for me in meshes:
        skin['meshes'].append(dict(name=me.name, geometry_slot=me.geometry_slot, vertex_array_slot=me.vertex_array_slot,
                                   texture=me.texture, vertices=len(me.positions), faces=len(me.faces),
                                   prim_type=me.prim_type,
                                   weights=[list(w) for w in me.weights], bone_indices=[list(b) for b in me.bone_indices]))
    with open(base + '_skin.json', 'w', encoding='utf-8') as o:
        json.dump(skin, o)
    return meshes, skel


def stats(root):
    """Scan every file: which vertex streams are used, strip/primitive types, index arrays."""
    from collections import Counter
    streams = Counter()
    prims = Counter()
    fmts = Counter()
    sizes = Counter()
    idx = 0
    nva = 0
    for p in igb.iter_igb(root):
        try:
            f = igb.IGBFile(p)
        except Exception as e:  # noqa: BLE001
            print('ERR', p, e)
            continue
        for va in f.find('igVertexArray'):
            nva += 1
            n = va.fields['_numVerts']
            vd = va.fields.get('_vdata')
            fmts[va.fields.get('_vertexFormat', b'').hex() if isinstance(va.fields.get('_vertexFormat'), bytes) else None] += 1
            if vd is not None and vd.refs is not None:
                for i, m in enumerate(vd.refs):
                    if m is not None:
                        streams[(i, m.elem_type, (m.size // n) if n else None)] += 1
            for k in ('_blendWeights', '_blendIndices', '_pointSpriteSize'):
                m = va.fields.get(k)
                if m is not None:
                    sizes[(k, m.elem_type, (m.size // n) if n else None)] += 1
        for ga in f.find('igGeometryAttr'):
            prims[(ga.fields.get('_primType'), ga.fields.get('_stripLengths') is not None, ga.fields.get('_primLengths') is not None,
                   ga.fields.get('_indexArray') is not None, ga.fields.get('_offset'))] += 1
            if ga.fields.get('_indexArray') is not None:
                idx += 1
    print('vertex arrays', nva)
    print('streams (index, elem type, bytes/vertex):', dict(streams))
    print('blend/sprite:', dict(sizes))
    print('vertex formats:', dict(fmts))
    print('prims (primType, hasStripLengths, hasPrimLengths, hasIndexArray, offset):', dict(prims))


if __name__ == '__main__':
    if sys.argv[1] == '--stats':
        stats(sys.argv[2])
    else:
        ff = igb.IGBFile(sys.argv[1])
        ms, sk = export_obj(ff, sys.argv[2])
        print('wrote %s: %d meshes, %d vertices, %d faces%s' % (
            sys.argv[2], len(ms), sum(len(m.positions) for m in ms), sum(len(m.faces) for m in ms),
            ', %d bones' % len(sk['bones']) if sk else ''))
