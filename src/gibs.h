// Gibs: a body caught at the heart of a blast, or by a shotgun at arm's length, comes apart -- chunks of meat and bone
// thrown out, bouncing and leaving blood where they land. No corpse.
//
// What is thrown (owner, 2026-11-18: "i saw little flesh pieces on the floor. they are simple object. can you increase
// their quality to be real chunks"). They were flat-coloured balls (DrawSphereEx: no light, no picture). They are eight
// shapes now -- lumps of muscle, torn flaps, a long bone, a joint end, a piece of rib -- built by
// tools/gibs/build_chunks.py into assets/gibs/chunks.gltf (colour in the vertices; lying on y = 0, reaching 1 from its
// own middle) and drawn through the enemies' lit shader with three small pictures (grain, bumps, wetness): the room's
// lamps, wet highlights, the red bleed of light through flesh, the fog. Each tumbles as it flies and is level again
// whenever it touches down, so it comes to rest lying as it was modelled -- on the blood it lands in, and on whatever
// floor is under it there (down a breach, off a deck's edge, up on a deck). Without the files or the shader: the
// balls, as before. tests/gibs.sh.
typedef struct { Vector3 pos, vel, ax, from; float life, floorY, size, ang, spin, yaw, age; int bounces, kind, burst, said, fell; Color col; } Gib;
// (from: where its burst began, on the floor he stood on. fell: down breach fell-1, or -1 off a ledge. said: what the debug log has been told of it)
#define MAX_GIBS 96
#define GIB_KINDS 8                                 // lump1 lump2 lump3 flap1 flap2 bone1 bone2 rib1 (the file's order)
#define GIB_N 13                                    // thrown for one man
#define GIB_G 16.0f                                 // how hard they fall
#define GIB_LIFT 0.015f                             // a chunk lies ON the blood, not under it: a pool is a film drawn 7 to 14 mm above the floor (StainAddPool),
                                                    // and a chunk standing on the floor itself lost everything below that -- a flap is not 2 cm thick.
                                                    // (The spatter's dots, 16 to 23 mm up, can still lie across a chunk's foot: they are small.)
// What one man leaves: which shape, and how big (its reach from its own middle, metres, for an enemy of scale 1). Two
// large (a long bone, a length of rib), five medium, six small: 0.085 m on average (the balls were 0.075).
static const struct { int kind; float lo, hi; } kGibSet[GIB_N]={
    {5,0.130f,0.160f}, {7,0.120f,0.150f},
    {6,0.095f,0.120f}, {7,0.085f,0.105f}, {0,0.085f,0.110f}, {1,0.080f,0.105f}, {2,0.080f,0.100f},
    {0,0.045f,0.065f}, {1,0.045f,0.065f}, {2,0.045f,0.062f}, {3,0.055f,0.068f}, {4,0.050f,0.068f}, {3,0.045f,0.060f} };
static Gib g_gibs[MAX_GIBS]; static int g_gibNext=0;
static Model g_gibM; static int g_gibOk=0, g_gibTried=0, g_gibTex=0, g_gibBurst=0;
static int g_gibSay=0, g_gibFlew=0;                // debug log: frames until it is told how the last burst is drawn; the last burst whose flight it was told of
static int g_gibFrame=0, g_gibDry=-1, g_gibKey=-1, g_gibDark=-1;   // dev (tests/gibs.sh): CHERNOBYL_GIB_DRY / _KEY / _DARK=<frame> -- from that frame on there is no
#define GIB_DEV(v) ((v)>=0 && g_gibFrame>=(v))                       // blood (what lies on the floor is wiped, none is laid) / each chunk is drawn flat green (what
                                                                     // is seen of it can be counted) / the room's lamps are left out of their light
