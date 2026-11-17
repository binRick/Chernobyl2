// mines.h -- the claymore: weapon 5 (owner, 2026-11-17). --mines / ./dev-mines.sh is its sandbox: one in the hand from
// the start, and they never run out.
//
// In the hand: the right arm of the grenade launcher's rig carrying an M18A1 by its end (assets/claymore_vm.glb, built
// with its clips by tools/mines/build_claymore.py). Left click lays it: the view kneels and dips to the floor while the
// hand reaches down and sets it there, facing the way you look. The hand lets go at a fixed frame of the clip and the
// mine it held is handed to the world exactly where it was: the viewmodel is drawn through the world's own lens, and
// the clip was authored to bring its feet to the floor this dip shows (assets/claymore_vm.txt carries both).
// Laid, it arms (a blinking light, then a red fan of laser over the arc it covers) and fires when one of them walks
// into that arc: sixty degrees of steel balls, seven metres out. Others close by go with it.
// 5 takes it out (the phone's fifth button), 1-4 the guns. Three carried at the start of a life, MINE_CARRY at most, an
// ammo box adds one; laying the last puts it away and the gun comes back up. Dev: CHERNOBYL_MINE_AUTO=<s> lays one every
// s seconds while it is in the hand; CHERNOBYL_MINE_PICK="s1,s2,..." presses 5 at those seconds (tests/claymore.sh).
#define MINE_RANGE   7.0f        // its fan, and what trips it, metres
#define MINE_KILL    9.0f        // how far the balls kill
#define MINE_ARC     30.0f       // half the fan, degrees
#define MINE_ARM_T   1.4f        // laid -> armed, s
#define MINE_SET_T   0.16f       // out of the hand onto the floor, s
#define MINE_FUSE    0.12f       // tripped -> fired, s
#define MINE_CHAIN   3.0f        // another mine this close goes with it, m
#define MAX_MINES    12
#define MINE_CARRY0  3           // carried at the start of a life
#define MINE_CARRY   5           // the most he can carry (an ammo box adds one)
enum { MS_SET=1, MS_ARMING, MS_ARMED, MS_FIRING };
typedef struct { int on, state, beamOk; Vector3 pos, p0; Quaternion q0, q1; float yaw, t, fuse, beam[3]; } Mine;   // beam: its three lasers' lengths, found once (the map does not move)
static Mine g_mines[MAX_MINES];
static int   g_minesOn=1, g_mineStart=0, g_minesLeft=MINE_CARRY0, g_mineMode=0, g_mineTried=0, g_mineVmOk=0, g_mineWOk=0, g_mineWrist=-1, g_mineIdle=-1, g_mineLay=-1, g_minePlaced=0;
static float g_layT=-1.0f, g_mineIdleT=0.0f, g_mineRaise=0.0f, g_mineModeT=0.0f;
static float g_mineDipPitch=60.0f, g_mineDipEye=0.62f, g_mineDipLean=0.25f, g_mineFps=24.0f;
static int   g_layFrames=24, g_layRel=12, g_layHid0=17, g_layHid1=19;
static float g_mineBeepT=0.0f;            // the trip's beep is the traps' lever sound pitched up: its pitch is given back when this runs out
static Vector3 g_mineO, g_mineA, g_mineU, g_mineF, g_mineEye={12,14,-45};
static Model g_mineVm, g_mineW; static ModelAnimation *g_mineAn=NULL; static int g_mineAnN=0;
static unsigned long long g_mineMask=0;   // bit k: mesh k of the viewmodel is the mine (skinned wholly to the wrist), not the arm

// ---- loading
// The mine is ONE mesh on ONE sheet of textures (tools/mines/build_claymore.py one_sheet): colour, normal and ORM, for the
// lit shader that draws it on the floor. The one in the hand is drawn by raylib's default shader (texture x vertex
// colour, as the guns are) and its file holds no texture at all: it is given that same colour sheet, and its sleeve and
// glove are the grenade launcher's own (it is that rig's right arm), taken from the launcher as loaded. Neither file
// names an image, so raylib's loader decodes none and each picture is read once, here.
// (As first built the two files named seventeen 1024 px images. raylib decoded every one, twelve were then read again
// by name, and its copies of the normal and ORM maps were never freed: 0.39 s of the load and some 60 MB of video
// memory, for four draws a mine. tests/claymore-lean.sh)
#define MINE_ARM_SLOT 0                                               // main.c's slot for the grenade launcher: the rig the claymore's arm is cut from
typedef struct { char name[24]; Texture2D c, n, o; } MineMat;
static MineMat g_mineMat[4]; static int g_nMineMat=0;
static int g_mineTexN=0, g_mineVmTex=0, g_mineMs=0;                   // pictures read (3); materials of the hand model given one already loaded (2); how long it all took
static Texture2D MineTex(const char *name, const char *kind){ Texture2D t={0}; char p[96];
    snprintf(p,sizeof p,"assets/mines/%s_%s.png",name,kind);
    if (!FileExists(p)){ snprintf(p,sizeof p,"assets/mines/%s_%s.jpg",name,kind); if (!FileExists(p)) return t; }   // (the browser / phone pack carries them as .jpg)
    t=LoadTexture(p); if (t.id>0){ g_mineTexN++; GenTextureMipmaps(&t); SetTextureFilter(t,TEXTURE_FILTER_ANISOTROPIC_8X); } return t; }
static const MineMat *MineMatOf(const char *name){                    // a material's sheets, read the first time it is asked for (the traps' way: traps.h TrapTexOf)
    for (int i=0;i<g_nMineMat;i++) if (!strcmp(g_mineMat[i].name,name)) return &g_mineMat[i];
    if (g_nMineMat>=4) return NULL;
    MineMat *m=&g_mineMat[g_nMineMat++]; snprintf(m->name,sizeof m->name,"%s",name);
    m->c=MineTex(name,"c"); m->n=MineTex(name,"n"); m->o=MineTex(name,"orm"); return m; }
static int MineMatNames(const char *js, char nm[][24], int max){      // a glTF's materials by name, in the file's order (raylib keeps that order, after its own default at 0)
    int n=0; const char *q=js?strstr(js,"\"materials\""):NULL, *z=q?strstr(q,"\"meshes\""):NULL;
    for (; q && z && n<max && (q=strstr(q,"\"name\""))!=NULL && q<z; n++){
        const char *v=strchr(q+6,'"'); if (!v) break; v++; int k=0; while (v[k] && v[k]!='"' && k<23){ nm[n][k]=v[k]; k++; } nm[n][k]=0; q=v+k; }
    return n; }
