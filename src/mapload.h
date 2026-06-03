// mapload.h - SPIKE: load idTech3 .map brush geometry as a TEXTURED raylib Model.
//
// Xonotic ships its maps as Quake-style .map SOURCES (brush CSG, text). The
// brushes ARE the level geometry, so we parse them directly - no q3map2.
//
// A brush is a convex solid = the intersection of its faces' half-spaces. Each
// face line is "( p0 )( p1 )( p2 ) texture shiftX shiftY rot scaleX scaleY ...".
// We recover each face polygon by clipping a huge quad on its plane by every
// OTHER face plane, then fan-triangulate. UVs use Quake's texture-axis
// projection (axial basis from the dominant normal, then rotate/scale/shift).
//
// Textures: tools/fetch_map_textures.sh pre-downloads each face's diffuse image
// to maps/textures/<name>.{tga,png}; we load those and build one mesh+material
// per texture. Invisible compiler filler (caulk/clip/nodraw/sky/hint) is used
// as clip planes but never drawn. A missing texture (e.g. animated lava) falls
// back to a material colour. No lightmaps (those live in the .bsp), so we bake
// a cheap directional shade into vertex colour for depth. No collision.
// Quake is Z-up/large-scale, so we swap Z<->Y, recenter and rescale.
//
// Assumes the includer pulled in raylib.h, raymath.h, stdio/stdlib/string/math.
#ifndef MAPLOAD_H
#define MAPLOAD_H

#define MAP_TEXDIR "maps/textures/"     // where fetch_map_textures.sh writes
#define MAP_MAXPOLY 64

typedef struct { Vector3 n; float d; } MapPlane;   // interior: dot(n,x) <= d
typedef struct {                                   // one brush face
    MapPlane pl;
    Vector3  stx, sty;                             // UV axes (pre-divided by scale)
    float    offx, offy;                           // UV shift
    int      tex;                                  // index into the texture registry (-1=skip)
    unsigned char r,g,b;                           // fallback colour if texture missing
} MapFace;

// Per-texture triangle soup + its loaded image.
typedef struct { float *pos,*nrm,*uv; unsigned char *col; int n,cap; } MapBuf;
typedef struct { char name[96]; Texture2D tex; int hasTex,w,h; MapBuf buf; } MapTex;
typedef struct { MapTex *t; int n,cap; } MapReg;

// Quake texture-axis basis: 6 face orientations, each {projAxis, uAxis, vAxis}.
static const float mapBaseAxis[18][3] = {
    {0,0,1},{1,0,0},{0,-1,0},  {0,0,-1},{1,0,0},{0,-1,0},   // floor, ceiling
    {1,0,0},{0,1,0},{0,0,-1},  {-1,0,0},{0,1,0},{0,0,-1},   // west,  east
    {0,1,0},{1,0,0},{0,0,-1},  {0,-1,0},{1,0,0},{0,0,-1},   // south, north
};

static MapPlane MapPlaneFromPts(Vector3 a, Vector3 b, Vector3 c){
    Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(a,b), Vector3Subtract(c,b)));
    return (MapPlane){ n, Vector3DotProduct(n,a) };
}

