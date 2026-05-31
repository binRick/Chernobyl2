// CHERNOBYL 2 - a more-3D successor to the sprite-based Chernobyl FPS.
//
// Player runs around a real 3D room with an animated first-person rifle
// (assets/rifle.glb - an M16A3 arms+gun rig: 89-bone, skinning verified, fits
// the GPU limit). World geometry is generated procedurally in code; the rifle
// is the only loaded art asset. Built on raylib 6.0.
//
// Conventions carried over from Chernobyl:
//   - run.sh builds + runs.
//   - --debug emits newline-delimited JSON events to stderr.
//   - ASCII-only strings in DrawText (raylib's default font is ASCII-only).
//
// Controls: WASD move - MOUSE look - SHIFT sprint - SPACE jump - LMB fire -
//           R reload - ` or TAB = dev overlay - ESC quit.
// Viewmodel tuning (live): I/K=Y J/L=X U/O=Z  -/= scale  [/] yaw  ;/' pitch
//                          V = inspect (gun floats in world)  0 = reset
// ----------------------------------------------------------------------------

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
  #include <OpenGL/gl3.h>          // glClear(GL_DEPTH_BUFFER_BIT) for the viewmodel pass
#elif defined(__EMSCRIPTEN__)
  #include <GLES3/gl3.h>
#endif

// raylib 6.0 redesigned skeletal animation; macro keeps it buildable on 5.5.
#if defined(RAYLIB_VERSION_MAJOR) && RAYLIB_VERSION_MAJOR >= 6
  #define ANIM_FRAMES(a)     ((a).keyframeCount)
  #define ANIM_APPLY(m,a,f)  UpdateModelAnimation((m),(a),(float)(f))
#else
  #define ANIM_FRAMES(a)     ((a).frameCount)
  #define ANIM_APPLY(m,a,f)  UpdateModelAnimation((m),(a),(int)(f))
#endif

// ---- Debug JSON logging -----------------------------------------------------
static int   g_debug     = 0;
static FILE *g_dbg       = NULL;
static int   g_maxFrames = 0;     // --frames N : timed run, then quit
static int   g_noEnemies = 0;     // --no-enemies / N key : empty arena for weapon tuning
static int   g_shotFrame = 0;     // --shot N : screenshot at frame N (0 = off)

static void DebugLog(const char *ev, const char *fmt, ...) {
    if (!g_debug || !g_dbg) return;
    fprintf(g_dbg, "{\"t\":%.3f,\"ev\":\"%s\"", GetTime(), ev);
    if (fmt && fmt[0]) { va_list ap; va_start(ap, fmt); fputc(',', g_dbg); vfprintf(g_dbg, fmt, ap); va_end(ap); }
    fprintf(g_dbg, "}\n"); fflush(g_dbg);
}
static const char *JStr(const char *s){
    static char b[96]; size_t j=0; if(!s)s="";
    for(size_t i=0;s[i]&&j<sizeof(b)-2;i++){char c=s[i]; if(c=='"'||c=='\\'){b[j++]='\\';b[j++]=c;} else if((unsigned char)c>=0x20)b[j++]=c;}
    b[j]=0; return b;
}

// ---- World ------------------------------------------------------------------
#define ARENA   28.0f
#define WALL_H  6.0f
#define EYE_H   1.7f

typedef struct { Vector3 pos; Vector3 size; Color col; } Box;
#define NUM_CRATES 7
static Box  g_crates[NUM_CRATES];
static Texture2D g_floorTex, g_wallTex, g_crateTex;
static Model g_floor, g_wall, g_crate;

// ---- Player -----------------------------------------------------------------
static Vector3 g_pos   = { 0, EYE_H, 6 };
static float   g_yaw   = 0.0f;
static float   g_pitch = 0.0f;
static float   g_vy    = 0.0f;
static int     g_grounded = 1;
static int     g_devOverlay = 1;
static float   g_bob = 0.0f;

// ---- Weapon: animated M16A3 viewmodel (assets/rifle.glb) --------------------
// File animations: Take(51) Shoot(26) Reload(338) Watch(301) Hide(51). There's
// NO short idle clip - "Watch" is a 10s fidget that makes the gun wander. So we
// use the FINAL FRAME of "Take" (the drawn/ready stance) as a STATIC idle pose
// (held, not looped), and only PLAY Shoot on LMB / Reload on R, each returning
// to that held pose. Clip indices resolved BY NAME at load.
static Model           g_gun;
static ModelAnimation *g_gunAnim = NULL;
static int             g_gunAnimN = 0;
static int             g_hasGun = 0;
static int             g_aIdle=0, g_aShoot=1, g_aReload=2;  // idle = hold last frame of "Take"
static int             g_curAnim = 0;           // start in idle (Take, held at end)
static float           g_animT = 0.0f;          // frame cursor
static int             g_animOnce = 0;          // current clip is a one-shot (Reload)
static float           g_fireCd = 0.0f;
static float           g_recoil = 0.0f;         // 0..1 recoil kick on fire (decays); drives muzzle-up
static Camera3D        g_vmCam;
static int             g_inspect = 0;           // V: gun floats in the world
static Vector3         g_gunCentroid = { 0, 0, 0 };  // bind-pose centroid, measured ONCE at load
static float           g_gunFitScale = 1.0f;         // inspect-view fit scale, measured ONCE at load

// This rig is BIG and off-origin (~2783u, centroid ~Y1390 Z849, barrel along
// model +Y). So: tiny scale, and recenter on the centroid so the gun lands at
// g_vmOff regardless of its authored origin. Orientation is a guess (the inspect
// view V confirms the model; tune yaw/pitch live to aim it down-range). 0=reset.
// Held-weapon framing: anchor the gun LOW and to the RIGHT, barrel reaching
// toward screen center (like a real FPS), bigger scale so it fills the lower
// third. These are best-guess defaults; tune live and 0 to reset.
// Defaults dialled in live and read off the dev overlay - held low-right,
// barrel down-range. vm_tune.txt (if present) overrides these at load.
#define VM_OFF0    ((Vector3){ -2.45f, -5.55f, 1.68f })
#define VM_SCALE0  0.0004f
#define VM_YAW0    178.0f
#define VM_PITCH0  -306.0f
static Vector3 g_vmOff   = { -2.45f, -5.55f, 1.68f };
static float   g_vmScale = VM_SCALE0;
static float   g_vmYaw   = VM_YAW0;
static float   g_vmPitch = VM_PITCH0;

