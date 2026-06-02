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

// Parse a .map and build a textured, multi-material raylib Model.
static Model LoadQ3MapModel(const char *path, int *okOut){
    if (okOut) *okOut=0;
    Model empty={0};
    FILE *f=fopen(path,"r"); if(!f) return empty;
    MapReg R={0};
    MapFace ctx[8][128]; int ctxN[8]={0}; int sp=0;
    char line[2048];
    while (fgets(line,sizeof line,f)){
        const char *p=line; while (*p==' '||*p=='\t') p++;
        if (*p=='{'){ if (sp<8) ctxN[sp]=0; sp++; }
        else if (*p=='}'){ sp--; if (sp>=0 && sp<8 && ctxN[sp]>=4) MapEmitBrush(&R,ctx[sp],ctxN[sp]); }
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

    // transform positions to raylib space, bake directional shade into colour
    int nMesh=0; for (int i=0;i<R.n;i++) if (R.t[i].buf.n>=3) nMesh++;
    Model model={0}; model.transform=MatrixIdentity();
    model.meshCount=nMesh; model.materialCount=nMesh;
    model.meshes=(Mesh*)calloc(nMesh,sizeof(Mesh));
    model.materials=(Material*)calloc(nMesh,sizeof(Material));
    model.meshMaterial=(int*)calloc(nMesh,sizeof(int));
    int mi=0;
    for (int i=0;i<R.n;i++){
        MapBuf *b=&R.t[i].buf; if (b->n<3) continue;
        for (int k=0;k<b->n;k++){
            float qx=b->pos[k*3],qy=b->pos[k*3+1],qz=b->pos[k*3+2];
            b->pos[k*3]=(qx-cx)*s; b->pos[k*3+1]=(qz-Mnz)*s; b->pos[k*3+2]=(qy-cy)*s;
            Vector3 nr=Vector3Normalize((Vector3){b->nrm[k*3],b->nrm[k*3+2],b->nrm[k*3+1]});
            b->nrm[k*3]=nr.x; b->nrm[k*3+1]=nr.y; b->nrm[k*3+2]=nr.z;
            float sh=0.60f+0.40f*fabsf(Vector3DotProduct(nr,L));   // ambient floor + directional (max 1.0)
            b->col[k*4]=(unsigned char)(b->col[k*4]*sh);
            b->col[k*4+1]=(unsigned char)(b->col[k*4+1]*sh);
            b->col[k*4+2]=(unsigned char)(b->col[k*4+2]*sh);
        }
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
