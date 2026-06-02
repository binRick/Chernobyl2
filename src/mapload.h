// mapload.h - SPIKE: load idTech3 .map brush geometry as a raylib Model.
//
// Xonotic ships its maps as Quake-style .map SOURCES (brush CSG, text). The
// compiled .bsp needs q3map2; but the brushes ARE the level geometry, so for a
// "can we see a Xonotic map in-engine?" spike we parse the brushes directly and
// build a mesh - no external tools, no binary assets.
//
// A brush is a convex solid = the intersection of its faces' half-spaces. Each
// .map face line begins with three points defining a plane, then a texture name;
// the brush interior is dot(n,x) <= d for every face. We recover each face's
// polygon by starting with a huge quad on that plane and clipping it by every
// OTHER face plane (Sutherland-Hodgman), then fan-triangulate. Works for both
// the old format and brushDef (both lead with the 3 points, then differ before
// the texture); patchDef2 curves are skipped (nested-paren rows don't match).
//
// Real Xonotic skins are shader-defined art spread across other repos, so this
// spike doesn't fetch images. Instead it reads the per-face texture NAME to (a)
// DROP invisible filler (caulk/clip/nodraw/hint/sky - the compiler strips these,
// they're never seen in-game) and (b) tint each surface by material so the map
// reads in colour. Still no Bezier patches, no real textures, no collision.
// Quake is Z-up/large-scale, so we swap Z<->Y, recenter and rescale.
//
// Assumes the includer already pulled in raylib.h, raymath.h, stdio/stdlib/
// string/math.
#ifndef MAPLOAD_H
#define MAPLOAD_H

typedef struct { Vector3 n; float d; } MapPlane;                 // interior: dot(n,x) <= d
typedef struct { MapPlane pl; unsigned char r,g,b; int draw; } MapFace;  // draw=0 -> filler (clip-only)

// Plane from 3 points, Quake winding: normal = (p0-p1) x (p2-p1), points out.
static MapPlane MapPlaneFromPts(Vector3 a, Vector3 b, Vector3 c){
    Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(a,b), Vector3Subtract(c,b)));
    return (MapPlane){ n, Vector3DotProduct(n,a) };
}