// Persist the live-tuned viewmodel transform across runs so the player's
// dialed-in framing survives a rebuild. Saved to ./vm_tune.txt on quit, loaded
// at startup. 7 floats: offX offY offZ scale yaw pitch (one line).
// A weapon slot owns its model+clips+tuning; the hot-path render/fire code reads
// the active working-set globals above, which SwitchWeapon swaps in/out. Keys
// 1/2 select. Each weapon persists its own framing to its own tune file.
typedef struct {
    Model           model;
    ModelAnimation *anim;
    int             animN, has;
    int             aIdle, aShoot, aReload;
    Vector3         centroid;
    float           fitScale;
    Vector3         off;  float scale, yaw, pitch;     // live, persisted
    Vector3         off0; float scale0, yaw0, pitch0;  // defaults for 0=reset
    const char     *label;
    const char     *tunePath;
} Weapon;
static Weapon g_weapons[6];
static int    g_curWeapon = 0;
static int    g_numWeapons = 0;

static void SaveTune(const char *path, Vector3 off, float scale, float yaw, float pitch){
    FILE *f=fopen(path,"w"); if(!f) return;
    fprintf(f,"%.5f %.5f %.5f %.6f %.2f %.2f\n", off.x,off.y,off.z,scale,yaw,pitch);
    fclose(f);
}
static int LoadTune(const char *path, Vector3 *off, float *scale, float *yaw, float *pitch){
    FILE *f=fopen(path,"r"); if(!f) return 0;
    float ox,oy,oz,sc,yw,pt;
    int ok=(fscanf(f,"%f %f %f %f %f %f",&ox,&oy,&oz,&sc,&yw,&pt)==6);
    fclose(f);
    if(ok){ *off=(Vector3){ox,oy,oz}; *scale=sc; *yaw=yw; *pitch=pt; }
    return ok;
}
// Copy the live working-set tuning back into the current slot (call before a switch/quit).
static void StashActiveTuning(void){
    if (g_numWeapons<=0) return;
    Weapon *w=&g_weapons[g_curWeapon];
    w->off=g_vmOff; w->scale=g_vmScale; w->yaw=g_vmYaw; w->pitch=g_vmPitch;
}
// Make slot n the live weapon: copy its model/clips/tuning into the globals.
static void ActivateWeapon(int n){
    g_curWeapon=n;
    Weapon *w=&g_weapons[n];
    g_gun=w->model; g_gunAnim=w->anim; g_gunAnimN=w->animN; g_hasGun=w->has;
    g_aIdle=w->aIdle; g_aShoot=w->aShoot; g_aReload=w->aReload;
    g_gunCentroid=w->centroid; g_gunFitScale=w->fitScale;
    g_vmOff=w->off; g_vmScale=w->scale; g_vmYaw=w->yaw; g_vmPitch=w->pitch;
    g_curAnim=g_aIdle; g_animOnce=0; g_animT=0.0f; g_recoil=0.0f;
}
static void SwitchWeapon(int n){
    if (n<0 || n>=g_numWeapons || n==g_curWeapon || !g_weapons[n].has) return;
    StashActiveTuning();
    ActivateWeapon(n);
    DebugLog("weapon","\"slot\":%d,\"label\":\"%s\"", n, g_weapons[n].label);
}
// Load a glb into slot, resolve idle/shoot/reload clips by name, load saved tuning.
static void LoadWeapon(int slot, const char *path, const char *alt,
                       const char *label, const char *tunePath,
                       Vector3 off0, float scale0, float yaw0, float pitch0){
    Weapon *w=&g_weapons[slot];
    w->label=label; w->tunePath=tunePath;
    w->off0=off0; w->scale0=scale0; w->yaw0=yaw0; w->pitch0=pitch0;
    w->off=off0; w->scale=scale0; w->yaw=yaw0; w->pitch=pitch0;
    w->aIdle=0; w->aShoot=1; w->aReload=2; w->has=0; w->anim=NULL; w->animN=0;
    const char *fp=path; if(!FileExists(fp)) fp=alt;
    if(!FileExists(fp)){ DebugLog("weapon","\"slot\":%d,\"error\":\"missing\"",slot); return; }
    w->model=LoadModel(fp);
    w->anim=LoadModelAnimations(fp,&w->animN);
    w->has=(w->model.meshCount>0);
    BoundingBox sb=GetModelBoundingBox(w->model);
    w->centroid=(Vector3){(sb.min.x+sb.max.x)*0.5f,(sb.min.y+sb.max.y)*0.5f,(sb.min.z+sb.max.z)*0.5f};
    float md=fmaxf(sb.max.x-sb.min.x,fmaxf(sb.max.y-sb.min.y,sb.max.z-sb.min.z));
    w->fitScale=(md>0.001f)?1.5f/md:1.0f;
    // Auto default scale: a passed scale0<=0 means "size me from the bounding box"
    // so every weapon starts roughly rifle-sized + on-screen regardless of the
    // rig's native units (otherwise the rifle's fixed scale makes other weapons
    // microscopic/huge/off-frame). Tunes still override via the saved file.
    if (scale0<=0.0f){ float as=(md>0.001f)?1.1f/md:0.0004f; w->scale0=as; w->scale=as; }
    for (int i=0;i<w->animN;i++){
        const char *nm=w->anim[i].name; char low[64]; int j=0;
        for (; nm[j] && j<63; j++){ char c=nm[j]; if(c>='A'&&c<='Z') c+=32; low[j]=c; }
        low[j]=0;
        if (strstr(low,"idle")||strstr(low,"take")||strstr(low,"draw")||strstr(low,"weild")||strstr(low,"wield")) w->aIdle=i;
        else if (strstr(low,"shoot")||strstr(low,"fire")) w->aShoot=i;
        else if (strstr(low,"reload")) w->aReload=i;
    }
    LoadTune(w->tunePath,&w->off,&w->scale,&w->yaw,&w->pitch);
    DebugLog("weapon","\"slot\":%d,\"label\":\"%s\",\"bones\":%d,\"anims\":%d,\"idle\":%d,\"shoot\":%d,\"reload\":%d",
             slot,label,w->model.skeleton.boneCount,w->animN,w->aIdle,w->aShoot,w->aReload);
}

// ---- Tracers + impact sparks ------------------------------------------------
typedef struct { Vector3 a, b; float life; } Tracer;
typedef struct { Vector3 pos, vel; float life, life0, size; Color col; } Spark;
#define MAX_TRACERS 32
#define MAX_SPARKS  1400
static Tracer g_tracers[MAX_TRACERS];
static Spark  g_sparks[MAX_SPARKS];