static char *MineGlbJson(const char *path){                           // a .glb's JSON chunk as text (MemFree it), or NULL
    int n=0; unsigned char *d=LoadFileData(path,&n); char *js=NULL; unsigned int len=0;
    if (d && n>20 && !memcmp(d,"glTF",4) && !memcmp(d+16,"JSON",4)){ memcpy(&len,d+12,4);
        if (len>0 && 20+(long)len<=(long)n){ js=(char*)MemAlloc(len+1); memcpy(js,d+20,len); js[len]=0; } }
    if (d) UnloadFileData(d); return js; }
static Texture2D MineSleeve(void){                                    // the launcher's sleeve and glove: the colour map of the one mesh of it that is arm, not gun
    const Weapon *w=&g_weapons[MINE_ARM_SLOT]; Texture2D none={0}; if (!w->has) return none;
    for (int k=0;k<w->model.meshCount && k<64;k++) if (!((w->gunMask>>k)&1ULL) && w->model.meshes[k].vertexCount>0){
        Texture2D t=w->model.materials[w->model.meshMaterial[k]].maps[MATERIAL_MAP_DIFFUSE].texture; return (t.id>0 && t.id!=rlGetTextureIdDefault())?t:none; }
    return none; }
static void MinesLoadWorld(void){                                   // the mine in the world: the traps' way (traps.h TrapsLoadModels)
    const char *path="assets/mines/claymore.gltf"; if (!FileExists(path) || EnemySkinInit()!=1) return;
    Model m=LoadModel(path);
    if (m.meshCount<=0) return;
    Image a=GenImageColor(1,1,(Color){128,128,255,255}), b=GenImageColor(1,1,(Color){255,204,0,255}), c=GenImageColor(1,1,(Color){0,0,0,255});
    Texture2D flatN=LoadTextureFromImage(a), defOrm=LoadTextureFromImage(b), noEmi=LoadTextureFromImage(c); UnloadImage(a); UnloadImage(b); UnloadImage(c);
    char *txt=LoadFileText(path); char nm[4][24]; int n=MineMatNames(txt,nm,4); if (txt) UnloadFileText(txt);
    for (int i=0;i<n && i+1<m.materialCount;i++){ const MineMat *t=MineMatOf(nm[i]); MaterialMap *mm=m.materials[i+1].maps; if (!t) continue;
        if (t->c.id>0) mm[MATERIAL_MAP_DIFFUSE].texture=t->c;
        if (t->n.id>0) mm[MATERIAL_MAP_NORMAL].texture=t->n;
        if (t->o.id>0) mm[MATERIAL_MAP_OCCLUSION].texture=t->o; }
    for (int i=0;i<m.meshCount;i++){ Mesh *ms=&m.meshes[i]; if (!ms->tangents && ms->texcoords && ms->normals) GenMeshTangents(ms); }
    for (int i=0;i<m.materialCount;i++){ MaterialMap *mm=m.materials[i].maps; m.materials[i].shader=g_skinShader;
        if (mm[MATERIAL_MAP_NORMAL].texture.id==0) mm[MATERIAL_MAP_NORMAL].texture=flatN;
        if (mm[MATERIAL_MAP_OCCLUSION].texture.id==0) mm[MATERIAL_MAP_OCCLUSION].texture=defOrm;
        if (mm[MATERIAL_MAP_EMISSION].texture.id==0 || mm[MATERIAL_MAP_EMISSION].texture.id==rlGetTextureIdDefault()) mm[MATERIAL_MAP_EMISSION].texture=noEmi; }
    g_mineW=m; g_mineWOk=1;
}
static void MinesLoad(void){
    g_mineTried=1; double t0=GetTime();
    const char *vp="assets/claymore_vm.glb"; FILE *f=fopen("assets/claymore_vm.txt","r");
    if (f && FileExists(vp)){
        char tag[32]; float eyeCm=68.0f;
        while (fscanf(f,"%31s",tag)==1){
            if (!strcmp(tag,"origin")) fscanf(f,"%f %f %f",&g_mineO.x,&g_mineO.y,&g_mineO.z);
            else if (!strcmp(tag,"across")) fscanf(f,"%f %f %f",&g_mineA.x,&g_mineA.y,&g_mineA.z);
            else if (!strcmp(tag,"up")) fscanf(f,"%f %f %f",&g_mineU.x,&g_mineU.y,&g_mineU.z);
            else if (!strcmp(tag,"out")) fscanf(f,"%f %f %f",&g_mineF.x,&g_mineF.y,&g_mineF.z);
            else if (!strcmp(tag,"eye")) fscanf(f,"%f %f %f",&g_mineEye.x,&g_mineEye.y,&g_mineEye.z);
            else if (!strcmp(tag,"dip")){ float lean=25.0f; fscanf(f,"%f %f %f",&g_mineDipPitch,&eyeCm,&lean); g_mineDipEye=eyeCm/100.0f; g_mineDipLean=lean/100.0f; }
            else if (!strcmp(tag,"lay")) fscanf(f,"%d %d %d %d",&g_layFrames,&g_layRel,&g_layHid0,&g_layHid1);
            else if (!strcmp(tag,"fps")) fscanf(f,"%f",&g_mineFps);
            else { int c; while ((c=fgetc(f))!=EOF && c!='\n'){} } }
        g_mineVm=LoadModel(vp); g_mineAn=LoadModelAnimations(vp,&g_mineAnN);
        for (int a=0;a<g_mineAnN;a++){ if (!strcmp(g_mineAn[a].name,"Idle")) g_mineIdle=a; if (!strcmp(g_mineAn[a].name,"Lay")) g_mineLay=a; }
        for (int b=0;b<g_mineVm.skeleton.boneCount;b++) if (!strcmp(g_mineVm.skeleton.bones[b].name,"R_wrist_026")) g_mineWrist=b;
        for (int k=0;k<g_mineVm.meshCount && k<64;k++){ const Mesh *ms=&g_mineVm.meshes[k]; int all=(ms->boneIndices && ms->boneWeights && ms->vertexCount>0);
            for (int v=0; all && v<ms->vertexCount; v+=7) if (ms->boneWeights[v*4]<0.99f || ms->boneIndices[v*4]!=g_mineWrist) all=0;
            if (all) g_mineMask|=(1ULL<<k); }
        g_mineVmOk=(g_mineVm.meshCount>0 && g_mineIdle>=0 && g_mineLay>=0 && g_mineWrist>=0 && g_mineMask);
        // The mine rides the wrist whole, so it is drawn by that bone's matrix (MinesDrawViewmodel), not skinned vertex by
        // vertex: without its animated copies raylib's UpdateModelAnimation passes it by (and leaves its buffers as loaded).
        for (int k=0;g_mineVmOk && k<g_mineVm.meshCount && k<64;k++) if ((g_mineMask>>k)&1ULL){ Mesh *ms=&g_mineVm.meshes[k];
            MemFree(ms->animVertices); MemFree(ms->animNormals); ms->animVertices=NULL; ms->animNormals=NULL; }
        char *js=MineGlbJson(vp); char nm[4][24]; int n=MineMatNames(js,nm,4); if (js) MemFree(js);       // its materials, by name: textures already loaded
        for (int i=0;i<n && i+1<g_mineVm.materialCount;i++){ Texture2D t={0};
            if (!strcmp(nm[i],"arms")) t=MineSleeve(); else { const MineMat *mt=MineMatOf(nm[i]); if (mt) t=mt->c; }
            if (t.id>0){ g_mineVm.materials[i+1].maps[MATERIAL_MAP_DIFFUSE].texture=t; g_mineVmTex++; } }
    }
    if (f) fclose(f);
    MinesLoadWorld();
    g_mineMs=(int)((GetTime()-t0)*1000.0+0.5);
    DebugLog("mines","\"vm\":%d,\"world\":%d,\"anims\":%d,\"wrist\":%d,\"mineMeshes\":%llu,\"meshes\":%d,\"tex\":%d,\"vmTex\":%d,\"ms\":%d",
             g_mineVmOk, g_mineWOk, g_mineAnN, g_mineWrist, g_mineMask, g_mineWOk?g_mineW.meshCount:0, g_mineTexN, g_mineVmTex, g_mineMs);
}