// First whitespace token starting with a letter = the texture name. Works for
// the old format (texture right after the 3rd ")") and brushDef (after the
// "( ( .. ) ( .. ) )" matrix) - every token before it starts with a digit/-/( .
static void MapTexName(const char *s, char *out, int outsz){
    out[0]=0;
    while (*s){
        while (*s==' '||*s=='\t') s++;
        if (!*s) return;
        if ((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')){
            int j=0; while (*s && *s!=' ' && *s!='\t' && *s!='\n' && *s!='\r' && j<outsz-1) out[j++]=*s++;
            out[j]=0; return;
        }
        while (*s && *s!=' ' && *s!='\t') s++;                   // skip a numeric/paren token
    }
}

// Compiler/tool surfaces that are invisible in-game - used as clip planes but
// never drawn.
static int MapIsFiller(const char *t){
    return strstr(t,"common/")||strstr(t,"caulk")||strstr(t,"nodraw")||strstr(t,"skies/")||
           strstr(t,"/sky")||strstr(t,"hint")||strstr(t,"areaportal")||strstr(t,"trigger")||
           strstr(t,"/clip")||strstr(t,"botclip")||strstr(t,"origin");
}

// Approximate a material colour from the texture name keyword, perturbed a touch
// by a name hash so distinct same-family textures don't read as one flat slab.
static Color MapMatColor(const char *t){
    static const struct { const char *k; unsigned char r,g,b; } M[]={
        {"liquids_lava",255,110,30},{"lava",255,110,30},{"water",60,125,180},{"liquid",70,120,165},
        {"light",255,238,200},{"glass",180,205,215},{"metal",150,160,175},{"trim",205,150,70},
        {"pipe",132,136,142},{"grate",108,110,116},{"panel",155,160,172},{"plate",150,156,166},
        {"floor",168,142,110},{"crete",172,162,150},{"wall",172,166,150},{"chain",120,122,128},
        {"link",120,122,128},{"wood",150,112,70},{"door",162,150,140},{"base",146,152,162},
    };
    for (unsigned i=0;i<sizeof(M)/sizeof(M[0]);i++) if (strstr(t,M[i].k)){
        unsigned h=2166136261u; for (const char*c=t;*c;c++){ h^=(unsigned char)*c; h*=16777619u; }
        int d=(int)(h%29)-14, r=M[i].r+d, g=M[i].g+d, b=M[i].b+d;
        if(r<0)r=0; if(r>255)r=255; if(g<0)g=0; if(g>255)g=255; if(b<0)b=0; if(b>255)b=255;
        return (Color){(unsigned char)r,(unsigned char)g,(unsigned char)b,255};
    }
    return (Color){160,160,166,255};
}

// Growable float (xyz) / byte (rgba) buffers for the accumulating triangle soup.
typedef struct { float *pos, *nrm; unsigned char *col; int n, cap; } MapBuf;
static void MapBufPush(MapBuf *b, Vector3 p, Vector3 nrm, unsigned char r,unsigned char g,unsigned char bl){
    if (b->n >= b->cap){
        b->cap = b->cap ? b->cap*2 : 4096;
        b->pos = (float*)realloc(b->pos, (size_t)b->cap*3*sizeof(float));
        b->nrm = (float*)realloc(b->nrm, (size_t)b->cap*3*sizeof(float));
        b->col = (unsigned char*)realloc(b->col, (size_t)b->cap*4);
    }
    b->pos[b->n*3+0]=p.x; b->pos[b->n*3+1]=p.y; b->pos[b->n*3+2]=p.z;
    b->nrm[b->n*3+0]=nrm.x; b->nrm[b->n*3+1]=nrm.y; b->nrm[b->n*3+2]=nrm.z;
    b->col[b->n*4+0]=r; b->col[b->n*4+1]=g; b->col[b->n*4+2]=bl; b->col[b->n*4+3]=255;
    b->n++;
}

// One brush -> triangles. fc[count] are its faces. All faces clip; only the
// drawable (non-filler) ones emit, coloured by material (Quake space).
#define MAP_MAXPOLY 64
static void MapEmitBrush(MapBuf *b, MapFace *fc, int count){
    const float S=131072.0f, EPS=0.02f;
    for (int i=0;i<count;i++){
        Vector3 n=fc[i].pl.n, org=Vector3Scale(n, fc[i].pl.d);
        Vector3 up = (fabsf(n.z)<0.9f) ? (Vector3){0,0,1} : (Vector3){1,0,0};
        Vector3 u=Vector3Normalize(Vector3CrossProduct(up,n));
        Vector3 v=Vector3CrossProduct(n,u);
        Vector3 poly[MAP_MAXPOLY]; int pn=4;
        poly[0]=Vector3Add(org,Vector3Add(Vector3Scale(u,-S),Vector3Scale(v,-S)));
        poly[1]=Vector3Add(org,Vector3Add(Vector3Scale(u, S),Vector3Scale(v,-S)));
        poly[2]=Vector3Add(org,Vector3Add(Vector3Scale(u, S),Vector3Scale(v, S)));
        poly[3]=Vector3Add(org,Vector3Add(Vector3Scale(u,-S),Vector3Scale(v, S)));
        for (int j=0;j<count && pn>=3;j++){
            if (j==i) continue;
            Vector3 out[MAP_MAXPOLY]; int on=0; MapPlane cp=fc[j].pl;
            for (int k=0;k<pn;k++){
                Vector3 A=poly[k], B=poly[(k+1)%pn];
                float da=Vector3DotProduct(cp.n,A)-cp.d, db=Vector3DotProduct(cp.n,B)-cp.d;
                int ina=(da<=EPS), inb=(db<=EPS);
                if (ina && on<MAP_MAXPOLY) out[on++]=A;
                if (ina!=inb && on<MAP_MAXPOLY){
                    float t=da/(da-db);
                    out[on++]=Vector3Add(A,Vector3Scale(Vector3Subtract(B,A),t));
                }
            }
            pn=on; for (int k=0;k<pn;k++) poly[k]=out[k];
        }
        if (!fc[i].draw || pn<3) continue;                       // filler clips but never draws
        for (int k=1;k+1<pn;k++){
            Vector3 e1=Vector3Subtract(poly[k],poly[0]), e2=Vector3Subtract(poly[k+1],poly[0]);
            if (Vector3Length(Vector3CrossProduct(e1,e2))<1.0f) continue;   // ~zero area
            MapBufPush(b,poly[0],  n, fc[i].r,fc[i].g,fc[i].b);
            MapBufPush(b,poly[k],  n, fc[i].r,fc[i].g,fc[i].b);
            MapBufPush(b,poly[k+1],n, fc[i].r,fc[i].g,fc[i].b);
        }
    }
}

// Parse a .map and build a raylib Model. *okOut=1 on success (>0 triangles).
static Model LoadQ3MapModel(const char *path, int *okOut){
    if (okOut) *okOut=0;
    Model empty={0};
    FILE *f=fopen(path,"r"); if(!f) return empty;
    MapBuf b={0};
    MapFace ctx[8][128]; int ctxN[8]={0}; int sp=0;              // brace-depth face stack
    char line[2048];
    while (fgets(line,sizeof(line),f)){
        const char *p=line; while (*p==' '||*p=='\t') p++;
        if (*p=='{'){ if (sp<8) ctxN[sp]=0; sp++; }
        else if (*p=='}'){ sp--; if (sp>=0 && sp<8 && ctxN[sp]>=4) MapEmitBrush(&b,ctx[sp],ctxN[sp]); }
        else if (*p=='('){
            Vector3 a,b3,c; int used=0;
            if (sscanf(p," ( %f %f %f ) ( %f %f %f ) ( %f %f %f )%n",
                       &a.x,&a.y,&a.z,&b3.x,&b3.y,&b3.z,&c.x,&c.y,&c.z,&used)>=9 && used>0){
                if (sp>0 && sp-1<8 && ctxN[sp-1]<128){
                    char tex[96]; MapTexName(p+used,tex,sizeof tex);
                    MapFace *fc=&ctx[sp-1][ctxN[sp-1]++];
                    fc->pl=MapPlaneFromPts(a,b3,c);
                    fc->draw=!MapIsFiller(tex);
                    Color mc=MapMatColor(tex); fc->r=mc.r; fc->g=mc.g; fc->b=mc.b;
                }
            }
        }
    }
    fclose(f);
    if (b.n<3){ free(b.pos); free(b.nrm); free(b.col); return empty; }

    // Quake-space bounds -> recenter (x,y), floor at 0, swap Z up to Y, rescale.
    float mnx=1e30f,mny=1e30f,mnz=1e30f,mxx=-1e30f,mxy=-1e30f,mxz=-1e30f;
    for (int i=0;i<b.n;i++){ float x=b.pos[i*3],y=b.pos[i*3+1],z=b.pos[i*3+2];
        if(x<mnx)mnx=x; if(y<mny)mny=y; if(z<mnz)mnz=z;
        if(x>mxx)mxx=x; if(y>mxy)mxy=y; if(z>mxz)mxz=z; }
    float cx=(mnx+mxx)*0.5f, cy=(mny+mxy)*0.5f;
    float ext=fmaxf(mxx-mnx,fmaxf(mxy-mny,mxz-mnz)); if(ext<1.0f)ext=1.0f;
    float s=60.0f/ext;                                           // ~60 raylib units across
    Vector3 L=Vector3Normalize((Vector3){0.5f,0.8f,0.35f});
    for (int i=0;i<b.n;i++){
        float qx=b.pos[i*3],qy=b.pos[i*3+1],qz=b.pos[i*3+2];
        b.pos[i*3+0]=(qx-cx)*s; b.pos[i*3+1]=(qz-mnz)*s; b.pos[i*3+2]=(qy-cy)*s;
        Vector3 nr=Vector3Normalize((Vector3){b.nrm[i*3],b.nrm[i*3+2],b.nrm[i*3+1]});
        b.nrm[i*3+0]=nr.x; b.nrm[i*3+1]=nr.y; b.nrm[i*3+2]=nr.z;
        float sh=0.40f+0.60f*fabsf(Vector3DotProduct(nr,L));     // shade material colour by normal
        b.col[i*4+0]=(unsigned char)(b.col[i*4+0]*sh);
        b.col[i*4+1]=(unsigned char)(b.col[i*4+1]*sh);
        b.col[i*4+2]=(unsigned char)(b.col[i*4+2]*sh);
    }

    Mesh m={0};
    m.vertexCount=b.n; m.triangleCount=b.n/3;
    m.vertices=b.pos; m.normals=b.nrm; m.colors=b.col;           // mesh takes ownership
    UploadMesh(&m,false);
    Model model=LoadModelFromMesh(m);
    if (okOut) *okOut=1;
    return model;
}
#endif // MAPLOAD_H