// ---- Enemies (assets/enemy.glb - Mixamo walk rig, 37 bones, verified) -------
// One shared Model drawn many times; each enemy keeps its own animation cursor
// so they don't march in lockstep. Mixamo export is cm-scale (~185u tall) with
// feet at the origin facing +Z, so ENEMY_SCALE shrinks to ~1.85m and we yaw
// each to face the player. ENEMY_YAW_OFFSET flips facing if they moonwalk.
#define MAX_ENEMIES        12
#define ENEMY_SCALE        1.0000f      // FBX2glTF export is already upright meter-scale (~1.86m)
#define ENEMY_SPEED        3.4f
#define ENEMY_HP           100.0f
#define ENEMY_DMG_PER_SHOT 34.0f      // ~3 shots to kill
#define ENEMY_RADIUS       0.5f
#define ENEMY_HEIGHT       1.85f
#define ENEMY_YAW_OFFSET   0.0f       // tune if they face the wrong way
#define ENEMY_TOUCH_DMG    18.0f      // player hp/sec when an enemy is in melee
#define ENEMY_ATTACK_RANGE  1.8f       // enters attack/melee within this range
typedef struct { Vector3 pos; float hp; int state; int clip; float animT; float deathT; float hitT; } Enemy; // state: 0 dead,1 alive,2 dying
static Enemy           g_enemies[MAX_ENEMIES];
static Model           g_enemy;
static ModelAnimation *g_enemyAnim = NULL;
static int             g_enemyAnimN = 0;
static int g_eWalk=0, g_eIdle=0, g_eRun=0, g_eHit=0, g_eAttack=0, g_eDeath=0; // clips resolved by name at load
static int             g_hasEnemy = 0;
static int             g_kills = 0;
static float           g_playerHp = 100.0f;

static void SpawnEnemy(int i){
    // place at a random spot near a wall, away from the player
    float ang=(float)GetRandomValue(0,628)/100.0f;
    float dist=ARENA*0.6f + GetRandomValue(0,(int)(ARENA*0.3f));
    g_enemies[i].pos=(Vector3){ cosf(ang)*dist, 0, sinf(ang)*dist };
    g_enemies[i].hp=ENEMY_HP; g_enemies[i].state=1;
    g_enemies[i].animT=(float)GetRandomValue(0,40); g_enemies[i].deathT=0;
    g_enemies[i].hitT=0; g_enemies[i].clip=g_eRun;
}

static Texture2D MakeChecker(int sz, Color a, Color b, int cells) {
    Image img = GenImageChecked(sz, sz, sz/cells, sz/cells, a, b);
    Texture2D t = LoadTextureFromImage(img); UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    return t;
}

static void SpawnImpact(Vector3 p, Vector3 n) {
    for (int i=0;i<14;i++) for (int s=0;s<MAX_SPARKS;s++){
        if (g_sparks[s].life>0) continue;
        Vector3 v={ n.x+GetRandomValue(-100,100)/100.0f, n.y+GetRandomValue(-20,120)/100.0f, n.z+GetRandomValue(-100,100)/100.0f };
        v=Vector3Scale(Vector3Normalize(v), 3.0f+GetRandomValue(0,300)/100.0f);
        float lf=0.4f+GetRandomValue(0,30)/100.0f;
        g_sparks[s]=(Spark){ p, v, lf, lf, 0.04f, (Color){255,(unsigned char)(200+GetRandomValue(0,55)),60,255} };
        break;
    }
}

// Detailed blood burst: a spray of crimson droplets of varied size flung along
// the bullet's travel (dir) in a wide cone, plus heavier slow "gobs" every few
// particles. Gravity + fade are handled by the shared spark integrator/draw.
static void SpawnBlood(Vector3 p, Vector3 dir) {
    for (int i=0;i<200;i++) for (int s=0;s<MAX_SPARKS;s++){
        if (g_sparks[s].life>0) continue;
        float spread=3.6f;
        Vector3 v={ dir.x*4.5f + GetRandomValue(-100,100)/100.0f*spread,
                    dir.y*4.5f + GetRandomValue(-100,100)/100.0f*spread + 1.7f,
                    dir.z*4.5f + GetRandomValue(-100,100)/100.0f*spread };
        int gob=(i%5==0);                                  // heavy gobs: slower, bigger, redder
        if (gob){ v.x*=0.45f; v.y=v.y*0.45f+0.4f; v.z*=0.45f; }
        float lf = 1.1f + GetRandomValue(0,120)/100.0f;
        float sz = gob ? (0.10f+GetRandomValue(0,7)/100.0f) : (0.028f+GetRandomValue(0,4)/100.0f);
        unsigned char r=(unsigned char)(150+GetRandomValue(0,105)); // dark maroon -> arterial red
        unsigned char g=(unsigned char)GetRandomValue(0,28);
        unsigned char b=(unsigned char)GetRandomValue(0,18);
        g_sparks[s]=(Spark){ p, v, lf, lf, sz, (Color){r,g,b,255} };
        break;
    }
}

static int RaycastWorld(Vector3 ro, Vector3 rd, Vector3 *hit, Vector3 *nrm) {
    float best=1e9f; int got=0;
    if (rd.y<-1e-4f){ float t=-ro.y/rd.y; if(t>0&&t<best){best=t;*hit=Vector3Add(ro,Vector3Scale(rd,t));*nrm=(Vector3){0,1,0};got=1;} }
    if (fabsf(rd.x)>1e-4f) for(int s=0;s<2;s++){ float px=s?-ARENA:ARENA; float t=(px-ro.x)/rd.x;
        if(t>0&&t<best){Vector3 h=Vector3Add(ro,Vector3Scale(rd,t)); if(fabsf(h.z)<=ARENA&&h.y>=0&&h.y<=WALL_H){best=t;*hit=h;*nrm=(Vector3){s?1.f:-1.f,0,0};got=1;}}}
    if (fabsf(rd.z)>1e-4f) for(int s=0;s<2;s++){ float pz=s?-ARENA:ARENA; float t=(pz-ro.z)/rd.z;
        if(t>0&&t<best){Vector3 h=Vector3Add(ro,Vector3Scale(rd,t)); if(fabsf(h.x)<=ARENA&&h.y>=0&&h.y<=WALL_H){best=t;*hit=h;*nrm=(Vector3){0,0,s?1.f:-1.f};got=1;}}}
    for (int c=0;c<NUM_CRATES;c++){
        Vector3 mn=Vector3Subtract(g_crates[c].pos,Vector3Scale(g_crates[c].size,0.5f));
        Vector3 mx=Vector3Add(g_crates[c].pos,Vector3Scale(g_crates[c].size,0.5f));
        RayCollision rc=GetRayCollisionBox((Ray){ro,rd},(BoundingBox){mn,mx});
        if(rc.hit&&rc.distance>0&&rc.distance<best){best=rc.distance;*hit=rc.point;*nrm=rc.normal;got=1;}
    }
    return got;
}

// switch the weapon animation, restart its cursor
static void SetAnim(int idx, int once){
    if (idx==g_curAnim) return;
    g_curAnim=idx; g_animT=0.0f; g_animOnce=once;
}