// First whitespace token starting with a letter = texture name; returns the
// char just past it (where the shift/rot/scale numbers begin).
static const char *MapTexName(const char *s, char *out, int outsz){
    out[0]=0;
    while (*s){
        while (*s==' '||*s=='\t') s++;
        if (!*s) return s;
        if ((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')){
            int j=0; while (*s && *s!=' ' && *s!='\t' && *s!='\n' && *s!='\r' && j<outsz-1) out[j++]=*s++;
            out[j]=0; return s;
        }
        while (*s && *s!=' ' && *s!='\t') s++;
    }
    return s;
}

static int MapIsFiller(const char *t){
    return strstr(t,"common/")||strstr(t,"caulk")||strstr(t,"nodraw")||strstr(t,"skies/")||
           strstr(t,"/sky")||strstr(t,"hint")||strstr(t,"areaportal")||strstr(t,"trigger")||
           strstr(t,"/clip")||strstr(t,"botclip")||strstr(t,"/origin")||strstr(t,"caulk");
}

// Fallback colour (used only when a texture image is missing) from a keyword.
static Color MapMatColor(const char *t){
    static const struct { const char *k; unsigned char r,g,b; } M[]={
        {"lava",255,110,30},{"water",60,125,180},{"light",255,238,200},{"glass",170,200,210},
        {"metal",150,160,175},{"trim",205,150,70},{"floor",168,142,110},{"wall",172,166,150},
    };
    for (unsigned i=0;i<sizeof(M)/sizeof(M[0]);i++) if (strstr(t,M[i].k)) return (Color){M[i].r,M[i].g,M[i].b,255};
    return (Color){165,165,170,255};
}

// Quake UV axes for a face: pick axial basis from the normal, then rotate (deg)
// in-plane and divide by scale. UV = (dot(vert,stx)+offx)/w , (dot(vert,sty)+offy)/h.
static void MapUVBasis(Vector3 n, float rot, float scx, float scy, Vector3 *stx, Vector3 *sty){
    int best=0; float bd=-1e9f;
    for (int i=0;i<6;i++){ float d=n.x*mapBaseAxis[i*3][0]+n.y*mapBaseAxis[i*3][1]+n.z*mapBaseAxis[i*3][2];
        if (d>bd){ bd=d; best=i; } }
    float ua[3]={mapBaseAxis[best*3+1][0],mapBaseAxis[best*3+1][1],mapBaseAxis[best*3+1][2]};
    float va[3]={mapBaseAxis[best*3+2][0],mapBaseAxis[best*3+2][1],mapBaseAxis[best*3+2][2]};
    if (scx==0) scx=1; if (scy==0) scy=1;
    float ang=rot*0.01745329252f, s=sinf(ang), cc=cosf(ang);
    int sv = ua[0]?0:(ua[1]?1:2), tv = va[0]?0:(va[1]?1:2);
    float a1=cc*ua[sv]-s*ua[tv], a2=s*ua[sv]+cc*ua[tv]; ua[sv]=a1; ua[tv]=a2;
    float b1=cc*va[sv]-s*va[tv], b2=s*va[sv]+cc*va[tv]; va[sv]=b1; va[tv]=b2;
    *stx=(Vector3){ua[0]/scx,ua[1]/scx,ua[2]/scx};
    *sty=(Vector3){va[0]/scy,va[1]/scy,va[2]/scy};
}

// Find or add a texture by name; loads the image on first sight.
static int MapRegGet(MapReg *R, const char *name){
    for (int i=0;i<R->n;i++) if (!strcmp(R->t[i].name,name)) return i;
    if (R->n>=R->cap){ R->cap=R->cap?R->cap*2:64; R->t=(MapTex*)realloc(R->t,(size_t)R->cap*sizeof(MapTex)); }
    MapTex *mt=&R->t[R->n]; memset(mt,0,sizeof(*mt));
    snprintf(mt->name,sizeof mt->name,"%s",name);
    char path[256]; const char *ext[2]={".png",".tga"};   // raylib build decodes PNG, not TGA
    for (int e=0;e<2 && !mt->hasTex;e++){
        snprintf(path,sizeof path,"%s%s%s",MAP_TEXDIR,name,ext[e]);
        if (FileExists(path)){
            mt->tex=LoadTexture(path);
            if (mt->tex.id>0){ mt->hasTex=1; mt->w=mt->tex.width; mt->h=mt->tex.height;
                GenTextureMipmaps(&mt->tex);
                SetTextureFilter(mt->tex,TEXTURE_FILTER_TRILINEAR);
                SetTextureWrap(mt->tex,TEXTURE_WRAP_REPEAT); }
        }
    }
    if (!mt->hasTex){ mt->w=mt->h=64; }              // sane divisor for the fallback path
    return R->n++;
}

static void MapBufPush(MapBuf *b, Vector3 p, Vector3 nrm, float u, float v, unsigned char r,unsigned char g,unsigned char bl){
    if (b->n>=b->cap){ b->cap=b->cap?b->cap*2:256;
        b->pos=(float*)realloc(b->pos,(size_t)b->cap*3*sizeof(float));
        b->nrm=(float*)realloc(b->nrm,(size_t)b->cap*3*sizeof(float));
        b->uv =(float*)realloc(b->uv ,(size_t)b->cap*2*sizeof(float));
        b->col=(unsigned char*)realloc(b->col,(size_t)b->cap*4); }
    b->pos[b->n*3]=p.x; b->pos[b->n*3+1]=p.y; b->pos[b->n*3+2]=p.z;
    b->nrm[b->n*3]=nrm.x; b->nrm[b->n*3+1]=nrm.y; b->nrm[b->n*3+2]=nrm.z;
    b->uv[b->n*2]=u; b->uv[b->n*2+1]=v;
    b->col[b->n*4]=r; b->col[b->n*4+1]=g; b->col[b->n*4+2]=bl; b->col[b->n*4+3]=255;
    b->n++;
}

// One brush -> triangles. All faces clip; drawable ones emit into their texture
// buffer with computed UVs (Quake space).
static void MapEmitBrush(MapReg *R, MapFace *fc, int count){
    const float S=131072.0f, EPS=0.02f;
    for (int i=0;i<count;i++){
        Vector3 n=fc[i].pl.n, org=Vector3Scale(n, fc[i].pl.d);
        Vector3 up=(fabsf(n.z)<0.9f)?(Vector3){0,0,1}:(Vector3){1,0,0};
        Vector3 u=Vector3Normalize(Vector3CrossProduct(up,n)), v=Vector3CrossProduct(n,u);
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
                if (ina!=inb && on<MAP_MAXPOLY){ float t=da/(da-db); out[on++]=Vector3Add(A,Vector3Scale(Vector3Subtract(B,A),t)); }
            }
            pn=on; for (int k=0;k<pn;k++) poly[k]=out[k];
        }
        if (fc[i].tex<0 || pn<3) continue;
        MapTex *mt=&R->t[fc[i].tex];
        unsigned char cr=255,cg=255,cb=255;
        if (!mt->hasTex){ cr=fc[i].r; cg=fc[i].g; cb=fc[i].b; }   // no image -> material colour
        for (int k=1;k+1<pn;k++){
            Vector3 e1=Vector3Subtract(poly[k],poly[0]), e2=Vector3Subtract(poly[k+1],poly[0]);
            if (Vector3Length(Vector3CrossProduct(e1,e2))<1.0f) continue;
            int idx[3]={0,k,k+1};
            for (int q=0;q<3;q++){
                Vector3 P=poly[idx[q]];
                float uu=(Vector3DotProduct(P,fc[i].stx)+fc[i].offx)/(float)mt->w;
                float vv=(Vector3DotProduct(P,fc[i].sty)+fc[i].offy)/(float)mt->h;
                MapBufPush(&mt->buf,P,n,uu,vv,cr,cg,cb);
            }
        }
    }
}