// ---- the hand and the view
static Matrix MineVmMatrix(void){                                   // the model's own space (cm, its eye at g_mineEye, looking +z) -> the camera's (m, eye at 0, looking -z)
    float still=(g_layT>=0.0f)?0.0f:1.0f, ease=g_mineRaise*g_mineRaise;   // (no bob or sway while it is being laid: the clip was authored against a still view)
    Vector3 P={ (sinf(g_bob)*0.010f - g_vmSway.x*0.0022f)*still, (-fabsf(cosf(g_bob))*0.008f + g_vmSway.y*0.0022f)*still - ease*0.30f, 0.0f };
    return MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixTranslate(-g_mineEye.x,-g_mineEye.y,-g_mineEye.z), MatrixRotateY(PI)),
                                         MatrixScale(0.01f,0.01f,0.01f)), MatrixTranslate(P.x,P.y,P.z));
}
static float MineDipW(void){                                        // 0 standing .. 1 knelt, over the Lay clip
    if (g_layT<0.0f) return 0.0f; float t=g_layT, a=0.40f, b=0.60f, c=0.98f;
    if (t<a){ float x=t/a; return x*x*(3.0f-2.0f*x); } if (t<b) return 1.0f;
    if (t<c){ float x=(t-b)/(c-b); return 1.0f-x*x*(3.0f-2.0f*x); } return 0.0f;
}
static void MinesDip(Vector3 *eye, Vector3 *fwd){                   // (main.c, building the camera) kneel to the clip's eye height and dip to its pitch -- absolute, so its floor is this floor
    float w=MineDipW(); if (!g_minesOn || w<=0.0f) return;
    float floorY=g_pos.y-EYE_H, yaw=atan2f(fwd->x,fwd->z);
    eye->y += ((floorY+g_mineDipEye)-eye->y)*w;
    eye->x += sinf(yaw)*g_mineDipLean*w; eye->z += cosf(yaw)*g_mineDipLean*w;          // leaning out over where it goes
    float p0=asinf(fmaxf(-1.0f,fminf(1.0f,fwd->y))), p=p0+(-g_mineDipPitch*DEG2RAD-p0)*w;
    *fwd=(Vector3){ sinf(yaw)*cosf(p), sinf(p), cosf(yaw)*cosf(p) };
}
static void MinesDrawViewmodel(void){
    if (!g_mineVmOk) return;
    ModelAnimation *an; float kf; int hide=0;
    if (g_layT>=0.0f){ an=&g_mineAn[g_mineLay]; kf=g_layT*60.0f; float lf=g_layT*g_mineFps; hide=(lf>=(float)g_layRel && lf<(float)g_layHid1); }   // let go of, until the hand comes back up with the next
    else { an=&g_mineAn[g_mineIdle]; float len=(ANIM_FRAMES(*an)-1)/60.0f; kf=fmodf(g_mineIdleT,len)*60.0f; }
    if (kf>ANIM_FRAMES(*an)-1) kf=(float)(ANIM_FRAMES(*an)-1);
    ANIM_APPLY(g_mineVm,*an,kf);
    Matrix M=MineVmMatrix(), MW=MatrixMultiply(g_mineVm.boneMatrices[g_mineWrist],M);   // the arm, skinned; the mine, carried whole by the wrist (MinesLoad)
    Camera3D c={ {0,0,0}, {0,0,-1}, {0,1,0}, g_cam.fovy, CAMERA_PERSPECTIVE };   // the world's own lens (not the guns'): what the hand lets go of is where it was
    rlDrawRenderBatchActive();
#if !defined(_WIN32)
    glClear(GL_DEPTH_BUFFER_BIT);
#endif
    rlSetClipPlanes(0.05, 4000.0);
    BeginMode3D(c);
        for (int k=0;k<g_mineVm.meshCount;k++){ int mine=(k<64 && ((g_mineMask>>k)&1ULL)); if (hide && mine) continue;
            DrawMesh(g_mineVm.meshes[k], g_mineVm.materials[g_mineVm.meshMaterial[k]], mine?MW:M); }
    EndMode3D();
}