// Test the aim ray against alive enemies (treated as a vertical box). Returns
// the nearest enemy index hit within maxDist, -1 if none; sets *outDist/*outPt.
static int HitEnemy(Vector3 ro, Vector3 rd, float maxDist, float *outDist, Vector3 *outPt){
    int best=-1; float bd=maxDist;
    for (int i=0;i<MAX_ENEMIES;i++){
        if (g_enemies[i].state!=1) continue;
        Vector3 c=g_enemies[i].pos;
        BoundingBox bb={ (Vector3){c.x-ENEMY_RADIUS, 0, c.z-ENEMY_RADIUS},
                         (Vector3){c.x+ENEMY_RADIUS, ENEMY_HEIGHT, c.z+ENEMY_RADIUS} };
        RayCollision rc=GetRayCollisionBox((Ray){ro,rd},bb);
        if (rc.hit && rc.distance>0 && rc.distance<bd){ bd=rc.distance; best=i; *outDist=rc.distance; *outPt=rc.point; }
    }
    return best;
}

static void Fire(Camera3D cam) {
    if (g_fireCd>0) return;
    g_fireCd=0.12f;
    Vector3 dir=Vector3Normalize(Vector3Subtract(cam.target,cam.position));
    Vector3 muzzle=Vector3Add(cam.position,Vector3Scale(dir,0.4f));
    Vector3 hit,nrm,end=Vector3Add(cam.position,Vector3Scale(dir,80.0f));
    float worldDist=80.0f;
    int worldHit=RaycastWorld(cam.position,dir,&hit,&nrm);
    if (worldHit){ end=hit; worldDist=Vector3Distance(cam.position,hit); }
    // enemy hit takes priority if it's closer than the world geometry
    float ed; Vector3 ep;
    int ei=HitEnemy(cam.position,dir,worldDist,&ed,&ep);
    if (ei>=0){
        end=ep;
        SpawnBlood(ep,dir);                               // detailed blood spray
        g_enemies[ei].hp-=ENEMY_DMG_PER_SHOT;
        if (g_enemies[ei].hp<=0){ g_enemies[ei].state=2; g_enemies[ei].deathT=0; g_enemies[ei].animT=0; g_kills++; }
        else g_enemies[ei].hitT=0.45f;   // non-fatal hit -> flinch
    } else if (worldHit){
        SpawnImpact(hit,nrm);
    }
    for (int i=0;i<MAX_TRACERS;i++) if (g_tracers[i].life<=0){ g_tracers[i]=(Tracer){muzzle,end,0.05f}; break; }
    // Code-driven recoil instead of the Shoot clip (that clip repositions the
    // gun out of the viewmodel frame -> "gun goes away"). Kick rises to 1, decays.
    g_recoil=1.0f;
    DebugLog("fire","\"enemy\":%d,\"end\":[%.2f,%.2f,%.2f]", ei, end.x,end.y,end.z);
}

// Walk alive enemies toward the player; advance death timers; melee on contact.
static void UpdateEnemies(float dt){
    if (!g_hasEnemy) return;
    for (int i=0;i<MAX_ENEMIES;i++){
        Enemy *e=&g_enemies[i];
        if (e->state==1){
            float dx=g_pos.x-e->pos.x, dz=g_pos.z-e->pos.z;
            float d=sqrtf(dx*dx+dz*dz);
            if (e->hitT>0) e->hitT-=dt;
            // clip priority: flinch > attack (in melee range) > run (chasing)
            int want = (e->hitT>0) ? g_eHit : (d<=ENEMY_ATTACK_RANGE ? g_eAttack : g_eRun);
            if (want!=e->clip){ e->clip=want; e->animT=0; }
            if (e->hitT<=0 && d>ENEMY_ATTACK_RANGE){           // chase unless flinching/in melee
                e->pos.x+=dx/d*ENEMY_SPEED*dt;
                e->pos.z+=dz/d*ENEMY_SPEED*dt;
            }
            if (e->clip==g_eAttack && e->hitT<=0) g_playerHp-=ENEMY_TOUCH_DMG*dt;
            if (g_enemyAnimN>0){                               // advance current clip (looping)
                int nf=ANIM_FRAMES(g_enemyAnim[e->clip]);
                float spd=(e->clip==g_eAttack)?90.0f:30.0f;    // attack plays much faster
                e->animT+=dt*spd;
                if (nf>0 && e->animT>=nf) e->animT-=nf;
            }
        } else if (e->state==2){                              // dying: play death once, then respawn
            if (g_enemyAnimN>0){
                int nf=ANIM_FRAMES(g_enemyAnim[g_eDeath]);
                if (nf>0){ e->animT+=dt*60.0f; if (e->animT>nf-1) e->animT=(float)(nf-1); }
            }
            e->deathT+=dt;
            if (!g_noEnemies && e->deathT>1.8f) SpawnEnemy(i);
        }
    }
    for (int a=0;a<MAX_ENEMIES;a++){            // keep enemies from overlapping each other
        if (g_enemies[a].state!=1) continue;
        for (int b=a+1;b<MAX_ENEMIES;b++){
            if (g_enemies[b].state!=1) continue;
            float dx=g_enemies[b].pos.x-g_enemies[a].pos.x;
            float dz=g_enemies[b].pos.z-g_enemies[a].pos.z;
            float d=sqrtf(dx*dx+dz*dz), mind=ENEMY_RADIUS*2.0f;
            if (d<mind && d>1e-4f){
                float push=(mind-d)*0.5f/d;
                g_enemies[a].pos.x-=dx*push; g_enemies[a].pos.z-=dz*push;
                g_enemies[b].pos.x+=dx*push; g_enemies[b].pos.z+=dz*push;
            }
        }
    }
    if (g_playerHp<0) g_playerHp=0;
}

static void Collide(void) {
    float r=0.4f;
    if (g_pos.x> ARENA-r) g_pos.x= ARENA-r;  if (g_pos.x<-ARENA+r) g_pos.x=-ARENA+r;
    if (g_pos.z> ARENA-r) g_pos.z= ARENA-r;  if (g_pos.z<-ARENA+r) g_pos.z=-ARENA+r;
    for (int c=0;c<NUM_CRATES;c++){
        Vector3 b=g_crates[c].pos, hs=Vector3Scale(g_crates[c].size,0.5f);
        float dx=g_pos.x-b.x, dz=g_pos.z-b.z;
        float ox=hs.x+r-fabsf(dx), oz=hs.z+r-fabsf(dz);
        if (ox>0&&oz>0){ if(ox<oz) g_pos.x+=(dx>0?ox:-ox); else g_pos.z+=(dz>0?oz:-oz); }
    }
    for (int e=0;e<MAX_ENEMIES;e++){            // don't let the player walk through enemies
        if (g_enemies[e].state!=1) continue;
        float dx=g_pos.x-g_enemies[e].pos.x, dz=g_pos.z-g_enemies[e].pos.z;
        float d=sqrtf(dx*dx+dz*dz), mind=r+ENEMY_RADIUS;
        if (d>1e-4f){ if (d<mind){ float push=(mind-d)/d; g_pos.x+=dx*push; g_pos.z+=dz*push; } }
        else g_pos.x+=mind;                      // exactly coincident: nudge out
    }
}

static Camera3D g_cam;