// ---- Baked ambient occlusion -----------------------------------------------
// Per-vertex AO: cast a hemisphere of rays around the normal; the fraction that
// hit nearby geometry (within AO_RADIUS) darkens the vertex. A uniform grid +
// DDA traversal keeps it to ~tens of triangle tests per ray. Bakes at load.
#define AO_RAYS     14
#define AO_RADIUS   5.0f     // reach, in raylib units (map is normalised to ~60 across)
#define AO_BIAS     0.08f    // lift the ray origin off the surface
#define AO_STRENGTH 0.82f    // how dark a fully-occluded vertex gets

typedef struct { Vector3 mn; float cs; int nx,ny,nz; int *start,*items; } AOGrid;

// Persistent triangle soup + grid (raylib space). Built at load; AO reads it,
// and runtime collision (floor rays / wall push-out) reuses the same grid.
typedef struct { float *tri; int ntri; AOGrid grid; int ready; } MapCol;
static MapCol g_mapCol = {0};
static float  g_mapAOsec = 0.0f;   // AO bake time (for the load log)
static Vector3 g_mapSpawn = {0,0,0};   // a player spawn point (raylib space)
static int     g_hasSpawn = 0;
typedef struct { Vector3 pos; float R; Vector3 color; } MapLight;   // point light (raylib space, R=reach)
static MapLight g_mapLights[2048];
static int      g_nMapLights = 0;

static float MapRayTri(Vector3 o, Vector3 d, Vector3 a, Vector3 b, Vector3 c){
    Vector3 e1=Vector3Subtract(b,a), e2=Vector3Subtract(c,a);
    Vector3 p=Vector3CrossProduct(d,e2); float det=Vector3DotProduct(e1,p);
    if (fabsf(det)<1e-7f) return -1.0f;
    float inv=1.0f/det; Vector3 t=Vector3Subtract(o,a);
    float u=Vector3DotProduct(t,p)*inv; if (u<0||u>1) return -1.0f;
    Vector3 q=Vector3CrossProduct(t,e1); float v=Vector3DotProduct(d,q)*inv; if (v<0||u+v>1) return -1.0f;
    return Vector3DotProduct(e2,q)*inv;
}

// DDA-march the grid along ray (o,d) up to R; 1 if any triangle is hit.
static int MapRayOccluded(AOGrid *g, const float *tri, Vector3 o, Vector3 d, float R, int *stamp, int rid){
    float ox=o.x-g->mn.x, oy=o.y-g->mn.y, oz=o.z-g->mn.z;
    int cx=(int)floorf(ox/g->cs), cy=(int)floorf(oy/g->cs), cz=(int)floorf(oz/g->cs);
    if (cx<0)cx=0; if(cy<0)cy=0; if(cz<0)cz=0;
    if (cx>=g->nx)cx=g->nx-1; if(cy>=g->ny)cy=g->ny-1; if(cz>=g->nz)cz=g->nz-1;
    int sx=d.x>0?1:-1, sy=d.y>0?1:-1, sz=d.z>0?1:-1;
    float tMaxX,tMaxY,tMaxZ,tDX,tDY,tDZ;
    if (fabsf(d.x)<1e-9f){ tMaxX=1e30f; tDX=1e30f; } else { float nb=(sx>0?cx+1:cx)*g->cs; tMaxX=(nb-ox)/d.x; tDX=g->cs/fabsf(d.x); }
    if (fabsf(d.y)<1e-9f){ tMaxY=1e30f; tDY=1e30f; } else { float nb=(sy>0?cy+1:cy)*g->cs; tMaxY=(nb-oy)/d.y; tDY=g->cs/fabsf(d.y); }
    if (fabsf(d.z)<1e-9f){ tMaxZ=1e30f; tDZ=1e30f; } else { float nb=(sz>0?cz+1:cz)*g->cs; tMaxZ=(nb-oz)/d.z; tDZ=g->cs/fabsf(d.z); }
    float t=0.0f;
    while (t<=R){
        int cell=(cz*g->ny+cy)*g->nx+cx;
        for (int e=g->start[cell]; e<g->start[cell+1]; e++){
            int ti=g->items[e]; if (stamp[ti]==rid) continue; stamp[ti]=rid;
            Vector3 A={tri[ti*9],tri[ti*9+1],tri[ti*9+2]}, B={tri[ti*9+3],tri[ti*9+4],tri[ti*9+5]}, C={tri[ti*9+6],tri[ti*9+7],tri[ti*9+8]};
            float h=MapRayTri(o,d,A,B,C); if (h>AO_BIAS && h<R) return 1;
        }
        if (tMaxX<tMaxY && tMaxX<tMaxZ){ cx+=sx; t=tMaxX; tMaxX+=tDX; if(cx<0||cx>=g->nx)break; }
        else if (tMaxY<tMaxZ){ cy+=sy; t=tMaxY; tMaxY+=tDY; if(cy<0||cy>=g->ny)break; }
        else { cz+=sz; t=tMaxZ; tMaxZ+=tDZ; if(cz<0||cz>=g->nz)break; }
    }
    return 0;
}