static float g_gibMid[GIB_KINDS];                   // each shape's middle, above the floor it lies on (in its own unit)
static void GibsLoad(void){                         // with the guns (LoadGameAssets): the mine's way (mines.h MinesLoadWorld)
    g_gibTried=1; const char *path="assets/gibs/chunks.gltf";
    static const char *pic[3]={"assets/gibs/meat_c.png","assets/gibs/meat_n.png","assets/gibs/meat_orm.png"}; static const int map[3]={MATERIAL_MAP_DIFFUSE,MATERIAL_MAP_NORMAL,MATERIAL_MAP_OCCLUSION};
    if (FileExists(path) && FileExists(pic[0]) && FileExists(pic[1]) && FileExists(pic[2]) && EnemySkinInit()==1){
        SetTraceLogLevel(LOG_ERROR); Model m=LoadModel(path); SetTraceLogLevel(LOG_WARNING);      // (the file names no picture: raylib reads its geometry alone)
        if (m.meshCount==GIB_KINDS){
            Image e=GenImageColor(1,1,(Color){0,0,0,255}); Texture2D noEmi=LoadTextureFromImage(e); UnloadImage(e);
            for (int q=0;q<3;q++){ Texture2D t=LoadTexture(pic[q]); if (t.id<=0) continue;
                GenTextureMipmaps(&t); SetTextureFilter(t,TEXTURE_FILTER_TRILINEAR); SetTextureWrap(t,TEXTURE_WRAP_REPEAT); g_gibTex++;
                for (int i=0;i<m.materialCount;i++) m.materials[i].maps[map[q]].texture=t; }
            for (int i=0;i<m.materialCount;i++){ m.materials[i].shader=g_skinShader; m.materials[i].maps[MATERIAL_MAP_EMISSION].texture=noEmi; }
            for (int i=0;i<m.meshCount;i++){ Mesh *ms=&m.meshes[i]; if (!ms->tangents && ms->texcoords && ms->normals) GenMeshTangents(ms);
                BoundingBox b=GetMeshBoundingBox(*ms); g_gibMid[i]=(b.min.y+b.max.y)*0.5f; }
            if (g_gibTex==3){ g_gibM=m; g_gibOk=1; }
        }
    }
    DebugLog("gibs","\"meshes\":%d,\"tex\":%d,\"lit\":%d", g_gibOk?g_gibM.meshCount:0, g_gibTex, g_gibOk);
}
static float GibRest(const Gib *g){ return g_gibOk ? g->floorY+GIB_LIFT+g->size*g_gibMid[g->kind] : g->floorY+g->size*0.6f; }   // how high its middle is when it lies on the floor
static Matrix GibMatrix(const Gib *g){              // a chunk as it is drawn: about its own middle -- its size (over its last second it shrinks away, on the floor:
    float s=g->size*fminf(1.0f,g->life), mid=g_gibMid[g->kind];                         // the lit shader does not fade), its heading, its tumble, where it is
    Matrix M=MatrixTranslate(0.0f,-mid,0.0f); M=MatrixMultiply(M,MatrixScale(s,s,s)); M=MatrixMultiply(M,MatrixRotateY(g->yaw));
    if (g->ang!=0.0f) M=MatrixMultiply(M,MatrixRotate(g->ax,g->ang));
    return MatrixMultiply(M,MatrixTranslate(g->pos.x,g->pos.y-(g->size-s)*mid,g->pos.z));
}
static float GibLow(const Gib *g){                  // the lowest point of a chunk AS DRAWN -- its mesh through the matrix it is drawn with -- above its floor
    Matrix M=GibMatrix(g); const Mesh *ms=&g_gibM.meshes[g->kind]; float low=1e9f;
    for (int v=0;v<ms->vertexCount;v++){ float y=M.m1*ms->vertices[v*3]+M.m5*ms->vertices[v*3+1]+M.m9*ms->vertices[v*3+2]+M.m13; if (y<low) low=y; }
    return low-g->floorY;
}
static float GibAir(const Gib *g){                  // how long until its middle is back at the height it rests at
    float h=fmaxf(g->pos.y-GibRest(g),0.0f), vy=g->vel.y; return (vy+sqrtf(vy*vy+2.0f*GIB_G*h))/GIB_G;
}
// The hop it is starting: how long until its middle is back at the height it rests at follows from how high it is and how
// fast it is rising, and it is spun so as to have turned whole turns by then -- level again as it touches down, whatever
// it did in the air. (A wall stops its travel, not its fall, so the time holds.)
static void GibHop(Gib *g){
    float t=GibAir(g);
    int turns=(t<0.22f)?0:(t<0.55f)?1:GetRandomValue(1,3);         // (a hop too short to turn over in is not turned)
    g->ang=0.0f; g->spin=(t>1e-3f)?turns*2.0f*PI/t:0.0f; if (GetRandomValue(0,1)) g->spin=-g->spin;
    Vector3 hz={g->vel.x,0,g->vel.z}; float hl=Vector3Length(hz);                    // head over heels along the way it goes, more or less
    Vector3 ax=(hl>0.2f)?(Vector3){hz.z/hl,0,-hz.x/hl}:(Vector3){1,0,0};
    ax.x+=GetRandomValue(-45,45)/100.0f; ax.y+=GetRandomValue(-35,35)/100.0f; ax.z+=GetRandomValue(-45,45)/100.0f; g->ax=Vector3Normalize(ax);
}
static void GibHopOn(Gib *g){                       // ...and one begun part-way through a turn (a bounce a frame late, a floor that was not there) takes what is left of it up
    float left=fmodf(g->ang,2.0f*PI); if (left>PI) left-=2.0f*PI; if (left<-PI) left+=2.0f*PI;
    GibHop(g); float t=GibAir(g); g->ang=left; if (t>0.05f) g->spin-=left/t; else g->spin=0.0f;
}
static void SpawnGibs(Vector3 c, Vector3 away, float floorY, float scale){
    if (!g_gibTried) GibsLoad();
    static float lifeDev=-1.0f; if (lifeDev<0.0f){ const char *e=getenv("CHERNOBYL_GIB_LIFE"); lifeDev=e?(float)atof(e):0.0f; }   // dev (tests/gibs.sh): how long they stay, s
    unsigned seen=0; g_gibBurst++;
    for (int k=0;k<GIB_N;k++){
        Gib g={0}; float a=GetRandomValue(0,628)/100.0f, sp=2.0f+GetRandomValue(0,450)/100.0f;
        g.pos=(Vector3){c.x+cosf(a)*0.15f, c.y+GetRandomValue(-40,50)/100.0f*scale, c.z+sinf(a)*0.15f};
        g.vel=(Vector3){cosf(a)*sp+away.x*2.6f, 2.5f+GetRandomValue(0,450)/100.0f, sinf(a)*sp+away.z*2.6f};
        g.floorY=floorY; g.burst=g_gibBurst; g.yaw=GetRandomValue(0,628)/100.0f; g.from=(Vector3){c.x,floorY,c.z};
        if (g_gibOk){ g.kind=kGibSet[k].kind; g.size=(kGibSet[k].lo+(kGibSet[k].hi-kGibSet[k].lo)*GetRandomValue(0,100)/100.0f)*scale; g.life=20.0f+GetRandomValue(0,800)/100.0f; }
        else { g.kind=k%GIB_KINDS; g.size=(0.045f+GetRandomValue(0,60)/1000.0f)*scale; g.life=7.0f+GetRandomValue(0,300)/100.0f; }   // (the balls, as they were)
        g.col=(k%4==3) ? (Color){170,160,140,255} : (Color){(unsigned char)(90+GetRandomValue(0,40)),12,10,255};       // (the balls: bone among the meat)
        if (lifeDev>0.0f) g.life=lifeDev;
        seen|=1u<<g.kind; if (g.pos.y<GibRest(&g)) g.pos.y=GibRest(&g);
        GibHop(&g);
        g_gibs[g_gibNext]=g; g_gibNext=(g_gibNext+1)%MAX_GIBS;
    }
    if (!GIB_DEV(g_gibDry)) for (int k=0;k<3;k++) SpawnBlood(c,(Vector3){away.x*0.5f+GetRandomValue(-60,60)/100.0f, 0.5f, away.z*0.5f+GetRandomValue(-60,60)/100.0f}, 2.2f, floorY);
    { int kinds=0; for (int k=0;k<GIB_KINDS;k++) kinds+=(seen>>k)&1; g_gibSay=300; DebugLog("gibburst","\"n\":%d,\"kinds\":%d", GIB_N, kinds); }
}
static float GibTilt(const Gib *g){                 // degrees its own "up" stands off the world's
    float c=cosf(g->ang); return acosf(fminf(1.0f,fmaxf(-1.0f, c+(1.0f-c)*g->ax.y*g->ax.y)))*RAD2DEG;
}
static void GibsUpdate(float dt){
    // dev (tests/gibs.sh; --gibs / --gib-row for the browser build, which has no environment):
    //   CHERNOBYL_GIBS=<frame>[*<n>]        a burst thrown 2.2 m in front of him at that frame (and n-1 more, 12 frames apart)
    //   CHERNOBYL_GIB_ROW=<frame>[*<size>]  the eight shapes in a row 2.3 m in front of him, let fall from 4 cm so that each lands as a
    //                                       thrown one does (its wet mark, the floor under it); 0.14 m each, or <size>, or (*0) each at the
    //                                       smallest it is ever thrown
    { static int at=-2, row=-2, more=1; static float rowSize=0.14f; int uf=g_gibFrame;
      if (at==-2){ const char *e=g_gibsArg?g_gibsArg:getenv("CHERNOBYL_GIBS"); at=e?atoi(e):-1; if (e && strchr(e,'*')) more=atoi(strchr(e,'*')+1);
          e=g_gibRowArg?g_gibRowArg:getenv("CHERNOBYL_GIB_ROW"); row=e?atoi(e):-1; if (e && strchr(e,'*')) rowSize=(float)atof(strchr(e,'*')+1);
          e=getenv("CHERNOBYL_GIB_DRY"); if (e && *e) g_gibDry=atoi(e);
          e=getenv("CHERNOBYL_GIB_KEY"); if (e && *e) g_gibKey=atoi(e);
          e=getenv("CHERNOBYL_GIB_DARK"); if (e && *e) g_gibDark=atoi(e); }
      if (g_gibDry>=0 && uf==g_gibDry) StainReset();
      float a=1.5708f-g_yaw, feet=g_pos.y-EYE_H, fx=cosf(a), fz=sinf(a);
      if (at>=0 && uf>=at && (uf-at)%12==0 && (uf-at)/12<more) SpawnGibs((Vector3){g_pos.x+fx*2.2f, feet+1.0f, g_pos.z+fz*2.2f},(Vector3){0,0,0},feet,1.0f);
      if (row>=0 && uf==row){ if (!g_gibTried) GibsLoad(); g_gibBurst++; g_gibSay=150;
          for (int k=0;k<GIB_KINDS;k++){ Gib g={0}; float o=(k-3.5f)*0.40f, least=1.0f;
              for (int q=0;q<GIB_N;q++) if (kGibSet[q].kind==k && kGibSet[q].lo<least) least=kGibSet[q].lo;
              g.kind=k; g.size=(rowSize>0.0f)?rowSize:least; g.floorY=feet; g.life=60.0f; g.bounces=2; g.burst=g_gibBurst; g.yaw=0.6f+k*0.9f; g.ax=(Vector3){1,0,0};
              g.from=(Vector3){g_pos.x+fx*2.3f, feet, g_pos.z+fz*2.3f}; g.col=(k%4==3) ? (Color){170,160,140,255} : (Color){110,12,10,255};
              g.pos=(Vector3){g_pos.x+fx*2.3f-fz*o, 0, g_pos.z+fz*2.3f+fx*o}; g.pos.y=GibRest(&g)+0.04f; g.vel=(Vector3){0,-1.0f,0};
              g_gibs[g_gibNext]=g; g_gibNext=(g_gibNext+1)%MAX_GIBS; } }
      g_gibFrame++; }
    for (int i=0;i<MAX_GIBS;i++){ Gib *g=&g_gibs[i]; if (g->life<=0) continue;
        g->life-=dt; g->age+=dt;
        if (g->life<=0){ DebugLog("gibgone","\"i\":%d,\"age\":%.1f", i, g->age); continue; }
        if (g->bounces>=3){                                        // lying there: whatever turn it had left eases out
            if (g->ang!=0.0f){ g->ang*=expf(-dt*20.0f); if (fabsf(g->ang)<0.002f) g->ang=0.0f; }
            if (g_debug && !(g->said&1) && g->ang==0.0f){ g->said|=1;   // the debug log is told how each lies AS DRAWN (GibLow), and where in the picture, once it is at rest...
                Vector2 sp=GetWorldToScreen(g->pos,g_cam);
                DebugLog("gibrest","\"i\":%d,\"kind\":%d,\"size\":%.3f,\"tilt\":%.2f,\"low\":%.4f,\"at\":[%.2f,%.2f],\"floor\":%.2f,\"fell\":%d,\"sx\":%.4f,\"sy\":%.4f,\"life\":%.1f", i, g->kind, g->size, GibTilt(g),
                         g_gibOk?GibLow(g):0.0f, g->pos.x, g->pos.z, g->floorY, g->fell, sp.x/(float)GetScreenWidth(), sp.y/(float)GetScreenHeight(), g->life+g->age); }
            if (g_debug && g_gibOk && !(g->said&2) && g->life<0.5f){ g->said|=2;   // ...and once more half gone (how much is left of it, and that it is still on the floor)
                DebugLog("gibshrink","\"i\":%d,\"k\":%.2f,\"low\":%.4f", i, g->life, GibLow(g)); }
            continue; }
        if (g->age>=0.25f && g->burst!=g_gibFlew && g->spin!=0.0f){ g_gibFlew=g->burst; DebugLog("gibfly","\"turned\":%.0f", fabsf(g->ang)*RAD2DEG); }   // (once a burst)
        g->vel.y-=GIB_G*dt; g->ang+=g->spin*dt;
        Vector3 st=Vector3Scale(g->vel,dt); float sl=Vector3Length(st);
        if (g_hasMap && g_mapCol.ready && sl>1e-5f){              // walls stop them
            float w=MapRayNearest(g->pos,Vector3Scale(st,1.0f/sl),sl+g->size);
            if (w>0.0f && fabsf(st.y)<sl*0.9f){ g->vel.x*=-0.3f; g->vel.z*=-0.3f; st=(Vector3){0,st.y,0}; }
        }
        g->pos=Vector3Add(g->pos,st);
        if (g->fell>0 && g->fell<=g_nMapHoles){ const MapHole *H=&g_mapHoles[g->fell-1];    // down a breach: the shaft's sides keep it (a steep fall is not tested against walls above)
            float mx=fmaxf(0.05f,H->hx-g->size), mz=fmaxf(0.05f,H->hz-g->size);
            if (fabsf(g->pos.x-H->pos.x)>mx){ g->pos.x=H->pos.x+(g->pos.x<H->pos.x?-mx:mx); g->vel.x*=-0.3f; }
            if (fabsf(g->pos.z-H->pos.z)>mz){ g->pos.z=H->pos.z+(g->pos.z<H->pos.z?-mz:mz); g->vel.z*=-0.3f; } }
        float rest=GibRest(g);
        if (g->pos.y<=rest && g->vel.y<0){
            // Is the floor he stood on under it here? Not over a breach (the piece goes down the shaft and lies at the
            // bottom), nor past the edge of a deck or down a stair (it falls on to the floor below; a step up, it lands on
            // that). One look a touch-down: a breach by the map's own list, anything else by a ray down the map. Left
            // lying at his floor's height it hung in the air over the breach on a disc of blood, for twenty seconds.
            if (g_gibOk){ float fy=g->floorY; int h=HoleAt(g->pos.x,g->pos.z,0.0f);
                if (h>=0) fy=fminf(fy, g_mapHoles[h].pos.y-g_mapHoles[h].depth);
                else if (g_hasMap && g_mapCol.ready){ float d=MapRayNearest((Vector3){g->pos.x,g->floorY+0.8f,g->pos.z},(Vector3){0,-1,0},12.0f); if (d>0.0f) fy=g->floorY+0.8f-d; }
                if (fy<g->floorY-0.06f){ g->floorY=fy; g->fell=(h>=0)?h+1:-1; GibHopOn(g); DebugLog("gibfell","\"i\":%d,\"hole\":%d,\"to\":%.2f", i, h, fy); continue; }
                if (fy>g->floorY+0.06f){ g->floorY=fy; rest=GibRest(g); } }
            g->pos.y=rest; g->bounces++;
            if (GIB_DEV(g_gibDry)) { }
            else if (g_gibOk){ Color pc={ (unsigned char)(105+GetRandomValue(0,35)), (unsigned char)GetRandomValue(0,12), (unsigned char)GetRandomValue(0,8), 255 };   // a wet mark where it lands, the
                StainAddPool(g->pos.x, g->floorY, g->pos.z, fminf(0.20f,fmaxf(0.06f,g->size*1.5f)), (unsigned int)GetRandomValue(1,1<<30), pc); }                // size of what landed (the balls
            else SpawnBlood((Vector3){g->pos.x,g->floorY+g->size*0.6f,g->pos.z},(Vector3){0,0.2f,0},0.05f,g->floorY);                                        // each left a pool 0.2 m across)
            if (g->bounces>=3 || fabsf(g->vel.y)<1.2f){ g->bounces=3; g->vel=(Vector3){0,0,0}; g->spin=0.0f;
                float turn=fmodf(g->ang,2.0f*PI); if (turn>PI) turn-=2.0f*PI; if (turn<-PI) turn+=2.0f*PI; g->ang=turn; }    // what is left of its turn (a frame's worth) eases out as it lies
            else { g->vel.y*=-0.30f; g->vel.x*=0.5f; g->vel.z*=0.5f; GibHopOn(g); }                                           // ...or is taken up in the next hop
        }
    }
}
static void DrawGibs(void){
    int n=0; for (int i=0;i<MAX_GIBS;i++) n+=(g_gibs[i].life>0); if (n==0) return;
    int say=(g_gibSay>0 && --g_gibSay==0);
    if (!g_gibOk){                                                 // the balls
        for (int i=0;i<MAX_GIBS;i++){ Gib *g=&g_gibs[i]; if (g->life<=0) continue;
            Color c=g->col; if (g->life<1.0f) c.a=(unsigned char)(255*g->life);
            DrawSphereEx(g->pos, g->size, 4, 6, c); }
        if (say) DebugLog("gibdraw","\"n\":%d,\"lamps\":0,\"lit\":0", n);
        return; }
    static const float zero[4]={0,0,0,0}; int lamps=0, drawn=0, done[MAX_GIBS/GIB_N+2], nd=0, key=GIB_DEV(g_gibKey), dark=GIB_DEV(g_gibDark); float ly=0.0f;
    Vector3 fw=Vector3Normalize(Vector3Subtract(g_cam.target,g_cam.position));
    rlDrawRenderBatchActive(); EnemyGlowDim(0.0f); EnemyMinLit(0.0f);
    rlSetVertexAttributeDefault(RL_DEFAULT_SHADER_ATTRIB_LOCATION_BONEWEIGHTS,zero,SHADER_ATTRIB_VEC4,4); rlSetVertexAttributeDefault(RL_DEFAULT_SHADER_ATTRIB_LOCATION_BONEINDICES,zero,SHADER_ATTRIB_VEC4,4);
    // A burst at a time. The lamps: finding them costs a ray each, so they are looked up once for the burst, where it
    // began -- where the man stood, on his floor: open air, in the middle of what he left, and it stays put while chunks
    // fall down a breach or are used up (the shader lights each pixel by its own distance from the lamps). A chunk
    // thrown more than 5 m clear of that is drawn after the rest, with a lookup of its own.
    for (int i=0;i<MAX_GIBS;i++){ if (g_gibs[i].life<=0) continue;
        int cur=g_gibs[i].burst, was=0, lit=0; for (int q=0;q<nd;q++) was|=(done[q]==cur);
        if (was) continue;
        if (nd<(int)(sizeof done/sizeof done[0])) done[nd++]=cur;
        Vector3 from=g_gibs[i].from;
        for (int pass=0;pass<2;pass++) for (int j=0;j<MAX_GIBS;j++){ Gib *g=&g_gibs[j]; if (g->life<=0 || g->burst!=cur) continue;
            int far=((g->pos.x-from.x)*(g->pos.x-from.x)+(g->pos.z-from.z)*(g->pos.z-from.z)>5.0f*5.0f); if (far!=pass) continue;
            { Vector3 to=Vector3Subtract(g->pos,g_cam.position); float d=Vector3Length(to);        // far off, or well outside the view: not drawn, and not lit (as the traps)
              if (d>60.0f || (d>4.0f && Vector3DotProduct(to,fw)<d*0.25f)) continue; }
            if (key){ const Mesh *ms=&g_gibM.meshes[g->kind]; Matrix M=GibMatrix(g);                // dev: the same triangles, the same place, flat green
                rlBegin(RL_TRIANGLES); rlColor4ub(0,255,0,255);
                for (int t=0;t<ms->triangleCount*3;t++){ int v=ms->indices?ms->indices[t]:t; Vector3 q=Vector3Transform((Vector3){ms->vertices[v*3],ms->vertices[v*3+1],ms->vertices[v*3+2]},M); rlVertex3f(q.x,q.y,q.z); }
                rlEnd(); drawn++; continue; }
            if (pass==1 || !lit){ Vector3 at=pass?(Vector3){g->pos.x,g->floorY,g->pos.z}:from; if (!pass) ly=at.y;
                if (dark) at.y-=1000.0f;                           // (dev: no lamp reaches down there)
                EnemyLightUpload(at); lamps++; lit=1; }
            DrawMesh(g_gibM.meshes[g->kind], g_gibM.materials[g_gibM.meshMaterial[g->kind]], GibMatrix(g)); drawn++;
        }
    }
    if (key) rlDrawRenderBatchActive();                            // (before the blood is drawn, as the chunks themselves are)
    if (say) DebugLog("gibdraw","\"n\":%d,\"drawn\":%d,\"lamps\":%d,\"ly\":%.2f,\"lit\":1", n, drawn, lamps, ly);
}