static void Update(void) {
    float dt=GetFrameTime(); if (dt>0.05f) dt=0.05f;

    if (IsWindowReady() && IsCursorHidden()){
        Vector2 md=GetMouseDelta();
        g_yaw-=md.x*0.0025f; g_pitch-=md.y*0.0025f;
        if (g_pitch>1.5f) g_pitch=1.5f;  if (g_pitch<-1.5f) g_pitch=-1.5f;
    }
    Vector3 fwd =Vector3Normalize((Vector3){ sinf(g_yaw)*cosf(g_pitch), sinf(g_pitch), cosf(g_yaw)*cosf(g_pitch) });
    Vector3 flat=Vector3Normalize((Vector3){ sinf(g_yaw), 0, cosf(g_yaw) });
    Vector3 right=Vector3Normalize(Vector3CrossProduct(flat,(Vector3){0,1,0}));

    float speed=IsKeyDown(KEY_LEFT_SHIFT)?9.0f:5.0f;
    Vector3 wish={0,0,0}; int moving=0;
    if (IsKeyDown(KEY_W)){wish=Vector3Add(wish,flat);moving=1;}
    if (IsKeyDown(KEY_S)){wish=Vector3Subtract(wish,flat);moving=1;}
    if (IsKeyDown(KEY_D)){wish=Vector3Add(wish,right);moving=1;}
    if (IsKeyDown(KEY_A)){wish=Vector3Subtract(wish,right);moving=1;}
    if (moving && Vector3Length(wish)>0.001f){
        wish=Vector3Scale(Vector3Normalize(wish),speed*dt);
        g_pos.x+=wish.x; g_pos.z+=wish.z;
        g_bob+=dt*(IsKeyDown(KEY_LEFT_SHIFT)?14.0f:10.0f);
    }
    if (g_grounded && IsKeyPressed(KEY_SPACE)){ g_vy=5.0f; g_grounded=0; }
    g_vy-=16.0f*dt; g_pos.y+=g_vy*dt;
    if (g_pos.y<=EYE_H){ g_pos.y=EYE_H; g_vy=0; g_grounded=1; }
    Collide();

    g_cam.position=g_pos; g_cam.target=Vector3Add(g_pos,fwd); g_cam.up=(Vector3){0,1,0};

    // shooting / reload
    if (g_fireCd>0) g_fireCd-=dt;
    if (g_recoil>0) g_recoil=fmaxf(0.0f, g_recoil - dt*7.0f);   // recoil settles in ~0.14s
    if (IsKeyPressed(KEY_R)) SetAnim(g_aReload,1);
    else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && g_curAnim!=g_aReload) Fire(g_cam);

    UpdateEnemies(dt);

    // advance weapon animation. idle = HOLD the last frame of Take (static ready
    // pose, no wandering). One-shot clips (Shoot/Reload) play through, then snap
    // back to the held idle.
    if (g_gunAnimN>0 && g_curAnim<g_gunAnimN){
        int nf=ANIM_FRAMES(g_gunAnim[g_curAnim]);
        if (nf>0){
            if (g_curAnim==g_aIdle && !g_animOnce){
                g_animT=(float)(nf-1);          // freeze on the drawn/ready frame
            } else {
                g_animT += dt*30.0f;
                if (g_animT>=nf){
                    if (g_animOnce){ g_curAnim=g_aIdle; g_animOnce=0; g_animT=0.0f; }
                    else g_animT-=nf;
                }
            }
        }
    }

    // overlay + inspect + reset
    if (IsKeyPressed(KEY_GRAVE) || IsKeyPressed(KEY_TAB)) g_devOverlay=!g_devOverlay;
    if (IsKeyPressed(KEY_V)) g_inspect=!g_inspect;
    if (IsKeyPressed(KEY_N)){                          // N: toggle orient mode (no enemies, weapon-aim tuning)
        g_noEnemies=!g_noEnemies;
        for (int i=0;i<MAX_ENEMIES;i++) g_enemies[i].state=0;   // clear the field
        if (!g_noEnemies) for (int i=0;i<5;i++) SpawnEnemy(i);  // bring the wave back
        DebugLog("mode","\"noEnemies\":%s", g_noEnemies?"true":"false");
    }
    if (IsKeyPressed(KEY_ONE)) SwitchWeapon(0);
    if (IsKeyPressed(KEY_TWO)) SwitchWeapon(1);
    if (IsKeyPressed(KEY_THREE)) SwitchWeapon(2);
    if (IsKeyPressed(KEY_FOUR)) SwitchWeapon(3);
    if (IsKeyPressed(KEY_FIVE)) SwitchWeapon(4);
    if (IsKeyPressed(KEY_SIX)) SwitchWeapon(5);
    if (IsKeyPressed(KEY_ZERO)){ Weapon *w=&g_weapons[g_curWeapon]; g_vmOff=w->off0; g_vmScale=w->scale0; g_vmYaw=w->yaw0; g_vmPitch=w->pitch0; }
    if (IsKeyPressed(KEY_F5)){ StashActiveTuning(); SaveTune(g_weapons[g_curWeapon].tunePath,g_vmOff,g_vmScale,g_vmYaw,g_vmPitch); DebugLog("vmsave","\"slot\":%d,\"saved\":true",g_curWeapon); }

    // live viewmodel tuning (faster in orient mode so a mispositioned weapon is
    // easy to sweep back into frame)
    float ns=dt*(g_noEnemies?3.0f:0.5f);
    float rs=dt*(g_noEnemies?120.0f:60.0f);
    float ss=g_noEnemies?3.0f:1.0f;
    if (IsKeyDown(KEY_I)) g_vmOff.y+=ns;  if (IsKeyDown(KEY_K)) g_vmOff.y-=ns;
    if (IsKeyDown(KEY_L)) g_vmOff.x+=ns;  if (IsKeyDown(KEY_J)) g_vmOff.x-=ns;
    if (IsKeyDown(KEY_O)) g_vmOff.z+=ns;  if (IsKeyDown(KEY_U)) g_vmOff.z-=ns;
    if (IsKeyDown(KEY_EQUAL)) g_vmScale*=(1.0f+dt*ss);  if (IsKeyDown(KEY_MINUS)) g_vmScale*=(1.0f-dt*ss);
    if (IsKeyDown(KEY_RIGHT_BRACKET)) g_vmYaw+=rs;  if (IsKeyDown(KEY_LEFT_BRACKET)) g_vmYaw-=rs;
    if (IsKeyDown(KEY_APOSTROPHE)) g_vmPitch+=rs;   if (IsKeyDown(KEY_SEMICOLON)) g_vmPitch-=rs;
    if (IsKeyPressed(KEY_P))
        DebugLog("vmxform","\"off\":[%.3f,%.3f,%.3f],\"scale\":%.4f,\"yaw\":%.1f,\"pitch\":%.1f",
                 g_vmOff.x,g_vmOff.y,g_vmOff.z,g_vmScale,g_vmYaw,g_vmPitch);
    if (g_noEnemies){   // orient mode: log the selected weapon + orientation whenever it changes
        static float lo[6]={1e9f,0,0,0,0,0};
        float cur[6]={g_vmOff.x,g_vmOff.y,g_vmOff.z,g_vmScale,g_vmYaw,g_vmPitch};
        int changed=0; for(int k=0;k<6;k++) if(fabsf(cur[k]-lo[k])>1e-4f) changed=1;
        if (changed){ for(int k=0;k<6;k++) lo[k]=cur[k];
            DebugLog("orient","\"weapon\":\"%s\",\"off\":[%.3f,%.3f,%.3f],\"scale\":%.5f,\"yaw\":%.1f,\"pitch\":%.1f",
                     g_weapons[g_curWeapon].label,g_vmOff.x,g_vmOff.y,g_vmOff.z,g_vmScale,g_vmYaw,g_vmPitch);
        }
    }

    for (int i=0;i<MAX_TRACERS;i++) if (g_tracers[i].life>0) g_tracers[i].life-=dt;
    for (int i=0;i<MAX_SPARKS;i++) if (g_sparks[i].life>0){
        Spark *sp=&g_sparks[i]; sp->life-=dt;
        int resting=(sp->pos.y<=0.02f && sp->vel.x==0 && sp->vel.z==0 && sp->vel.y==0);
        if (!resting){
            sp->vel.y-=12.0f*dt;
            sp->pos=Vector3Add(sp->pos,Vector3Scale(sp->vel,dt));
            if (sp->pos.y<=0.0f){            // hit the floor -> splat into a resting puddle
                sp->pos.y=0.015f; sp->vel=(Vector3){0,0,0};
                if (sp->life<2.8f) sp->life=2.8f; sp->life0=fmaxf(sp->life0,sp->life);
            }
        }
    }
}