static float MapVertexAO(AOGrid *g, const float *tri, Vector3 p, Vector3 n, const Vector3 *samp, int *stamp, int *rid){
    Vector3 up=(fabsf(n.y)<0.99f)?(Vector3){0,1,0}:(Vector3){1,0,0};
    Vector3 T=Vector3Normalize(Vector3CrossProduct(up,n)), B=Vector3CrossProduct(n,T);
    Vector3 o=Vector3Add(p,Vector3Scale(n,AO_BIAS));
    int occ=0;
    for (int s=0;s<AO_RAYS;s++){
        Vector3 sd=samp[s];
        Vector3 d={ T.x*sd.x+B.x*sd.y+n.x*sd.z, T.y*sd.x+B.y*sd.y+n.y*sd.z, T.z*sd.x+B.z*sd.y+n.z*sd.z };
        (*rid)++; if (MapRayOccluded(g,tri,o,d,AO_RADIUS,stamp,*rid)) occ++;
    }
    return (float)occ/(float)AO_RAYS;
}

// Baked direct light at a vertex: each in-range light contributes Lambert *
// distance-falloff, gated by a shadow ray through the grid (so it casts hard
// shadows). White lights give a grey result; coloured lights tint.
static Vector3 MapVertexLight(Vector3 P, Vector3 N, int *stamp, int *rid){
    Vector3 sum={0,0,0};
    for (int l=0;l<g_nMapLights;l++){
        MapLight *L=&g_mapLights[l];
        Vector3 dl=Vector3Subtract(L->pos,P); float R=L->R, d2=Vector3DotProduct(dl,dl);
        if (d2>=R*R) continue;
        float d=sqrtf(d2); if (d<1e-4f) d=1e-4f;
        Vector3 Ldir=Vector3Scale(dl,1.0f/d);
        float ndl=Vector3DotProduct(N,Ldir); if (ndl<=0.0f) continue;     // facing away
        Vector3 o=Vector3Add(P,Vector3Scale(N,AO_BIAS));
        (*rid)++;
        if (MapRayOccluded(&g_mapCol.grid,g_mapCol.tri,o,Ldir,d-2.0f*AO_BIAS,stamp,*rid)) continue;  // shadowed
        float c=ndl*(1.0f-d/R)*1.05f;
        sum.x+=L->color.x*c; sum.y+=L->color.y*c; sum.z+=L->color.z*c;
    }
    return sum;
}

static int MapCellIdx(AOGrid *g,int x,int y,int z){ return (z*g->ny+y)*g->nx+x; }

