"""Sample one Enbaya animation of a character IGB and write posed skeletons for eyeballing.

    python -I tools/anim_export.py [FILE.IGB] [--anim NAME|INDEX] [--step 10] [--out DIR] [--all-frames]

Defaults: work/assets/DATA/CHR/C00/C00A.IGB, animation 0, frames 0,10,20,... (plus the last frame),
output work/igb_preview/anim/.  For every sampled frame an OBJ point/line set <anim>_fNNNN.obj is
written (one vertex per bone in world space, `p` for joints, `l` parent->child), plus <anim>.csv
with the local rotation/translation and world position of a handful of bones per frame, and
<anim>_summary.txt (ranges, NaN count, root motion).

The pose is computed exactly the way the runtime does it:
  * igAnimationBinding._boneTrackIdxArray[bone] -> track; track._source._trackId -> stream bone.
  * igEnbayaAnimationSource is sampled through enbaya.EnbayaSource.evaluate(ns) (play mode,
    interpolation methods, frame = 0x1fca055 ns).
  * translation: the animated one if bit[track] of igAnimation._useAnimationTransBoolArray is set,
    otherwise igSkeleton._boneTranslationArray[bone]  (igAnimationCombiner, FUN_003c6240).
  * local matrix = FUN_0039fc50(quat) with the translation in row 3  (FUN_003c5550).
  * world[i] = local[i] x world[parent] for bones 1..n-1, bones whose parent is bone 0 use local
    directly (igSkeleton FUN_003f66a0, FUN_0039d3d0 row-vector multiply); blend[k] = inv[k] x world.
"""
import csv
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from igb import IGBFile  # noqa: E402
from igb_geom import skeleton_info  # noqa: E402
from enbaya import EnbayaSource, fmul, fadd, fsub, fmadd  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FRAME_NS = 0x1fca055            # 33,333,333 ns = one 30 Hz frame


def quat_to_matrix(q, t):
    """FUN_0039fc50 (rotation 3x3) + translation row, row-major 4x4, row-vector convention."""
    x, y, z, w = q
    y2 = fmul(y, 2.0)
    xw2 = fmul(fmul(x, 2.0), w)
    xx2 = fmul(fmul(x, 2.0), x)
    z2 = fmul(z, 2.0)
    zz2 = fmul(z2, z)
    m = [0.0] * 16
    m[0] = fsub(1.0, fadd(fmul(y2, y), zz2))
    m[1] = fsub(fmul(y2, x), fmul(z2, w))
    m[2] = fadd(fmul(z2, x), fmul(y2, w))
    m[4] = fadd(fmul(y2, x), fmul(z2, w))
    m[5] = fsub(1.0, fadd(xx2, zz2))
    m[6] = fsub(fmul(z2, y), xw2)
    m[8] = fsub(fmul(z2, x), fmul(y2, w))
    m[9] = fadd(fmul(z2, y), xw2)
    m[10] = fsub(1.0, fadd(xx2, fmul(y2, y)))
    m[12], m[13], m[14] = t[0], t[1], t[2]
    m[15] = 1.0
    return m


def mat_mul(a, b):
    """FUN_0039d3d0: out row r = a[r].x*b0 + a[r].y*b1 + a[r].z*b2 + a[r].w*b3 (row vectors)."""
    out = [0.0] * 16
    for r in range(4):
        ax, ay, az, aw = a[4 * r:4 * r + 4]
        for c in range(4):
            acc = fmul(b[c], ax)
            acc = fmadd(acc, b[4 + c], ay)
            acc = fmadd(acc, b[8 + c], az)
            out[4 * r + c] = fmadd(acc, b[12 + c], aw)
    return out


IDENTITY = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]


def load_animation(f, which):
    anims = f.find('igAnimation')
    if not anims:
        raise SystemExit('no igAnimation in file')
    if isinstance(which, int) or (isinstance(which, str) and which.isdigit()):
        return anims[int(which)], anims
    for a in anims:
        if a.fields.get('_name') == which:
            return a, anims
    raise SystemExit('animation %r not found; have %s' % (which, [a.fields.get('_name') for a in anims]))