static void DrawWorld(void) {
    DrawModelEx(g_floor,(Vector3){0,0,0},(Vector3){0,1,0},0,(Vector3){ARENA*2,1,ARENA*2},WHITE);
    DrawModelEx(g_wall,(Vector3){0,WALL_H/2, ARENA},(Vector3){0,1,0}, 0,(Vector3){ARENA*2,WALL_H,1},WHITE);
    DrawModelEx(g_wall,(Vector3){0,WALL_H/2,-ARENA},(Vector3){0,1,0}, 0,(Vector3){ARENA*2,WALL_H,1},WHITE);
    DrawModelEx(g_wall,(Vector3){ ARENA,WALL_H/2,0},(Vector3){0,1,0},90,(Vector3){ARENA*2,WALL_H,1},WHITE);
    DrawModelEx(g_wall,(Vector3){-ARENA,WALL_H/2,0},(Vector3){0,1,0},90,(Vector3){ARENA*2,WALL_H,1},WHITE);
    for (int c=0;c<NUM_CRATES;c++)
        DrawModelEx(g_crate,g_crates[c].pos,(Vector3){0,1,0},0,g_crates[c].size,g_crates[c].col);
    for (int i=0;i<MAX_TRACERS;i++) if (g_tracers[i].life>0)
        DrawLine3D(g_tracers[i].a,g_tracers[i].b,(Color){255,240,150,255});
    for (int i=0;i<MAX_SPARKS;i++) if (g_sparks[i].life>0){
        Spark *sp=&g_sparks[i];
        float k=(sp->life0>0)?sp->life/sp->life0:1.0f;     // 1 at birth -> 0 at death
        float sz=sp->size*(0.45f+0.55f*k);                 // droplets shrink as they fade
        Color c=sp->col; c.a=(unsigned char)(k*255.0f);    // and fade out
        DrawSphereEx(sp->pos, sz*0.6f, 6, 6, c);
    }
}

// Draw all alive/dying enemies. The shared model is posed to each enemy's own
// animation cursor right before drawing, so they animate independently.
static void DrawEnemies(void){
    if (!g_hasEnemy) return;
    for (int i=0;i<MAX_ENEMIES;i++){
        Enemy *e=&g_enemies[i];
        if (e->state==0) continue;
        float dx=g_pos.x-e->pos.x, dz=g_pos.z-e->pos.z;       // face the player (yaw about +Y)
        float yaw=atan2f(dx,dz)*RAD2DEG + ENEMY_YAW_OFFSET;
        float sink=0.0f;
        if (e->state==2){                                     // dying: play death clip, sink late
            float late=e->deathT-1.2f; if (late>0) sink=-late*2.7f;
            if (g_enemyAnimN>0) ANIM_APPLY(g_enemy, g_enemyAnim[g_eDeath], e->animT);
        } else {
            if (g_enemyAnimN>0) ANIM_APPLY(g_enemy, g_enemyAnim[e->clip], e->animT);
        }
        DrawModelEx(g_enemy, (Vector3){e->pos.x, sink, e->pos.z}, (Vector3){0,1,0}, yaw,
                    (Vector3){ENEMY_SCALE,ENEMY_SCALE,ENEMY_SCALE}, WHITE);
        if (e->state==1 && e->hp<ENEMY_HP){                   // HP bar above damaged enemies
            Vector3 hp={e->pos.x, ENEMY_HEIGHT+0.3f, e->pos.z};
            DrawCube(hp, 0.6f*(e->hp/ENEMY_HP), 0.08f, 0.02f, (Color){230,60,60,255});
        }
    }
}

static void PoseGun(void){
    if (g_gunAnimN>0 && g_curAnim<g_gunAnimN) ANIM_APPLY(g_gun, g_gunAnim[g_curAnim], g_animT);
}

// INSPECT (V): gun floats ~2.5m ahead, auto-fit, spinning, in the world camera.
// Can't-fail view - if the model loaded, you WILL see it here.
static void DrawInspect(void){
    if (!g_hasGun) return;
    PoseGun();
    // Use the centroid + scale measured ONCE at load (NOT per-frame bbox - that
    // shifts as the animation moves the geometry, making the gun wander).
    float s=g_gunFitScale;
    Vector3 dir=Vector3Normalize(Vector3Subtract(g_cam.target,g_cam.position));
    Vector3 at=Vector3Add(g_cam.position,Vector3Scale(dir,2.5f));
    Matrix rot=MatrixRotateY(DEG2RAD*(float)GetTime()*40.0f);
    Vector3 pos=Vector3Subtract(at,Vector3Scale(Vector3Transform(g_gunCentroid,rot),s));
    BeginMode3D(g_cam);
        g_gun.transform=rot;
        DrawModelEx(g_gun,pos,(Vector3){0,1,0},0,(Vector3){s,s,s},WHITE);
    EndMode3D();
    g_gun.transform=MatrixIdentity();
}