// Build the persistent triangle grid (counting-sort buckets; a triangle is
// registered in every cell its bbox overlaps).
static void MapColBuild(float *tri, int ntri){
    AOGrid *g=&g_mapCol.grid; g_mapCol.tri=tri; g_mapCol.ntri=ntri;
    Vector3 mn={1e30f,1e30f,1e30f}, mx={-1e30f,-1e30f,-1e30f};
    for (int v=0;v<ntri*3;v++){ float x=tri[v*3],y=tri[v*3+1],z=tri[v*3+2];
        if(x<mn.x)mn.x=x; if(y<mn.y)mn.y=y; if(z<mn.z)mn.z=z;
        if(x>mx.x)mx.x=x; if(y>mx.y)mx.y=y; if(z>mx.z)mx.z=z; }
    g->mn=mn; g->cs=AO_RADIUS*0.5f;
    for (;;){ g->nx=(int)((mx.x-mn.x)/g->cs)+1; g->ny=(int)((mx.y-mn.y)/g->cs)+1; g->nz=(int)((mx.z-mn.z)/g->cs)+1;
        if(g->nx<1)g->nx=1; if(g->ny<1)g->ny=1; if(g->nz<1)g->nz=1;
        if ((long)g->nx*g->ny*g->nz<=4000000L) break; g->cs*=1.5f; }
    int NC=g->nx*g->ny*g->nz;
    g->start=(int*)calloc(NC+1,sizeof(int));
    for (int t=0;t<ntri;t++){
        Vector3 a={tri[t*9],tri[t*9+1],tri[t*9+2]},b={tri[t*9+3],tri[t*9+4],tri[t*9+5]},c={tri[t*9+6],tri[t*9+7],tri[t*9+8]};
        int x0=(int)((fminf(a.x,fminf(b.x,c.x))-mn.x)/g->cs), x1=(int)((fmaxf(a.x,fmaxf(b.x,c.x))-mn.x)/g->cs);
        int y0=(int)((fminf(a.y,fminf(b.y,c.y))-mn.y)/g->cs), y1=(int)((fmaxf(a.y,fmaxf(b.y,c.y))-mn.y)/g->cs);
        int z0=(int)((fminf(a.z,fminf(b.z,c.z))-mn.z)/g->cs), z1=(int)((fmaxf(a.z,fmaxf(b.z,c.z))-mn.z)/g->cs);
        for (int z=z0;z<=z1;z++)for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++) g->start[MapCellIdx(g,x,y,z)+1]++;
    }
    for (int i=0;i<NC;i++) g->start[i+1]+=g->start[i];
    g->items=(int*)malloc((size_t)g->start[NC]*sizeof(int));
    int *cur=(int*)malloc((size_t)NC*sizeof(int)); for(int i=0;i<NC;i++)cur[i]=g->start[i];
    for (int t=0;t<ntri;t++){
        Vector3 a={tri[t*9],tri[t*9+1],tri[t*9+2]},b={tri[t*9+3],tri[t*9+4],tri[t*9+5]},c={tri[t*9+6],tri[t*9+7],tri[t*9+8]};
        int x0=(int)((fminf(a.x,fminf(b.x,c.x))-mn.x)/g->cs), x1=(int)((fmaxf(a.x,fmaxf(b.x,c.x))-mn.x)/g->cs);
        int y0=(int)((fminf(a.y,fminf(b.y,c.y))-mn.y)/g->cs), y1=(int)((fmaxf(a.y,fmaxf(b.y,c.y))-mn.y)/g->cs);
        int z0=(int)((fminf(a.z,fminf(b.z,c.z))-mn.z)/g->cs), z1=(int)((fmaxf(a.z,fmaxf(b.z,c.z))-mn.z)/g->cs);
        for (int z=z0;z<=z1;z++)for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++) g->items[cur[MapCellIdx(g,x,y,z)]++]=t;
    }
    free(cur); g_mapCol.ready=1;
}

// Nearest triangle hit along ray (o,d) within R; returns distance or -1.
static float MapRayNearest(Vector3 o, Vector3 d, float R){
    if (!g_mapCol.ready) return -1.0f; AOGrid *g=&g_mapCol.grid; const float *tri=g_mapCol.tri;
    static int *st=NULL; static int cap=0, rid=0; if (cap<g_mapCol.ntri){ free(st); cap=g_mapCol.ntri; st=(int*)calloc(cap,sizeof(int)); }
    rid++;
    float ox=o.x-g->mn.x, oy=o.y-g->mn.y, oz=o.z-g->mn.z;
    int cx=(int)floorf(ox/g->cs), cy=(int)floorf(oy/g->cs), cz=(int)floorf(oz/g->cs);
    if(cx<0)cx=0; if(cy<0)cy=0; if(cz<0)cz=0; if(cx>=g->nx)cx=g->nx-1; if(cy>=g->ny)cy=g->ny-1; if(cz>=g->nz)cz=g->nz-1;
    int sx=d.x>0?1:-1,sy=d.y>0?1:-1,sz=d.z>0?1:-1; float tMaxX,tMaxY,tMaxZ,tDX,tDY,tDZ;
    if(fabsf(d.x)<1e-9f){tMaxX=1e30f;tDX=1e30f;}else{float nb=(sx>0?cx+1:cx)*g->cs;tMaxX=(nb-ox)/d.x;tDX=g->cs/fabsf(d.x);}
    if(fabsf(d.y)<1e-9f){tMaxY=1e30f;tDY=1e30f;}else{float nb=(sy>0?cy+1:cy)*g->cs;tMaxY=(nb-oy)/d.y;tDY=g->cs/fabsf(d.y);}
    if(fabsf(d.z)<1e-9f){tMaxZ=1e30f;tDZ=1e30f;}else{float nb=(sz>0?cz+1:cz)*g->cs;tMaxZ=(nb-oz)/d.z;tDZ=g->cs/fabsf(d.z);}
    float t=0.0f, best=-1.0f;
    while (t<=R){
        int cell=MapCellIdx(g,cx,cy,cz);
        for (int e=g->start[cell]; e<g->start[cell+1]; e++){ int ti=g->items[e]; if(st[ti]==rid)continue; st[ti]=rid;
            Vector3 A={tri[ti*9],tri[ti*9+1],tri[ti*9+2]},B={tri[ti*9+3],tri[ti*9+4],tri[ti*9+5]},C={tri[ti*9+6],tri[ti*9+7],tri[ti*9+8]};
            float h=MapRayTri(o,d,A,B,C); if (h>0.001f && h<R && (best<0||h<best)) best=h; }
        if (best>=0 && best<=t) break;                    // nothing nearer can appear in later cells
        if (tMaxX<tMaxY && tMaxX<tMaxZ){ cx+=sx; t=tMaxX; tMaxX+=tDX; if(cx<0||cx>=g->nx)break; }
        else if (tMaxY<tMaxZ){ cy+=sy; t=tMaxY; tMaxY+=tDY; if(cy<0||cy>=g->ny)break; }
        else { cz+=sz; t=tMaxZ; tMaxZ+=tDZ; if(cz<0||cz>=g->nz)break; }
    }
    return best;
}

