import struct, json, sys
path = sys.argv[1] if len(sys.argv) > 1 else 'assets/robot.glb'
d = open(path, 'rb').read()
magic, ver, total = struct.unpack('<4sII', d[:12])
out = ["FILE=%s magic=%s ver=%d total=%d actual=%d complete=%s" % (
    path, magic.decode(errors='replace'), ver, total, len(d), total == len(d))]
# walk chunks
off = 12
json_chunk = None
while off + 8 <= len(d):
    clen, ctype = struct.unpack('<I4s', d[off:off+8])
    out.append("  chunk type=%r len=%d at=%d" % (ctype, clen, off))
    body = d[off+8:off+8+clen]
    if ctype == b'JSON':
        json_chunk = body
    off += 8 + clen
g = json.loads(json_chunk.decode('utf-8'))
acc = g.get('accessors', [])
anims = g.get('animations', [])
out.append("ANIMS=%d NODES=%d SKINS=%d MESHES=%d" % (
    len(anims), len(g.get('nodes', [])), len(g.get('skins', [])), len(g.get('meshes', []))))
for i, a in enumerate(anims):
    kf = max([acc[s['input']]['count'] for s in a.get('samplers', []) if 'input' in s] + [0])
    out.append("  [%d] name=%r channels=%d samplers=%d maxKeyframes=%d" % (
        i, a.get('name', '<none>'), len(a.get('channels', [])), len(a.get('samplers', [])), kf))
open('/tmp/glb_report.txt', 'w').write("\n".join(out) + "\n")