static void DrawViewmodel(void){
    // In orient mode the live first-person viewmodel must always show so tuning
    // is visible; inspect (which ignores the transform) is suppressed there.
    if (!g_hasGun || (g_inspect && !g_noEnemies)) return;
    PoseGun();
    float bobX=sinf(g_bob)*0.012f, bobY=fabsf(cosf(g_bob))*0.010f;
    // recoil: muzzle kicks UP (pitch) and the gun shoves slightly up+back, then
    // settles. r is the live kick amount (0..1) driven by Fire().
    float r=g_recoil;
    Vector3 target={ g_vmOff.x+bobX, g_vmOff.y+bobY+r*0.012f, g_vmOff.z+r*0.03f };
    Matrix rot=MatrixMultiply(MatrixRotateX(DEG2RAD*(g_vmPitch - r*4.0f)), MatrixRotateY(DEG2RAD*g_vmYaw));
    // recenter: place the model's centroid AT target (model is far off-origin)
    Vector3 vpos=Vector3Subtract(target, Vector3Scale(Vector3Transform(g_gunCentroid,rot), g_vmScale));
    rlDrawRenderBatchActive();
#if !defined(_WIN32)
    glClear(GL_DEPTH_BUFFER_BIT);   // weapon always on top
#endif
    BeginMode3D(g_vmCam);
        g_gun.transform=rot;
        DrawModelEx(g_gun,vpos,(Vector3){0,1,0},0,(Vector3){g_vmScale,g_vmScale,g_vmScale},WHITE);
    EndMode3D();
    g_gun.transform=MatrixIdentity();
}

// Drop-shadowed text: a near-black copy offset down-right, then the bright text
// on top. The raylib default font is a small bitmap that turns muddy against the
// 3D scene; the shadow gives every glyph a hard edge so it stays readable.
static void TextSh(const char *t, int x, int y, int sz, Color c){
    DrawText(t, x+2, y+2, sz, (Color){0,0,0,200});
    DrawText(t, x,   y,   sz, c);
}

static void DrawHUD(void) {
    int W=GetScreenWidth(), H=GetScreenHeight();
    Color cc={0,255,120,220};
    DrawLine(W/2-10,H/2,W/2-3,H/2,cc); DrawLine(W/2+3,H/2,W/2+10,H/2,cc);
    DrawLine(W/2,H/2-10,W/2,H/2-3,cc); DrawLine(W/2,H/2+3,W/2,H/2+10,cc);
    DrawFPS(W-90,10);
    // kills + player health
    DrawText(TextFormat("KILLS %d", g_kills), W-160, 36, 22, (Color){255,230,120,255});
    DrawRectangle(20, H-44, 224, 24, (Color){0,0,0,150});
    DrawRectangle(22, H-42, (int)(220*g_playerHp/100.0f), 20, (Color){200,40,40,255});
    DrawText(TextFormat("HP %d", (int)g_playerHp), 28, H-40, 16, RAYWHITE);
    if (g_playerHp<=0) DrawText("YOU DIED - press ESC", W/2-120, H/2+30, 24, (Color){255,80,80,255});
    // Big unmistakable mode banner so "floating" can be diagnosed: INSPECT mode
    // intentionally floats the gun in front of you; press V to get back to FP.
    if (g_inspect)
        DrawText("INSPECT MODE - press V for first-person", W/2-220, 30, 24, (Color){255,180,40,255});
    if (g_devOverlay){
        DrawRectangle(0,0,380,90,(Color){0,0,0,150});
        DrawText("CHERNOBYL 2  -  M16A3 (LMB fire, R reload)",6,6,12,GRAY);
        DrawText(TextFormat("%s  [%s]  1-6=weapon N=mode V=inspect 0=reset", g_inspect?"INSPECT":"FP", g_weapons[g_curWeapon].label),8,22,16,LIME);
        if (g_noEnemies){   // orient mode panel - bigger + drop-shadowed for legibility
            DrawRectangle(6,96,560,196,(Color){0,0,0,205});
            DrawRectangleLines(6,96,560,196,(Color){255,210,60,255});
            TextSh("ORIENT MODE  (N = back to play)",18,104,28,YELLOW);
            TextSh(TextFormat("weapon:  %s",g_weapons[g_curWeapon].label),18,142,24,(Color){120,230,255,255});
            TextSh(TextFormat("off  [%.2f %.2f %.2f]   scale %.5f",g_vmOff.x,g_vmOff.y,g_vmOff.z,g_vmScale),18,176,20,RAYWHITE);
            TextSh(TextFormat("yaw  %.1f      pitch  %.1f",g_vmYaw,g_vmPitch),18,202,20,RAYWHITE);
            TextSh("IJKL/UO move   -/= scale   [ ] yaw   ; \047 pitch",18,234,18,(Color){210,210,210,255});
            TextSh("0 = reset      F5 = save this weapon",18,258,18,(Color){210,210,210,255});
        }
        DrawText(TextFormat("vm off %.2f %.2f %.2f  scale %.5f  yaw %.0f pit %.0f",
                 g_vmOff.x,g_vmOff.y,g_vmOff.z,g_vmScale,g_vmYaw,g_vmPitch),8,42,13,RAYWHITE);
        DrawText("IJKL/UO move  -/= scale  [/] yaw  ;/' pitch  P log",8,60,12,GRAY);
    }
}

static void Frame(void) {
    static int frameNo=0;
    Update();
    BeginDrawing();
        ClearBackground((Color){70,90,110,255});
        BeginMode3D(g_cam);
            DrawWorld();
            DrawEnemies();
        EndMode3D();
        if (g_inspect && !g_noEnemies) DrawInspect();
        DrawViewmodel();
        DrawHUD();
    EndDrawing();
    if (g_debug && (frameNo%30==0))
        DebugLog("frame","\"n\":%d,\"fps\":%d,\"anim\":%d,\"pos\":[%.1f,%.1f,%.1f]",
                 frameNo,GetFPS(),g_curAnim,g_pos.x,g_pos.y,g_pos.z);
    if (g_shotFrame>0 && frameNo==g_shotFrame){
        TakeScreenshot("chernobyl2-shot.png");
        DebugLog("shot","\"file\":\"chernobyl2-shot.png\",\"frame\":%d", frameNo);
    }
    frameNo++;
}

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