static Vector3 MapClosestPtTri(Vector3 p, Vector3 a, Vector3 b, Vector3 c){
    Vector3 ab=Vector3Subtract(b,a), ac=Vector3Subtract(c,a), ap=Vector3Subtract(p,a);
    float d1=Vector3DotProduct(ab,ap), d2=Vector3DotProduct(ac,ap);
    if (d1<=0&&d2<=0) return a;
    Vector3 bp=Vector3Subtract(p,b); float d3=Vector3DotProduct(ab,bp), d4=Vector3DotProduct(ac,bp);
    if (d3>=0&&d4<=d3) return b;
    float vc=d1*d4-d3*d2; if (vc<=0&&d1>=0&&d3<=0){ float v=d1/(d1-d3); return Vector3Add(a,Vector3Scale(ab,v)); }
    Vector3 cp=Vector3Subtract(p,c); float d5=Vector3DotProduct(ab,cp), d6=Vector3DotProduct(ac,cp);
    if (d6>=0&&d5<=d6) return c;
    float vb=d5*d2-d1*d6; if (vb<=0&&d2>=0&&d6<=0){ float w=d2/(d2-d6); return Vector3Add(a,Vector3Scale(ac,w)); }
    float va=d3*d6-d5*d4; if (va<=0&&(d4-d3)>=0&&(d5-d6)>=0){ float w=(d4-d3)/((d4-d3)+(d5-d6)); return Vector3Add(b,Vector3Scale(Vector3Subtract(c,b),w)); }
    float den=1.0f/(va+vb+vc), v=vb*den, w=vc*den;
    return Vector3Add(a,Vector3Add(Vector3Scale(ab,v),Vector3Scale(ac,w)));
}

// Does a sphere (centre,radius) intersect any steep (wall) triangle? Floors and
// ceilings (|n.y|>0.7) are ignored - the vertical ray handles those. Used to
// BLOCK movement into walls (so idle players never drift, and they slide along
// walls when one axis is blocked).
static int MapSphereHitsWall(Vector3 c, float radius){
    if (!g_mapCol.ready) return 0; AOGrid *g=&g_mapCol.grid; const float *tri=g_mapCol.tri;
    int x0=(int)((c.x-radius-g->mn.x)/g->cs), x1=(int)((c.x+radius-g->mn.x)/g->cs);
    int y0=(int)((c.y-radius-g->mn.y)/g->cs), y1=(int)((c.y+radius-g->mn.y)/g->cs);
    int z0=(int)((c.z-radius-g->mn.z)/g->cs), z1=(int)((c.z+radius-g->mn.z)/g->cs);
    if(x0<0)x0=0; if(y0<0)y0=0; if(z0<0)z0=0; if(x1>=g->nx)x1=g->nx-1; if(y1>=g->ny)y1=g->ny-1; if(z1>=g->nz)z1=g->nz-1;
    float r2=radius*radius;
    for (int z=z0;z<=z1;z++)for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){
        int cell=MapCellIdx(g,x,y,z);
        for (int e=g->start[cell]; e<g->start[cell+1]; e++){ int ti=g->items[e];
            Vector3 A={tri[ti*9],tri[ti*9+1],tri[ti*9+2]},B={tri[ti*9+3],tri[ti*9+4],tri[ti*9+5]},C={tri[ti*9+6],tri[ti*9+7],tri[ti*9+8]};
            Vector3 fn=Vector3Normalize(Vector3CrossProduct(Vector3Subtract(B,A),Vector3Subtract(C,A)));
            if (fabsf(fn.y)>0.7f) continue;                // skip floors/ceilings
            Vector3 cp=MapClosestPtTri(c,A,B,C); Vector3 v=Vector3Subtract(c,cp);
            if (Vector3DotProduct(v,v)<r2) return 1;
        }
    }
    return 0;
}