// ---- laid
static Vector3 MineFwd(const Mine *m){ return (Vector3){ sinf(m->yaw), 0.0f, cosf(m->yaw) }; }
static Vector3 MineSight(const Mine *m){ Vector3 f=MineFwd(m); return (Vector3){ m->pos.x+f.x*0.012f, m->pos.y+0.136f, m->pos.z+f.z*0.012f }; }
static void MinePlace(void){                                        // the hand lets go: the mine it held, carried from the clip's keyframe to the world
    ModelAnimation *an=&g_mineAn[g_mineLay]; int kf=(int)(g_layRel/g_mineFps*60.0f+0.5f); if (kf>=an->keyframeCount) kf=an->keyframeCount-1;
    Matrix skin=MatrixMultiply(MatrixInvert(XformToMatrix(g_mineVm.skeleton.bindPose[g_mineWrist])), XformToMatrix(an->keyframePoses[kf][g_mineWrist]));
    Matrix M=MatrixMultiply(skin, MineVmMatrix());                  // rest pose -> held at the release -> the camera
    Vector3 o=Vector3Transform(g_mineO,M);
    Vector3 ax=Vector3Normalize(Vector3Subtract(Vector3Transform(Vector3Add(g_mineO,g_mineA),M),o));
    Vector3 au=Vector3Normalize(Vector3Subtract(Vector3Transform(Vector3Add(g_mineO,g_mineU),M),o));
    Vector3 af=Vector3Normalize(Vector3Subtract(Vector3Transform(Vector3Add(g_mineO,g_mineF),M),o));
    Vector3 fw=Vector3Normalize(Vector3Subtract(g_cam.target,g_cam.position)), rt=Vector3Normalize(Vector3CrossProduct(fw,(Vector3){0,1,0})), up=Vector3CrossProduct(rt,fw);
    #define MINE_C2W(v) Vector3Add(Vector3Add(Vector3Scale(rt,(v).x),Vector3Scale(up,(v).y)),Vector3Scale(fw,-(v).z))
    Vector3 P0=Vector3Add(g_cam.position,MINE_C2W(o)), A=MINE_C2W(ax), U=MINE_C2W(au), F=MINE_C2W(af);
    #undef MINE_C2W
    int slot=-1; float oldest=-1.0f;                                // the oldest goes if they are all out
    for (int i=0;i<MAX_MINES;i++){ if (!g_mines[i].on){ slot=i; break; } if (g_mines[i].t>oldest){ oldest=g_mines[i].t; slot=i; } }
    Mine *m=&g_mines[slot]; memset(m,0,sizeof *m);
    Matrix R={0}; R.m0=A.x; R.m1=A.y; R.m2=A.z; R.m4=U.x; R.m5=U.y; R.m6=U.z; R.m8=F.x; R.m9=F.y; R.m10=F.z; R.m15=1.0f;
    m->on=1; m->state=MS_SET; m->p0=P0; m->q0=QuaternionNormalize(QuaternionFromMatrix(R));
    m->yaw=atan2f(F.x,F.z); m->q1=QuaternionFromMatrix(MatrixRotateY(m->yaw));
    float d=MapRayNearest((Vector3){P0.x,P0.y+0.3f,P0.z},(Vector3){0,-1,0},3.0f);
    m->pos=(Vector3){ P0.x, (d>0.0f)?P0.y+0.3f-d:g_pos.y-EYE_H, P0.z };
    if (!g_mineStart && g_minesLeft>0) g_minesLeft--;                                   // (the sandbox never runs out)
    DebugLog("mine","\"placed\":%d,\"at\":[%.2f,%.2f,%.2f],\"drop\":%.3f,\"yaw\":%.1f,\"upY\":%.2f,\"left\":%d", slot, m->pos.x,m->pos.y,m->pos.z, P0.y-m->pos.y, m->yaw*RAD2DEG, U.y, g_minesLeft);
}
static void MineExplode(Mine *m){
    Vector3 f=MineFwd(m), c=(Vector3){ m->pos.x, m->pos.y+0.09f, m->pos.z };
    int killsBefore=g_kills, gibsBefore=g_gibKills, hit=0;
    for (int i=0;i<MAX_ENEMIES;i++){ Enemy *e=&g_enemies[i]; if (e->state!=1) continue;
        Vector3 ch={ e->pos.x, e->pos.y+ENEMY_HEIGHT*0.5f, e->pos.z }, v=Vector3Subtract(ch,c); float dist=Vector3Length(v);
        Vector3 vh={ v.x, 0.0f, v.z }; float lh=Vector3Length(vh); int inArc=(lh>1e-3f && Vector3DotProduct(Vector3Scale(vh,1.0f/lh),f)>cosf(MINE_ARC*DEG2RAD) && dist<MINE_KILL);
        if (inArc){ float h=MapRayNearest(c,Vector3Scale(v,1.0f/dist),dist); if (h>0.0f && h<dist-0.4f) inArc=0; }   // (behind a wall: the balls stop there)
        if (inArc || dist<1.6f){
            g_hurtCause=HC_BLAST; g_hurtFrom=Vector3Subtract(c,Vector3Scale(f,1.5f));
            HurtEnemy(i, e->boss ? 60.0f+300.0f*(1.0f-dist/MINE_KILL) : 99999.0f, 0, f, ch, 1); g_hurtCause=HC_BULLET; hit++; }
    }
    DynLightAdd((Vector3){c.x+f.x*0.8f,c.y+0.5f,c.z+f.z*0.8f}, (Vector3){3.4f,2.0f,0.9f}, 18.0f, 0.45f, 0);
    for (int q=0;q<18;q++) SpawnSmoke(Vector3Add(c,Vector3Scale(f,GetRandomValue(0,150)/100.0f)),1);
    for (int q=0;q<44;q++){ float a=(GetRandomValue(-100,100)/100.0f)*MINE_ARC*DEG2RAD, e=GetRandomValue(-4,14)/100.0f;   // the fan of fire
        float yw=m->yaw+a; SpawnFlame(c,Vector3Normalize((Vector3){sinf(yw),e,cosf(yw)})); }
    for (int q=0;q<28;q++){ float a=(GetRandomValue(-100,100)/100.0f)*MINE_ARC*DEG2RAD, e=GetRandomValue(-10,12)/100.0f;   // the balls, where they strike
        float yw=m->yaw+a; Vector3 d=Vector3Normalize((Vector3){sinf(yw),e,cosf(yw)}), n; float h=MapRayNearestEx(c,d,14.0f,&n,NULL);
        if (h>0.0f) SpawnImpact(Vector3Add(c,Vector3Scale(d,h)),n); }
    SpawnImpact(c,(Vector3){0,1,0});
    AddScorch((Vector3){m->pos.x,m->pos.y+0.02f,m->pos.z},(Vector3){0,1,0},1.1f);
    { float dp=Vector3Distance(g_pos,c); g_shake=fminf(1.0f,g_shake+0.75f*fmaxf(0.25f,1.0f-dp/14.0f));
      if (!g_godMode && dp<3.0f) g_playerHp-=90.0f*(1.0f-dp/3.0f)*PLAYER_DMG_K; }
    if (g_audio && g_hasGrenSnd) SfxAt(g_grenSnd,c,1.0f);
    { int bodies=(g_kills-killsBefore)-(g_gibKills-gibsBefore);
      if (bodies>=MULTIKILL_N && g_audio && g_hasCrowdSnd) SfxAt(g_crowdSnd,c,1.0f); }
    for (int i=0;i<MAX_MINES;i++){ Mine *o=&g_mines[i]; if (o==m || !o->on || o->state==MS_FIRING) continue;   // the ones beside it go with it
        if (Vector3Distance(o->pos,m->pos)<MINE_CHAIN){ o->state=MS_FIRING; o->fuse=0.18f; } }
    DebugLog("mine","\"explode\":[%.1f,%.1f,%.1f],\"hit\":%d,\"kills\":%d", c.x,c.y,c.z, hit, g_kills-killsBefore);
    m->on=0;
}
// 5: take it out -- main.c's weapon keys, the phone's fifth button (C2KeyPressed), CHERNOBYL_MINE_PICK
static void MinesPick(void){
    if (!g_minesOn || g_mineMode || g_layT>=0.0f) return;
    if (!g_mineTried) MinesLoad();
    if (!g_mineVmOk) return;
    if (!g_mineStart && g_minesLeft<=0){ snprintf(g_pkMsg,sizeof g_pkMsg,"NO CLAYMORES"); g_pkMsgT=1.2f; DebugLog("mine","\"pick\":0,\"left\":0"); return; }
    g_mineMode=1; g_mineRaise=1.0f; g_mineModeT=0.0f;
    if (g_audio) ExtraPlay(EX_SWITCH,0.6f);                       // drawn, as the guns are
    DebugLog("mine","\"pick\":1,\"left\":%d", g_minesLeft);
}
static void MinesAmmo(void){ if (g_minesLeft<MINE_CARRY) g_minesLeft++; }   // an ammo box (main.c PK_AMMO): one more
static void MinesUpdate(float dt){
    if (!g_minesOn) return;
    if (!g_mineTried) MinesLoad();
    { static float pickAt[8]; static int pickN=-1, pickI=0; static float clock=0.0f; clock+=dt;   // dev: CHERNOBYL_MINE_PICK="s1,s2" -- 5 at those seconds
      if (pickN<0){ pickN=0; const char *e=getenv("CHERNOBYL_MINE_PICK"); while (e && *e && pickN<8){ pickAt[pickN++]=(float)atof(e); e=strchr(e,','); if (e) e++; } }
      if (pickI<pickN && clock>=pickAt[pickI]){ pickI++; MinesPick(); } }
    g_mineIdleT+=dt; if (g_mineMode) g_mineModeT+=dt;
    if (g_mineRaise>0.0f) g_mineRaise=fmaxf(0.0f,g_mineRaise-dt*3.5f);
    static float autoGap=-1.0f, autoT=1.5f;                          // dev: CHERNOBYL_MINE_AUTO=<s> lays one every s seconds
    if (autoGap<0.0f){ const char *e=getenv("CHERNOBYL_MINE_AUTO"); autoGap=e?(float)atof(e):0.0f; }
    int autoLay=0; if (autoGap>0.0f && g_mineMode){ autoT-=dt; autoLay=(autoT<=0.0f); }   // (held until he can lay: on his feet, the last one down)
    static float gunAt=-2.0f; static int gunEnv=0, gunDue=0;         // dev: CHERNOBYL_MINE_GUN=<s> -- put it away for the gun after s (tests of shooting one)
    if (gunAt<-1.0f){ const char *e=getenv("CHERNOBYL_MINE_GUN"); gunEnv=(e!=NULL); gunAt=e?(float)atof(e):-1.0f; }   // (read once: not a getenv a frame)
    if (gunAt>0.0f){ gunAt-=dt; if (gunAt<=0.0f) gunDue=1; }
    if (gunDue && g_layT<0.0f){ g_mineMode=0; gunDue=0; gunAt=-1.0f; DebugLog("mine","\"gun\":1"); }   // (due while one is being laid: once it is down)
    if (gunEnv && gunAt==-1.0f){ float bd=1e9f; Mine *t=NULL;                                   // ...and keep the sights on the last one laid
        for (int i=0;i<MAX_MINES;i++) if (g_mines[i].on && g_mines[i].t<bd){ bd=g_mines[i].t; t=&g_mines[i]; }
        if (t){ float dx=t->pos.x-g_pos.x, dy=t->pos.y+0.08f-g_pos.y, dz=t->pos.z-g_pos.z; g_yaw=atan2f(dx,dz); g_pitch=atan2f(dy,sqrtf(dx*dx+dz*dz)); } }
    if (g_mineBeepT>0.0f){ g_mineBeepT-=dt; if (g_mineBeepT<=0.0f && g_hasTrapSnd) SetSoundPitch(g_trapSnd[TS_ARM],1.0f); }   // the trip's beep has played: the lever sounds as a lever again
    if (g_mineMode && g_mineVmOk && g_layT<0.0f && g_mineRaise<=0.2f && g_grounded && !g_paused && !g_menu && !g_dead && (g_mineStart || g_minesLeft>0)   // (on his feet: it is set on the floor he kneels on)
        && (autoLay || (!g_swallowClick && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))){ g_layT=0.0f; g_minePlaced=0; if (autoLay) autoT=autoGap; DebugLog("mine","\"lay\":1"); }
    if (g_layT>=0.0f){
        g_layT+=dt;                                                  // (first: the frame the hand is drawn without it is the frame it is on the floor -- placed before this, it was in neither for one)
        if (!g_minePlaced && g_layT*g_mineFps>=(float)g_layRel){ MinePlace(); g_minePlaced=1; }
        if (g_layT*g_mineFps>=(float)g_layFrames){ g_layT=-1.0f;
            if (!g_mineStart && g_minesLeft<=0){ g_mineMode=0; g_vmRaise=1.0f; DebugLog("mine","\"empty\":1"); } }   // the last one laid: the gun back up
    }
    for (int i=0;i<MAX_MINES;i++){ Mine *m=&g_mines[i]; if (!m->on) continue; m->t+=dt;
        if (m->state==MS_SET && m->t>=MINE_SET_T){ m->state=MS_ARMING; m->t=0.0f;
            if (g_audio && g_hasSy){ SetSoundPitch(g_sy[SY_STEP],0.55f); SfxAt(g_sy[SY_STEP],m->pos,0.5f); }   // (SfxAt sets the volume itself: the half goes in as its gain)
            for (int q=0;q<3;q++) SpawnSmoke((Vector3){m->pos.x,m->pos.y+0.03f,m->pos.z},0); }
        else if (m->state==MS_ARMING && m->t>=MINE_ARM_T){ m->state=MS_ARMED; m->t=0.0f;
            if (g_audio && g_hasTrapSnd){ SetSoundPitch(g_trapSnd[TS_ARM],1.0f); g_mineBeepT=0.0f; SfxAt(g_trapSnd[TS_ARM],m->pos,0.8f); }
            DebugLog("mine","\"armed\":%d",i); }
        else if (m->state==MS_ARMED){                                // anything in its fan, in sight of it
            Vector3 f=MineFwd(m), s=MineSight(m);
            for (int k=0;k<MAX_ENEMIES;k++){ const Enemy *e=&g_enemies[k]; if (e->state!=1) continue;
                Vector3 v={ e->pos.x-m->pos.x, 0.0f, e->pos.z-m->pos.z }; float l=Vector3Length(v);
                if (l>MINE_RANGE || l<1e-3f || fabsf(e->pos.y-m->pos.y)>1.8f || Vector3DotProduct(Vector3Scale(v,1.0f/l),f)<cosf(MINE_ARC*DEG2RAD)) continue;
                Vector3 to=Vector3Subtract((Vector3){e->pos.x,e->pos.y+0.5f,e->pos.z},s); float tl=Vector3Length(to), h=MapRayNearest(s,Vector3Scale(to,1.0f/tl),tl);
                if (h>0.0f && h<tl-0.3f) continue;
                m->state=MS_FIRING; m->fuse=MINE_FUSE; DebugLog("mine","\"tripped\":%d,\"by\":%d,\"at\":%.1f",i,k,l);
                if (g_audio && g_hasTrapSnd){ SetSoundPitch(g_trapSnd[TS_ARM],1.6f); SfxAt(g_trapSnd[TS_ARM],m->pos,1.0f); g_mineBeepT=0.3f; }   // (a pitch set back at once is never heard: raylib re-pitches the sound as it plays)
                break; } }
        if (m->state==MS_FIRING){ m->fuse-=dt; if (m->fuse<=0.0f) MineExplode(m); }
    }
}

