#!/usr/bin/env python3
"""Extract glTF inverse-bind matrices into a sidecar raylib can't give us.

raylib builds a skinned model's bind pose from the joints' REST node transforms
and ignores the skin's inverseBindMatrices. For rigs exported with the bind pose
!= rest pose (most of the Sketchfab FPS arm rigs here: AK, MP5, LMG, minigun,
flamethrower, knife) that skins every vertex against the wrong reference -- hands
float off the gun, whole arms vanish. The game loads this sidecar and replaces
model.skeleton.bindPose with the true bind pose (inverse of each IBM).

Output: <model>.glb.ibm, plain text:
    ibm 1
    meshworld m0..m15          (world matrix of the first skinned mesh node, column-major)
    joints N
    <name> m0..m15             (one per skin joint, skin order = raylib bone order; column-major IBM)

Usage: tools/gen_ibm.py assets/ak74.glb [more.glb ...]
"""
import json
import struct
import sys


def load(path):
    b = open(path, "rb").read()
    jlen = struct.unpack("<I", b[12:16])[0]
    j = json.loads(b[20:20 + jlen])
    return b, j, 20 + jlen + 8


def accessor_mat4(b, j, base, idx):
    a = j["accessors"][idx]
    v = j["bufferViews"][a["bufferView"]]
    off = base + v.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = v.get("byteStride", 64)
    return [struct.unpack_from("<16f", b, off + k * stride) for k in range(a["count"])]


def trs(n):
    t = n.get("translation", [0, 0, 0]); r = n.get("rotation", [0, 0, 0, 1]); s = n.get("scale", [1, 1, 1])
    x, y, z, w = r
    R = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    if "matrix" in n:
        m = n["matrix"]; return [[m[c * 4 + r_] for c in range(4)] for r_ in range(4)]
    return [[R[i][0] * s[0], R[i][1] * s[1], R[i][2] * s[2], t[i]] for i in range(3)] + [[0, 0, 0, 1]]


def mul(A, B):
    return [[sum(A[i][k] * B[k][q] for k in range(4)) for q in range(4)] for i in range(4)]


def main():
    for path in sys.argv[1:]:
        b, j, base = load(path)
        if not j.get("skins"):
            print(f"{path}: no skin, skipped"); continue
        nodes = j["nodes"]; par = {}
        for i, n in enumerate(nodes):
            for c in n.get("children", []):
                par[c] = i
        mesh_node = next(i for i, n in enumerate(nodes) if "mesh" in n and "skin" in n)
        M = trs(nodes[mesh_node]); p = par.get(mesh_node)
        while p is not None:
            M = mul(trs(nodes[p]), M); p = par.get(p)
        skin = j["skins"][nodes[mesh_node]["skin"]]
        ibm = accessor_mat4(b, j, base, skin["inverseBindMatrices"])
        out = path + ".ibm"
        with open(out, "w") as o:
            o.write("ibm 1\n")
            o.write("meshworld " + " ".join("%.9g" % M[r][c] for c in range(4) for r in range(4)) + "\n")
            o.write(f"joints {len(skin['joints'])}\n")
            for k, jn in enumerate(skin["joints"]):
                name = nodes[jn].get("name", f"joint{k}").replace(" ", "_")
                o.write(name + " " + " ".join("%.9g" % x for x in ibm[k]) + "\n")
        print(f"{out}: {len(skin['joints'])} joints")


if __name__ == "__main__":
    main()