// Parse a .map and build a textured, multi-material raylib Model.
static Model LoadQ3MapModel(const char *path, int *okOut){
    if (okOut) *okOut=0;
    Model empty={0};
    FILE *f=fopen(path,"r"); if(!f) return empty;
    MapReg R={0};
    MapFace ctx[8][128]; int ctxN[8]={0}; int sp=0;
    // entity capture: per-entity origin/class/light value, plus the saved spawn
    Vector3 entO={0,0,0}, entCol={1,1,1}, spawnQ={0,0,0};
    int curSpawn=0, curLight=0, haveOrig=0, spawnSet=0; float entLi=300.0f;
    char line[2048];
    while (fgets(line,sizeof line,f)){
        const char *p=line; while (*p==' '||*p=='\t') p++;
        if (*p=='{'){ if (sp==0){ curSpawn=curLight=haveOrig=0; entLi=300.0f; entCol=(Vector3){1,1,1}; } if (sp<8) ctxN[sp]=0; sp++; }
        else if (*p=='}'){ sp--; if (sp>=0 && sp<8 && ctxN[sp]>=4) MapEmitBrush(&R,ctx[sp],ctxN[sp]);
            if (sp==0){
                if (curSpawn && haveOrig && !spawnSet){ spawnQ=entO; spawnSet=1; }
                if (curLight && haveOrig && g_nMapLights<2048){
                    g_mapLights[g_nMapLights].pos=entO; g_mapLights[g_nMapLights].R=entLi;  // Quake space + raw intensity; transformed below
                    g_mapLights[g_nMapLights].color=entCol; g_nMapLights++;
                }
            } }
        else if (*p=='"' && sp==1){                                // entity key/value
            if (!strncmp(p,"\"classname\"",11)){ if (strstr(p,"info_player")) curSpawn=1; else if (strstr(p,"\"light\"")) curLight=1; }
            else if (!strncmp(p,"\"origin\"",8)){ if (sscanf(p,"\"origin\" \"%f %f %f\"",&entO.x,&entO.y,&entO.z)==3) haveOrig=1; }
            else if (!strncmp(p,"\"light\"",7)) sscanf(p,"\"light\" \"%f\"",&entLi);
            else if (!strncmp(p,"\"_color\"",8)) sscanf(p,"\"_color\" \"%f %f %f\"",&entCol.x,&entCol.y,&entCol.z);
        }
        else if (*p=='('){
            Vector3 a,b,c; int used=0;
            if (sscanf(p," ( %f %f %f ) ( %f %f %f ) ( %f %f %f )%n",
                       &a.x,&a.y,&a.z,&b.x,&b.y,&b.z,&c.x,&c.y,&c.z,&used)>=9 && used>0
                && sp>0 && sp-1<8 && ctxN[sp-1]<128){
                char tex[96]; const char *rest=MapTexName(p+used,tex,sizeof tex);
                float sx=0,sy=0,rot=0,scx=0.5f,scy=0.5f;
                sscanf(rest," %f %f %f %f %f",&sx,&sy,&rot,&scx,&scy);   // old format; brushDef -> defaults
                MapFace *fc=&ctx[sp-1][ctxN[sp-1]++];
                fc->pl=MapPlaneFromPts(a,b,c);
                if (MapIsFiller(tex)){ fc->tex=-1; }
                else {
                    fc->tex=MapRegGet(&R,tex);
                    MapUVBasis(fc->pl.n,rot,scx,scy,&fc->stx,&fc->sty);
                    fc->offx=sx; fc->offy=sy;
                    Color mc=MapMatColor(tex); fc->r=mc.r; fc->g=mc.g; fc->b=mc.b;
                }
            }
        }
    }
    fclose(f);

    // total verts + Quake-space bounds (over every texture buffer)
    int total=0; float Mnx=1e30f,Mny=1e30f,Mnz=1e30f,Mxx=-1e30f,Mxy=-1e30f,Mxz=-1e30f;
    for (int i=0;i<R.n;i++){ MapBuf *b=&R.t[i].buf; total+=b->n;
        for (int k=0;k<b->n;k++){ float x=b->pos[k*3],y=b->pos[k*3+1],z=b->pos[k*3+2];
            if(x<Mnx)Mnx=x; if(y<Mny)Mny=y; if(z<Mnz)Mnz=z; if(x>Mxx)Mxx=x; if(y>Mxy)Mxy=y; if(z>Mxz)Mxz=z; } }
    if (total<3){ for(int i=0;i<R.n;i++){ if(R.t[i].hasTex)UnloadTexture(R.t[i].tex);
        free(R.t[i].buf.pos);free(R.t[i].buf.nrm);free(R.t[i].buf.uv);free(R.t[i].buf.col);} free(R.t); return empty; }
    float cx=(Mnx+Mxx)*0.5f, cy=(Mny+Mxy)*0.5f;
    float ext=fmaxf(Mxx-Mnx,fmaxf(Mxy-Mny,Mxz-Mnz)); if(ext<1.0f)ext=1.0f;
    float s=60.0f/ext;
    Vector3 L=Vector3Normalize((Vector3){0.45f,0.85f,0.30f});
    if (spawnSet){ g_mapSpawn=(Vector3){(spawnQ.x-cx)*s,(spawnQ.z-Mnz)*s,(spawnQ.y-cy)*s}; g_hasSpawn=1; }
    for (int l=0;l<g_nMapLights;l++){                  // lights: Quake origin -> raylib, intensity -> reach
        Vector3 q=g_mapLights[l].pos;
        g_mapLights[l].pos=(Vector3){(q.x-cx)*s,(q.z-Mnz)*s,(q.y-cy)*s};
        g_mapLights[l].R=g_mapLights[l].R*s*2.6f; if (g_mapLights[l].R<1.0f) g_mapLights[l].R=1.0f;
    }

    // 1) transform every vertex into raylib space (positions + normals)
    for (int i=0;i<R.n;i++){ MapBuf *b=&R.t[i].buf;
        for (int k=0;k<b->n;k++){
            float qx=b->pos[k*3],qy=b->pos[k*3+1],qz=b->pos[k*3+2];
            b->pos[k*3]=(qx-cx)*s; b->pos[k*3+1]=(qz-Mnz)*s; b->pos[k*3+2]=(qy-cy)*s;
            Vector3 nr=Vector3Normalize((Vector3){b->nrm[k*3],b->nrm[k*3+2],b->nrm[k*3+1]});
            b->nrm[k*3]=nr.x; b->nrm[k*3+1]=nr.y; b->nrm[k*3+2]=nr.z;
        }
    }

    // 2) gather all triangles and build the persistent collision/AO grid
    float *AT=(float*)malloc((size_t)total*3*sizeof(float)); int at=0;
    for (int i=0;i<R.n;i++){ MapBuf *b=&R.t[i].buf;
        for (int k=0;k<b->n*3;k++) AT[at++]=b->pos[k]; }
    MapColBuild(AT, total/3);                              // AT now owned by g_mapCol (collision reuses it)

    // 3) bake per-vertex ambient occlusion + directional shade into vertex colour
    Vector3 samp[AO_RAYS];
    for (int i=0;i<AO_RAYS;i++){ float kk=(i+0.5f)/AO_RAYS, z=1.0f-kk, r=sqrtf(1.0f-z*z), phi=i*2.39996323f;
        samp[i]=(Vector3){cosf(phi)*r, sinf(phi)*r, z}; }
    int *stamp=(int*)calloc(g_mapCol.ntri,sizeof(int)); int rid=0; double t0=GetTime();
    int haveLights=(g_nMapLights>0);
    for (int i=0;i<R.n;i++){ MapBuf *b=&R.t[i].buf;
        for (int k=0;k<b->n;k++){
            Vector3 P={b->pos[k*3],b->pos[k*3+1],b->pos[k*3+2]}, N={b->nrm[k*3],b->nrm[k*3+1],b->nrm[k*3+2]};
            float ao=MapVertexAO(&g_mapCol.grid,g_mapCol.tri,P,N,samp,stamp,&rid);
            float sr,sg,sb;
            if (haveLights){                                  // ambient (AO-darkened) + shadowed point lights
                float amb=0.32f*(1.0f-AO_STRENGTH*ao);
                Vector3 lit=MapVertexLight(P,N,stamp,&rid);
                sr=amb+lit.x; sg=amb+lit.y; sb=amb+lit.z;
            } else {                                          // no lights in the .map: directional fallback
                float ndl=fabsf(Vector3DotProduct(N,L));
                sr=sg=sb=(0.50f+0.50f*ndl)*(1.0f-AO_STRENGTH*ao);
            }
            if(sr<0.03f)sr=0.03f; if(sr>1.4f)sr=1.4f; if(sg<0.03f)sg=0.03f; if(sg>1.4f)sg=1.4f; if(sb<0.03f)sb=0.03f; if(sb>1.4f)sb=1.4f;
            float cr=b->col[k*4]*sr, cg=b->col[k*4+1]*sg, cb=b->col[k*4+2]*sb;
            b->col[k*4]=(unsigned char)(cr>255?255:cr); b->col[k*4+1]=(unsigned char)(cg>255?255:cg); b->col[k*4+2]=(unsigned char)(cb>255?255:cb);
        }
    }
    free(stamp); g_mapAOsec=(float)(GetTime()-t0);

    // 4) build one mesh + material per texture
    int nMesh=0; for (int i=0;i<R.n;i++) if (R.t[i].buf.n>=3) nMesh++;
    Model model={0}; model.transform=MatrixIdentity();
    model.meshCount=nMesh; model.materialCount=nMesh;
    model.meshes=(Mesh*)calloc(nMesh,sizeof(Mesh));
    model.materials=(Material*)calloc(nMesh,sizeof(Material));
    model.meshMaterial=(int*)calloc(nMesh,sizeof(int));
    int mi=0;
    for (int i=0;i<R.n;i++){ MapBuf *b=&R.t[i].buf; if (b->n<3) continue;
        Mesh m={0}; m.vertexCount=b->n; m.triangleCount=b->n/3;
        m.vertices=b->pos; m.normals=b->nrm; m.texcoords=b->uv; m.colors=b->col;
        UploadMesh(&m,false);
        Material mat=LoadMaterialDefault();
        if (R.t[i].hasTex) mat.maps[MATERIAL_MAP_DIFFUSE].texture=R.t[i].tex;
        mat.maps[MATERIAL_MAP_DIFFUSE].color=WHITE;
        model.meshes[mi]=m; model.materials[mi]=mat; model.meshMaterial[mi]=mi; mi++;
    }
    free(R.t);
    if (okOut) *okOut=1;
    return model;
}
#endif // MAPLOAD_H