def binding_for(anim, skel_slot):
    bl = anim.fields.get('_bindingList')
    binds = bl.fields['_data'].refs if bl is not None and bl.fields['_data'] is not None else []
    for b in binds:
        h = b.fields.get('_hierarchy')
        if h is None or h.slot == skel_slot:
            return b
    return binds[0] if binds else None


def pose_at(src, anim, binding, skel, time_ns):
    """Return (locals, worlds, blends, per-bone (quat, trans, track))."""
    bones = skel['bones']
    n = len(bones)
    arr = binding.fields['_boneTrackIdxArray']
    track_of = list(struct.unpack('<%di' % (arr.size // 4), arr.data)) if arr is not None else list(range(n))
    bm = anim.fields.get('_useAnimationTransBoolArray')
    words = ()
    if bm is not None and bm.fields.get('_data') is not None:
        words = struct.unpack('<%dI' % (bm.fields['_data'].size // 4), bm.fields['_data'].data)
    tracks = anim.fields['_trackList'].fields['_data'].refs

    def use_anim_trans(track_idx):
        if not words:
            return False
        w = track_idx >> 5
        return w < len(words) and (words[w] >> (track_idx & 31)) & 1 == 1

    locals_, info = [], []
    for i, b in enumerate(bones):
        skel_t = list(b.get('translation', (0.0, 0.0, 0.0)))
        ti = track_of[i] if i < len(track_of) else -1
        if ti == -1 or ti >= len(tracks):
            q, t = [0.0, 0.0, 0.0, 1.0], skel_t
        else:
            tr = tracks[ti]
            tsrc = tr.fields['_source']
            if tsrc is None:
                q = list(tr.fields['_constantQuaternion'])
                t = list(tr.fields['_constantTranslation']) if use_anim_trans(ti) else skel_t
            else:
                stream_bone = tsrc.fields['_trackId']
                q, t_anim = src.evaluate(time_ns, stream_bone)
                if q is None:
                    q = [0.0, 0.0, 0.0, 1.0]
                t = t_anim if (use_anim_trans(ti) and t_anim is not None) else skel_t
        locals_.append(quat_to_matrix(q, t))
        info.append((q, t, ti))
    worlds = [None] * n
    worlds[0] = locals_[0]
    for i in range(1, n):
        p = bones[i]['parent']
        if p == 0 or p < 0:
            worlds[i] = list(locals_[i])
        else:
            worlds[i] = mat_mul(locals_[i], worlds[p])
    blends = {}
    inv = skel.get('inverse_bind') or []
    k = 0
    for i in range(1, n):
        if bones[i]['bmIdx'] != -1 and k < len(inv):
            blends[bones[i]['bmIdx']] = mat_mul(inv[k], worlds[i])
            k += 1
    return locals_, worlds, blends, info


def write_obj(path, bones, worlds, frame, time_s):
    with open(path, 'w') as fo:
        fo.write('# posed skeleton frame %d t=%.6f s; vertices = bones (world), p = joints, l = parent->child\n' % (frame, time_s))
        for i, (b, m) in enumerate(zip(bones, worlds)):
            fo.write('v %.6f %.6f %.6f\n' % (m[12], m[13], m[14]))
        for i in range(len(bones)):
            fo.write('p %d\n' % (i + 1))
        for i, b in enumerate(bones):
            if b['parent'] >= 0:
                fo.write('l %d %d\n' % (b['parent'] + 1, i + 1))


def pick_bones(bones):
    picks = [0, 1, 2] if len(bones) > 2 else list(range(len(bones)))
    for key in ('Spine', 'Neck', 'Head', 'LeftHand', 'RightHand', 'LeftFoot', 'RightFoot', 'LeftToe', 'RightToe'):
        for i, b in enumerate(bones):
            if b['name'] == key and i not in picks:
                picks.append(i)
                break
    return picks[:12]


def main(argv):
    path = os.path.join(ROOT, 'work', 'assets', 'DATA', 'CHR', 'C00', 'C00A.IGB')
    which, step, out_dir, all_frames = 0, 10, os.path.join(ROOT, 'work', 'igb_preview', 'anim'), False
    args = list(argv[1:])
    while args:
        a = args.pop(0)
        if a == '--anim':
            which = args.pop(0)
        elif a == '--step':
            step = int(args.pop(0))
        elif a == '--out':
            out_dir = args.pop(0)
        elif a == '--all-frames':
            all_frames = True
        else:
            path = a
    f = IGBFile(path)
    anim, anims = load_animation(f, which)
    name = anim.fields.get('_name') or 'anim%d' % anims.index(anim)
    skel = skeleton_info(f)
    if skel is None:
        raise SystemExit('no igSkeleton in file')
    bones = skel['bones']
    binding = binding_for(anim, skel['skeleton_slot'])
    if binding is None:
        raise SystemExit('animation has no igAnimationBinding')
    src, _ = EnbayaSource.from_object(anim)
    hdr = src.header
    duration_ns = anim.fields['_duration']
    nframes = duration_ns // FRAME_NS + 1
    frames = list(range(0, nframes)) if all_frames else sorted(set(list(range(0, nframes, step)) + [nframes - 1]))
    os.makedirs(out_dir, exist_ok=True)
    base = os.path.basename(path).split('.')[0]
    prefix = os.path.join(out_dir, '%s_%s' % (base, name))
    picks = pick_bones(bones)
    nan_count = 0
    lo = [math.inf] * 3
    hi = [-math.inf] * 3
    root_motion = []
    with open(prefix + '.csv', 'w', newline='') as fc:
        wr = csv.writer(fc)
        wr.writerow(['frame', 'time_s', 'bone', 'name', 'track', 'lqx', 'lqy', 'lqz', 'lqw', 'ltx', 'lty', 'ltz', 'wx', 'wy', 'wz'])
        for fr in frames:
            time_ns = anim.fields['_startTime'] + fr * FRAME_NS
            t_s = src.local_time(time_ns)
            locals_, worlds, blends, info = pose_at(src, anim, binding, skel, time_ns)
            for m in worlds:
                for v in m:
                    if v != v or v in (math.inf, -math.inf):
                        nan_count += 1
                for k in range(3):
                    lo[k] = min(lo[k], m[12 + k])
                    hi[k] = max(hi[k], m[12 + k])
            hips = 2 if len(bones) > 2 else len(bones) - 1
            root_motion.append((fr, t_s, worlds[1][12:15] if len(bones) > 1 else worlds[0][12:15], worlds[hips][12:15]))
            write_obj('%s_f%04d.obj' % (prefix, fr), bones, worlds, fr, t_s)
            for i in picks:
                q, t, ti = info[i]
                wr.writerow([fr, '%.6f' % t_s, i, bones[i]['name'], ti] + ['%.6f' % v for v in q] + ['%.4f' % v for v in t] + ['%.4f' % v for v in worlds[i][12:15]])
    with open(prefix + '_summary.txt', 'w') as fs:
        fs.write('%s %s: duration %.6f s (%d ns), %d frames at 30 Hz, stream %d bones fps %d scale %g, interp %s play %d\n'
                 % (base, name, hdr.duration, duration_ns, nframes, hdr.bone_count, hdr.fps, hdr.scale, src.interpolation, src.play_mode))
        fs.write('sampled frames: %s\n' % frames)
        fs.write('non-finite values in world matrices: %d\n' % nan_count)
        fs.write('world joint position range: x %.3f..%.3f  y %.3f..%.3f  z %.3f..%.3f\n' % (lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]))
        fs.write('root motion (bone 1 world pos | bone 2 world pos):\n')
        for fr, t_s, p1, p2 in root_motion:
            fs.write('  f%04d t=%.4f  b1=(%.3f %.3f %.3f)  b2=(%.3f %.3f %.3f)\n' % ((fr, t_s) + tuple(p1) + tuple(p2)))
    print('wrote %d OBJ frames, %s.csv, %s_summary.txt; non-finite=%d; range x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f'
          % (len(frames), prefix, prefix, nan_count, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