int main(int argc, char **argv) {
    for (int i=1;i<argc;i++){
        if (!strcmp(argv[i],"--debug")) g_debug=1;
        else if (!strcmp(argv[i],"--frames") && i+1<argc) g_maxFrames=atoi(argv[++i]);
        else if (!strcmp(argv[i],"--shot") && i+1<argc) g_shotFrame=atoi(argv[++i]);
        else if (!strcmp(argv[i],"--no-enemies")) g_noEnemies=1;
    }
    // Send the JSON event stream straight to a log file (not stdout/stderr, so
    // it stays clean of raylib's own warnings). Fresh file each run.
    if (g_debug){
        g_dbg=fopen("chernobyl2-debug.log","w");
        if (!g_dbg) g_dbg=stderr;   // fall back if the file can't be opened
    }

    SetTraceLogLevel(LOG_WARNING);
    InitWindow(1280,720,"Chernobyl 2");
    int ready=IsWindowReady();
    DebugLog("boot","\"windowReady\":%s,\"raylib\":\"%s\"", ready?"true":"false", RAYLIB_VERSION);

    const char *gunPath="assets/rifle.glb";
    if (!FileExists(gunPath)) gunPath="../assets/rifle.glb";
    DebugLog("model","\"path\":\"%s\"", JStr(gunPath));

    if (!ready){   // headless: nothing to render without a GL context
        DebugLog("shutdown","\"reason\":\"no-gl-context\"");
        return 0;
    }

    SetTargetFPS(60);
    DisableCursor();

    g_floorTex=MakeChecker(512,(Color){60,64,70,255},(Color){44,48,54,255},16);
    g_wallTex =MakeChecker(256,(Color){80,72,64,255},(Color){64,58,52,255},8);
    g_crateTex=MakeChecker(128,(Color){120,90,55,255},(Color){95,70,42,255},4);
    g_floor=LoadModelFromMesh(GenMeshPlane(1,1,1,1));
    g_wall =LoadModelFromMesh(GenMeshCube(1,1,1));
    g_crate=LoadModelFromMesh(GenMeshCube(1,1,1));
    g_floor.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture=g_floorTex;
    g_wall.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture =g_wallTex;
    g_crate.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture=g_crateTex;

    for (int c=0;c<NUM_CRATES;c++){
        float gx=GetRandomValue(-(int)ARENA+4,(int)ARENA-4);
        float gz=GetRandomValue(-(int)ARENA+4,(int)ARENA-4);
        float h=1.0f+GetRandomValue(0,20)/10.0f;
        g_crates[c]=(Box){ (Vector3){gx,h/2,gz}, (Vector3){1.6f,h,1.6f}, WHITE };
    }

    g_cam  =(Camera3D){ g_pos, Vector3Add(g_pos,(Vector3){0,0,-1}), (Vector3){0,1,0}, 70.0f, CAMERA_PERSPECTIVE };
    g_vmCam=(Camera3D){ (Vector3){0,0,0}, (Vector3){0,0,-1}, (Vector3){0,1,0}, 55.0f, CAMERA_PERSPECTIVE };

    LoadWeapon(0, "assets/rifle.glb", "../assets/rifle.glb", "M16A3 Rifle", "vm_tune.txt",
               VM_OFF0, VM_SCALE0, VM_YAW0, VM_PITCH0);
    LoadWeapon(1, "assets/shotgun.glb", "../assets/shotgun.glb", "Shotgun", "vm_tune_shotgun.txt",
               (Vector3){ -2.45f, -5.55f, 1.68f }, -1.0f, 180.0f, 0.0f);
    LoadWeapon(2, "assets/minigun.glb", "../assets/minigun.glb", "Minigun", "vm_tune_minigun.txt",
               (Vector3){ -2.45f, -5.55f, 1.68f }, -1.0f, 180.0f, 0.0f);
    LoadWeapon(3, "assets/lmg.glb", "../assets/lmg.glb", "LMG", "vm_tune_lmg.txt",
               (Vector3){ -2.45f, -5.55f, 1.68f }, -1.0f, 180.0f, 0.0f);
    LoadWeapon(4, "assets/sawnoff.glb", "../assets/sawnoff.glb", "Sawnoff", "vm_tune_sawnoff.txt",
               (Vector3){ -2.45f, -5.55f, 1.68f }, -1.0f, 180.0f, 0.0f);
    LoadWeapon(5, "assets/knife.glb", "../assets/knife.glb", "Knife", "vm_tune_knife.txt",
               (Vector3){ -2.45f, -5.55f, 1.68f }, -1.0f, 180.0f, 0.0f);
    g_numWeapons=6;
    ActivateWeapon(0);   // park on the rifle (also restores its saved framing)

    // Load the enemy (Mixamo walk rig) and spawn a starting wave.
    const char *enemyPath="assets/enemy.glb";
    if (!FileExists(enemyPath)) enemyPath="../assets/enemy.glb";
    if (FileExists(enemyPath)){
        g_enemy=LoadModel(enemyPath);
        g_enemyAnim=LoadModelAnimations(enemyPath,&g_enemyAnimN);
        for (int ci=0; ci<g_enemyAnimN; ci++){
            const char *nm=g_enemyAnim[ci].name;
            if      (strstr(nm,"walk"))   g_eWalk=ci;
            else if (strstr(nm,"idle"))   g_eIdle=ci;
            else if (strstr(nm,"run"))    g_eRun=ci;
            else if (strstr(nm,"hit"))    g_eHit=ci;
            else if (strstr(nm,"attack")) g_eAttack=ci;
            else if (strstr(nm,"death"))  g_eDeath=ci;
        }
        TraceLog(LOG_INFO,"enemy clips walk=%d idle=%d run=%d hit=%d attack=%d death=%d",g_eWalk,g_eIdle,g_eRun,g_eHit,g_eAttack,g_eDeath);
        g_hasEnemy=(g_enemy.meshCount>0);
        BoundingBox eb=GetModelBoundingBox(g_enemy);
        DebugLog("enemy","\"meshes\":%d,\"bones\":%d,\"anims\":%d,\"size\":[%.1f,%.1f,%.1f]",
                 g_enemy.meshCount, g_enemy.skeleton.boneCount, g_enemyAnimN,
                 eb.max.x-eb.min.x, eb.max.y-eb.min.y, eb.max.z-eb.min.z);
        if (!g_noEnemies) for (int i=0;i<5;i++) SpawnEnemy(i);   // start with 5 enemies (none in --no-enemies mode)
    } else DebugLog("enemy","\"error\":\"assets/enemy.glb not found\"");

#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(Frame,0,1);
#else
    int fc=0;
    while (!WindowShouldClose()){ Frame(); if (g_maxFrames>0 && ++fc>=g_maxFrames) break; }
#endif

    StashActiveTuning();
    for (int i=0;i<g_numWeapons;i++) SaveTune(g_weapons[i].tunePath,g_weapons[i].off,g_weapons[i].scale,g_weapons[i].yaw,g_weapons[i].pitch);
    DebugLog("shutdown","\"ok\":true");
    for (int i=0;i<g_numWeapons;i++) if (g_weapons[i].has){
        if (g_weapons[i].anim) UnloadModelAnimations(g_weapons[i].anim,g_weapons[i].animN);
        UnloadModel(g_weapons[i].model);
    }
    if (g_hasEnemy){ if (g_enemyAnim) UnloadModelAnimations(g_enemyAnim,g_enemyAnimN); UnloadModel(g_enemy); }
    UnloadModel(g_floor); UnloadModel(g_wall); UnloadModel(g_crate);
    UnloadTexture(g_floorTex); UnloadTexture(g_wallTex); UnloadTexture(g_crateTex);
    CloseWindow();
    return 0;
}