// ---- what sets one off besides a man in its fan: a round into it, a blast beside it (main.c's guns call these). Not fire:
// the flamethrower's wide cone is used right where the claymores are, and cooking them off in it threw everyone in their
// fan off his feet -- the owner's "the flamethrower shouldn't knock enemies down" (tests/flame.sh)
static int MinesRayHit(Vector3 o, Vector3 d, float maxD, float *outD){   // the first laid one a ray strikes before maxD, or -1
    if (!g_minesOn) return -1; int best=-1; float bd=maxD;
    for (int i=0;i<MAX_MINES;i++){ const Mine *m=&g_mines[i]; if (!m->on) continue;
        Vector3 c={ m->pos.x, m->pos.y+0.08f, m->pos.z }, oc=Vector3Subtract(c,o); float t=Vector3DotProduct(oc,d);   // its body: 13 cm round its middle
        if (t<0.0f || t>bd+0.13f) continue; float r2=Vector3DotProduct(oc,oc)-t*t; if (r2>0.13f*0.13f) continue;
        float tt=t-sqrtf(0.13f*0.13f-r2); if (tt<0.0f) tt=0.0f; if (tt<bd){ bd=tt; best=i; } }
    if (outD) *outD=bd; return best;
}
static void MineSetOff(int i, float fuse){
    if (i<0 || i>=MAX_MINES) return; Mine *m=&g_mines[i]; if (!m->on || m->state==MS_FIRING) return;
    m->state=MS_FIRING; m->fuse=fuse; DebugLog("mine","\"setoff\":%d,\"fuse\":%.2f",i,fuse);
}
static void MinesBlast(Vector3 p, float r){                          // a grenade (or anything that bursts) beside them
    if (!g_minesOn) return;
    for (int i=0;i<MAX_MINES;i++) if (g_mines[i].on && Vector3Distance(g_mines[i].pos,p)<r) MineSetOff(i,0.06f+GetRandomValue(0,10)/100.0f);
}

