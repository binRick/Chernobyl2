#!/usr/bin/env python3
# Fix GLBs whose skin binding stance differs from the node-default stance.
#
# glTF skinning poses vertices via jointWorld * inverseBindMatrix; the node
# hierarchy's default TRS may be a completely different stance than the one the
# mesh was bound in. raylib ignores the IBMs and assumes binding == node stance,
# so such models render their skinned meshes in the (often T-pose-ish) binding
# stance forever -- e.g. FPS arms "stretched forward" instead of gripping.
#
# Fix: re-skin every skinned vertex by nodeWorld*IBM (binding -> node stance),
# then rewrite the IBMs to inv(nodeWorld) so the file matches raylib's assumption.
# In-place binary patch; counts/offsets unchanged. Usage: fix_gltf_bindpose.py file.glb
import json,struct,math,sys,shutil

fn=sys.argv[1]
shutil.copyfile(fn, fn+".bak")
d=bytearray(open(fn,'rb').read());off=12;js=None;binoff=None
while off<len(d):
    cl,ct=struct.unpack_from('<II',d,off);off+=8
    if ct==0x4E4F534A: js=json.loads(d[off:off+cl].decode())
    elif ct==0x004E4942: binoff=off
    off+=cl
nodes=js['nodes']; acc=js['accessors']; bvs=js['bufferViews']
def aoff(ai):
    a=acc[ai]; bv=bvs[a['bufferView']]
    return binoff+bv.get('byteOffset',0)+a.get('byteOffset',0), a['count'], a['componentType']
def local_mat(n):
    if 'matrix' in n:
        m=n['matrix']; return [[m[0],m[4],m[8],m[12]],[m[1],m[5],m[9],m[13]],[m[2],m[6],m[10],m[14]],[m[3],m[7],m[11],m[15]]]
    T=n.get('translation',[0,0,0]); Q=n.get('rotation',[0,0,0,1]); S=n.get('scale',[1,1,1])
    x,y,z,w=Q
    R=[[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]]
    return [[R[r][c]*S[c] for c in range(3)]+[T[r]] for r in range(3)]+[[0,0,0,1]]
def matmul(A,B): return [[sum(A[r][k]*B[k][c] for k in range(4)) for c in range(4)] for r in range(4)]
def inv(M):
    A=[row[:]+[1.0 if i==j else 0.0 for j in range(4)] for i,row in enumerate(M)]
    for col in range(4):
        p=max(range(col,4),key=lambda r:abs(A[r][col])); A[col],A[p]=A[p],A[col]
        dv=A[col][col]; A[col]=[x/dv for x in A[col]]
        for r in range(4):
            if r!=col and abs(A[r][col])>1e-12:
                f=A[r][col]; A[r]=[x-f*y for x,y in zip(A[r],A[col])]
    return [row[4:] for row in A]
parent={}
for ni,n in enumerate(nodes):
    for c in n.get('children',[]): parent[c]=ni
def world(ni):
    M=local_mat(nodes[ni]); p=parent.get(ni)
    while p is not None: M=matmul(local_mat(nodes[p]),M); p=parent.get(p)
    return M
total_moved=0.0; nverts=0
for skin in js.get('skins',[]):
    joints=skin['joints']
    ibmoff,ibmcnt,_=aoff(skin['inverseBindMatrices'])
    Mj=[]; Wn=[]
    mismatch=0.0
    for j in range(len(joints)):
        f=struct.unpack_from('<16f', d, ibmoff+j*64)
        IBM=[[f[0],f[4],f[8],f[12]],[f[1],f[5],f[9],f[13]],[f[2],f[6],f[10],f[14]],[f[3],f[7],f[11],f[15]]]
        W=world(joints[j]); Wn.append(W); Mj.append(matmul(W,IBM))
        Wi=inv(IBM)
        mismatch=max(mismatch, math.sqrt(sum((Wi[r][3]-W[r][3])**2 for r in range(3))))
    print(f"skin: {len(joints)} joints, max binding-vs-node mismatch {mismatch:.2f}")
    if mismatch<0.01:
        print("  already consistent; skipping"); continue
    def xf(M,p): return (M[0][0]*p[0]+M[0][1]*p[1]+M[0][2]*p[2]+M[0][3],
                         M[1][0]*p[0]+M[1][1]*p[1]+M[1][2]*p[2]+M[1][3],
                         M[2][0]*p[0]+M[2][1]*p[1]+M[2][2]*p[2]+M[2][3])
    def xfn(M,p): return (M[0][0]*p[0]+M[0][1]*p[1]+M[0][2]*p[2],
                          M[1][0]*p[0]+M[1][1]*p[1]+M[1][2]*p[2],
                          M[2][0]*p[0]+M[2][1]*p[1]+M[2][2]*p[2])
    jsz={5121:('B',1),5123:('H',2),5125:('I',4)}
    for mesh in js['meshes']:
        for prim in mesh['primitives']:
            at=prim['attributes']
            if 'JOINTS_0' not in at or 'WEIGHTS_0' not in at: continue
            poff,n,_=aoff(at['POSITION'])
            noff = aoff(at['NORMAL'])[0] if 'NORMAL' in at else None
            joff,_,jct=aoff(at['JOINTS_0']); jfmt,jb=jsz[jct]
            woff,_,wct=aoff(at['WEIGHTS_0'])
            for v in range(n):
                jp=struct.unpack_from('<4'+jfmt, d, joff+v*4*jb)
                if wct==5126: wp=struct.unpack_from('<4f', d, woff+v*16)
                elif wct==5121: wp=[x/255.0 for x in struct.unpack_from('<4B', d, woff+v*4)]
                else: wp=[x/65535.0 for x in struct.unpack_from('<4H', d, woff+v*8)]
                sw=sum(wp)
                if sw<1e-6: continue
                p=struct.unpack_from('<3f', d, poff+v*12)
                np_=[0.0,0.0,0.0]
                for c in range(4):
                    if wp[c]<=0: continue
                    q=xf(Mj[jp[c]],p)
                    for i in range(3): np_[i]+=q[i]*wp[c]/sw
                total_moved+=math.sqrt(sum((a-b)**2 for a,b in zip(np_,p))); nverts+=1
                struct.pack_into('<3f', d, poff+v*12, *np_)
                if noff is not None:
                    nv=struct.unpack_from('<3f', d, noff+v*12)
                    nn=[0.0,0.0,0.0]
                    for c in range(4):
                        if wp[c]<=0: continue
                        q=xfn(Mj[jp[c]],nv)
                        for i in range(3): nn[i]+=q[i]*wp[c]/sw
                    L=math.sqrt(sum(x*x for x in nn)) or 1.0
                    struct.pack_into('<3f', d, noff+v*12, nn[0]/L, nn[1]/L, nn[2]/L)
    for j in range(len(joints)):
        Wi=inv(Wn[j])
        cm=[Wi[0][0],Wi[1][0],Wi[2][0],Wi[3][0], Wi[0][1],Wi[1][1],Wi[2][1],Wi[3][1],
            Wi[0][2],Wi[1][2],Wi[2][2],Wi[3][2], Wi[0][3],Wi[1][3],Wi[2][3],Wi[3][3]]
        struct.pack_into('<16f', d, ibmoff+j*64, *cm)
open(fn,'wb').write(d)
print(f"re-skinned {nverts} verts (mean move {total_moved/max(nverts,1):.2f}); backup at {fn}.bak")