static void MinesReset(void){                         // a new run (main.c RunResetExtras): none laid from the last life, three in the pack,
    memset(g_mines,0,sizeof g_mines); g_layT=-1.0f;     // and the hand as at the start (the gun; the sandbox: a claymore)
    g_minesLeft=MINE_CARRY0;
    g_mineMode=g_mineStart; if (g_mineMode){ g_mineRaise=1.0f; g_mineModeT=0.0f; }
}

// ---- drawing the laid ones
#define MINE_MIN_LIT 1.00f                                            // the least light a laid one is shown in: 1 = as the one in the hand is drawn
#define MINE_SEEN 60.0f                                               // further off than this one is not drawn (a few px of screen)
static int MineUnseen(Vector3 c, float r, Vector3 fw, float half){   // 1: no part of a ball of radius r round c can be on screen -- too far off, or wholly outside the cone the view fits in
    Vector3 to=Vector3Subtract(c,g_cam.position); float d=Vector3Length(to);
    if (d>MINE_SEEN+r) return 1;
    if (d<=r+1.0f) return 0;                                          // (he is in it, or as good as)
    return acosf(fmaxf(-1.0f,fminf(1.0f,Vector3DotProduct(to,fw)/d))) > half+asinf(r/d);
}
// Its lights are drawn in one batch on one small texture, g_beamTex: the cross-section of a laser beam -- a hot core
// and a skirt of glow, 64 texels across, with mipmaps (three pixels of it are still its average).
//
// A beam is a ribbon from a to b that faces the eye, MINE_BEAM_W wide, glow and all, and never narrower on screen than
// MINE_BEAM_PX pixels. (It was a round rod 4.4 mm thick inside a faint one of 18: two metres off that is under a pixel,
// so the line broke into dashes, and nothing softened the staircase of what was drawn -- the owner's "kinda ragged, not
// clean lines", 2026-11-17. tests/claymore-look.sh)
#define MINE_BEAM_W  0.030f
#define MINE_BEAM_PX 3.4f
static Texture2D g_beamTex={0};
static void MineBeamTex(void){
    if (g_beamTex.id) return;
    Image im=GenImageColor(64,4,BLANK); Color *px=(Color*)im.data;
    for (int x=0;x<64;x++){ float u=((float)x+0.5f)/32.0f-1.0f, edge=(1.0f-u*u)*(1.0f-u*u);
        float a=fminf(1.0f,1.8f*expf(-u*u/(2.0f*0.16f*0.16f)))+0.20f*expf(-u*u/(2.0f*0.42f*0.42f)); a=fminf(1.0f,a)*edge;
        for (int y=0;y<4;y++) px[y*64+x]=(Color){255,255,255,(unsigned char)(255.0f*a+0.5f)}; }
    g_beamTex=LoadTextureFromImage(im); UnloadImage(im); GenTextureMipmaps(&g_beamTex);
    SetTextureFilter(g_beamTex,TEXTURE_FILTER_TRILINEAR); SetTextureWrap(g_beamTex,TEXTURE_WRAP_CLAMP);
}
static float MineDepth(Vector3 p, Vector3 fw){ return fmaxf(0.05f,Vector3DotProduct(Vector3Subtract(p,g_cam.position),fw)); }   // how far ahead of the eye
static void MineBeam(Vector3 a, Vector3 b, Color col, Vector3 fw, float mpp){   // mpp: the metres one pixel spans, a metre ahead
    Vector3 ab=Vector3Subtract(b,a), ca=Vector3Subtract(g_cam.position,a), cb=Vector3Subtract(g_cam.position,b);
    Vector3 sa=Vector3CrossProduct(ab,ca), sb=Vector3CrossProduct(ab,cb); float la=Vector3Length(sa), lb=Vector3Length(sb);
    if (la<1e-7f || lb<1e-7f) return;                                 // seen dead end on: nothing to show of it
    float wa=0.5f*fmaxf(MINE_BEAM_W,MINE_BEAM_PX*mpp*MineDepth(a,fw)), wb=0.5f*fmaxf(MINE_BEAM_W,MINE_BEAM_PX*mpp*MineDepth(b,fw));
    sa=Vector3Scale(sa,wa/la); sb=Vector3Scale(sb,wb/lb);
    Vector3 a0=Vector3Subtract(a,sa), a1=Vector3Add(a,sa), b0=Vector3Subtract(b,sb), b1=Vector3Add(b,sb);
    rlBegin(RL_TRIANGLES); rlColor4ub(col.r,col.g,col.b,col.a);
    rlTexCoord2f(0.0f,0.5f); rlVertex3f(a0.x,a0.y,a0.z); rlTexCoord2f(1.0f,0.5f); rlVertex3f(a1.x,a1.y,a1.z); rlTexCoord2f(1.0f,0.5f); rlVertex3f(b1.x,b1.y,b1.z);
    rlTexCoord2f(0.0f,0.5f); rlVertex3f(a0.x,a0.y,a0.z); rlTexCoord2f(1.0f,0.5f); rlVertex3f(b1.x,b1.y,b1.z); rlTexCoord2f(0.0f,0.5f); rlVertex3f(b0.x,b0.y,b0.z);
    rlEnd();
}
// A point of light: a disc facing the eye, where the nearest point of a ball of that radius would be, and never under
// `px` pixels across its radius (a lamp that is a fraction of a pixel flickers in and out as he moves). Drawn in one flat
// colour a ball IS its disc -- and raylib's DrawSphereEx spends 288 vertices and a sine or two on each of them to say
// so, five times a mine: most of what a laid claymore cost to draw. Ten sides, thirty vertices, no trigonometry.
static void MineDot(Vector3 c, float r, float px, Color col, Vector3 rt, Vector3 up, Vector3 fw, float mpp){
    static float cs[11], sn[11]; static int init=0;
    if (!init){ init=1; for (int k=0;k<=10;k++){ cs[k]=cosf((float)k*2.0f*PI/10.0f); sn[k]=sinf((float)k*2.0f*PI/10.0f); } }
    r=fmaxf(r,px*mpp*MineDepth(c,fw));
    Vector3 o=Vector3Subtract(c,Vector3Scale(fw,r));
    rlBegin(RL_TRIANGLES); rlColor4ub(col.r,col.g,col.b,col.a); rlTexCoord2f(0.5f,0.5f);   // (the beam texture's middle: full white)
    for (int k=0;k<10;k++){
        rlVertex3f(o.x,o.y,o.z);
        rlVertex3f(o.x+r*(cs[k]*rt.x+sn[k]*up.x), o.y+r*(cs[k]*rt.y+sn[k]*up.y), o.z+r*(cs[k]*rt.z+sn[k]*up.z));
        rlVertex3f(o.x+r*(cs[k+1]*rt.x+sn[k+1]*up.x), o.y+r*(cs[k+1]*rt.y+sn[k+1]*up.y), o.z+r*(cs[k+1]*rt.z+sn[k+1]*up.z)); }
    rlEnd();
}
static void MinesDraw(void){
    if (!g_minesOn) return;
    Vector3 fw=Vector3Normalize(Vector3Subtract(g_cam.target,g_cam.position));
    Vector3 rt=Vector3CrossProduct(fw,(Vector3){0,1,0}); if (Vector3LengthSqr(rt)<1e-6f) rt=(Vector3){1,0,0};   // the screen's own right and up
    rt=Vector3Normalize(rt); Vector3 up=Vector3CrossProduct(rt,fw);
    float asp=(float)GetScreenWidth()/fmaxf(1.0f,(float)GetScreenHeight());
    float half=atanf(tanf(g_cam.fovy*0.5f*DEG2RAD)*sqrtf(1.0f+asp*asp))+10.0f*DEG2RAD;   // out to the screen's corners, and ten degrees over
    int nLaid=0, nArmed=0, nModel=0, nLaser=0;
    static float minLit=-1.0f; if (minLit<0.0f){ const char *e=getenv("CHERNOBYL_MINE_FLOOR"); minLit=e?(float)atof(e):MINE_MIN_LIT; }   // dev: CHERNOBYL_MINE_FLOOR=0 -- as the lamps alone leave it (tests/claymore-look.sh)
    for (int i=0;i<MAX_MINES;i++){ Mine *m=&g_mines[i]; if (!m->on) continue; nLaid++;
        Vector3 p=m->pos; Quaternion q=m->q1;
        if (m->state==MS_SET){ float x=fminf(1.0f,m->t/MINE_SET_T), e=1.0f-(1.0f-x)*(1.0f-x); p=Vector3Lerp(m->p0,m->pos,e); q=QuaternionSlerp(m->q0,m->q1,e); }
        if (MineUnseen(p,0.3f,fw,half)) continue;                     // not drawn, and not lit (finding its lamps costs a ray each)
        nModel++;
        Matrix W=MatrixMultiply(QuaternionToMatrix(q),MatrixTranslate(p.x,p.y,p.z));
        if (g_mineWOk){ static const float zero[4]={0,0,0,0};
            EnemyLightUpload(m->pos); EnemyGlowDim(0.0f);           // its own lamps: the shader kept those of whatever was drawn last (a cook, a trap across the hall)
            EnemyMinLit(minLit);                                    // ...and never less than this: where no lamp reaches it was the floor's own black, and could not be found again
            rlSetVertexAttributeDefault(RL_DEFAULT_SHADER_ATTRIB_LOCATION_BONEWEIGHTS,zero,SHADER_ATTRIB_VEC4,4); rlSetVertexAttributeDefault(RL_DEFAULT_SHADER_ATTRIB_LOCATION_BONEINDICES,zero,SHADER_ATTRIB_VEC4,4);
            g_mineW.transform=W; DrawModel(g_mineW,(Vector3){0,0,0},1.0f,WHITE); }
        else { rlPushMatrix(); rlMultMatrixf(MatrixToFloat(W)); DrawCube((Vector3){0,0.087f,0},0.216f,0.083f,0.035f,(Color){70,76,46,255}); rlPopMatrix(); }
    }
    EnemyMinLit(0.0f);                                                // (the shader is the enemies' and the traps' too)
    BeginBlendMode(BLEND_ADDITIVE); rlDisableDepthMask(); rlDisableBackfaceCulling();   // the lights, and the fan of laser over what each covers:
    MineBeamTex(); rlSetTexture(g_beamTex.id);                        //  one batch, one texture (a dot takes the beam's bright middle)
    float mpp=2.0f*tanf(g_cam.fovy*0.5f*DEG2RAD)/fmaxf(1.0f,(float)GetScreenHeight());
    for (int i=0;i<MAX_MINES;i++){ Mine *m=&g_mines[i]; if (!m->on || m->state==MS_SET) continue;
        Vector3 s=MineSight(m), led={ s.x, s.y+0.006f, s.z }, f=MineFwd(m); float t=(float)GetTime()+i*0.37f;
        int fan=(m->state==MS_ARMED || m->state==MS_FIRING); nArmed+=fan;
        int on=(m->state==MS_ARMING)?(fmodf(t,0.25f)<0.10f):(m->state==MS_FIRING)?1:(fmodf(t,1.0f)<0.14f);
        Color lc=(m->state==MS_ARMING)?(Color){60,255,90,255}:(Color){255,40,30,255};
        if (on && !MineUnseen(s,1.7f,fw,half)){ MineDot(led,0.0065f,1.2f,lc,rt,up,fw,mpp); MineDot(led,0.018f,2.8f,Fade(lc,0.25f),rt,up,fw,mpp);   // (1.7 m: as far as its light falls)
                 DynLightAdd(led,(m->state==MS_ARMING)?(Vector3){0.1f,0.6f,0.15f}:(Vector3){0.7f,0.08f,0.05f},1.6f,0.06f,1); }
        if (!fan || MineUnseen((Vector3){s.x+f.x*MINE_RANGE*0.5f,s.y,s.z+f.z*MINE_RANGE*0.5f},MINE_RANGE*0.6f,fw,half)) continue;   // (the ball its whole fan lies in)
        nLaser++;
        float fl=0.75f+0.25f*sinf(t*37.0f)*sinf(t*13.0f);
        for (int b=-1;b<=1;b++){ float yw=m->yaw+b*24.0f*DEG2RAD; Vector3 d=Vector3Normalize((Vector3){sinf(yw),-0.035f,cosf(yw)});
            if (!m->beamOk){ float L=MapRayNearest(s,d,MINE_RANGE); m->beam[b+1]=(L>0.0f)?L:MINE_RANGE; if (b==1) m->beamOk=1; }   // where each stops: found the first time it is drawn, not a ray a beam a frame
            Vector3 e=Vector3Add(s,Vector3Scale(d,m->beam[b+1]));
            MineBeam(s,e,Fade((Color){255,34,22,255},fl),fw,mpp);
            MineDot(e,0.012f,1.6f,Fade((Color){255,40,30,255},0.6f*fl),rt,up,fw,mpp); }
    }
    rlDrawRenderBatchActive(); rlSetTexture(0); rlEnableBackfaceCulling(); rlEnableDepthMask(); EndBlendMode();   // (drawn BEFORE the depth mask comes back: left to EndBlendMode's own flush, the lights went into the depth buffer after all)
    if (g_debug && nLaid>0){ static double next=0.0; double now=GetTime();   // what was drawn of what is laid, once a second (tests/claymore-lean.sh)
        if (now>=next){ next=now+1.0; DebugLog("minedraw","\"laid\":%d,\"armed\":%d,\"models\":%d,\"lasers\":%d",nLaid,nArmed,nModel,nLaser); } }
}
static void MinesHud(int W, int H){
    if (!g_minesOn || !g_mineMode) return;
    int armed=0, laid=0; for (int i=0;i<MAX_MINES;i++) if (g_mines[i].on){ laid++; if (g_mines[i].state==MS_ARMED) armed++; }
    const char *lb="Claymore";                                         // where the guns show theirs: name, count, a pip for each carried
    DrawText(lb, W-28-MeasureText(lb,16), H-72, 16, (Color){205,205,215,210});
    if (g_mineStart){ const char *n=TextFormat("%d laid  %d armed", laid, armed); DrawText(n, W-28-MeasureText(n,24), H-50, 24, RAYWHITE); }
    else {
        const char *at=TextFormat("%d / %d", g_minesLeft, MINE_CARRY); int fs=30;
        DrawText(at, W-28-MeasureText(at,fs), H-50, fs, g_minesLeft==0?(Color){255,90,60,255}:RAYWHITE);
        int pw=22, ph=12, gap=6, x0=W-28-MINE_CARRY*(pw+gap)+gap, y0=H-96;     // the claymores themselves: olive, bowed, empties dim
        for (int k=0;k<MINE_CARRY;k++){ int x=x0+k*(pw+gap), full=(k<g_minesLeft);
            DrawRectangle(x+1,y0+2,pw,ph,(Color){0,0,0,120});
            DrawRectangleRounded((Rectangle){(float)x,(float)y0,(float)pw,(float)ph},0.45f,4,full?(Color){92,104,58,255}:(Color){50,54,40,140});
            DrawRectangle(x+3,y0+ph,2,4,full?(Color){60,64,44,255}:(Color){50,54,40,120}); DrawRectangle(x+pw-5,y0+ph,2,4,full?(Color){60,64,44,255}:(Color){50,54,40,120}); }
        if (laid>0){ const char *n=TextFormat("%d laid  %d armed", laid, armed); DrawText(n, W-28-MeasureText(n,16), H-18, 16, (Color){205,205,215,200}); } }
    static int clean=-1; if (clean<0) clean=getenv("CHERNOBYL_CLEAN_HUD")?1:0;
#if defined(C2_IOS)
    const char *h="FIRE  lays it, facing the way you look";
#else
    const char *h=g_mineStart ? "LEFT CLICK  lay a claymore      1-4  the guns      5  the claymore" : "LEFT CLICK  lays it, facing the way you look";
#endif
    float hold=g_mineStart?10.0f:4.0f;
    if (!clean && g_mineModeT<hold) DrawText(h,(W-MeasureText(h,18))/2,H-110,18,Fade(RAYWHITE,fminf(1.0f,(hold-g_mineModeT)*0.5f)));
}
