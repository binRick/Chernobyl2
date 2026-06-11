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
#include <dirent.h>      // map-select menu: scan maps/*.map at startup

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
  #include <OpenGL/gl3.h>          // glClear(GL_DEPTH_BUFFER_BIT) for the viewmodel pass
#elif defined(__EMSCRIPTEN__)
  #include <GLES3/gl3.h>
#endif

#include "mapload.h"   // SPIKE: --map loads idTech3 (.map) brush geometry as a Model

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
void MacSetDockIcon(const char *path);   // macicon.m: Dock / Cmd-Tab icon (SetWindowIcon is a no-op on macOS)
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
// raylib trace-log filter: passes everything through to the default handler
// EXCEPT the harmless "Indices data converted from u32 to u16" warning that the
// glTF loader spams for every multi-thousand-vert model. Keeps the console clean
// without hiding genuine warnings/errors.
static void FilteredTraceLog(int logLevel, const char *text, va_list args){
    if (text && strstr(text, "converted from u32 to u16")) return;
    char buf[512];
    vsnprintf(buf, sizeof buf, text, args);
    const char *tag = (logLevel>=LOG_ERROR)?"ERROR: ":(logLevel==LOG_WARNING)?"WARNING: ":
                      (logLevel==LOG_INFO)?"INFO: ":(logLevel==LOG_DEBUG)?"DEBUG: ":"";
    fprintf(logLevel>=LOG_ERROR?stderr:stdout, "%s%s\n", tag, buf);
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
static Model g_map; static int g_hasMap=0; static const char *g_mapPath=NULL;  // SPIKE: --map <file.map>
static int g_mapPathOwned=0;   // 1 if g_mapPath was malloc'd by the picker (free on map switch); 0 if it's argv
static Shader g_scrollShader; static int g_scrollLoc=-1; static int g_hasScrollShader=0;  // animated UV scroll for liquids/warpzones

// ---- Player -----------------------------------------------------------------
static Vector3 g_pos   = { 0, EYE_H, 6 };
static float   g_yaw   = 0.0f;
static float   g_pitch = 0.0f;
static int     g_lookSet = 0;            // --look <yaw> <pitch>: aim the spawn camera (dev/map inspection)
static float   g_lookYaw = 0.0f, g_lookPitch = 0.0f;
static float   g_vy    = 0.0f;
static int     g_grounded = 1;
static int     g_devOverlay = 1;
static float   g_bob = 0.0f;
static float   g_eyeSmooth = 0.0f;   // view-height lag that absorbs stair-step snaps, decays to 0

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
static int             g_aIdle=0, g_aShoot=1, g_aReload=2, g_aWalk=-1;  // idle = hold last frame of "Take"; aWalk=-1 = no walk clip
static int             g_reloadSeq[6], g_reloadSeqN=0;  // active weapon's multi-phase reload clips
static int             g_reloadStep=-1;                 // -1 idle; >=0 index into g_reloadSeq
static int             g_reloading=0;                   // a reload (single or sequence) is in progress
static int             g_curAnim = 0;           // start in idle (Take, held at end)
static float           g_animT = 0.0f;          // frame cursor
static int             g_animOnce = 0;          // current clip is a one-shot (Reload)
static float           g_fireCd = 0.0f;
static float           g_recoil = 0.0f;         // 0..1 recoil kick on fire (decays); drives muzzle-up
static int             g_burstLeft = 0;         // rounds remaining in the current burst (burst weapons)
static float           g_spin = 0.0f;           // minigun barrel-spin rate 0..1
static float           g_mgSpinT = 0.0f;         // minigun: seconds the trigger's been held (spin-up timer)
static float           g_shake = 0.0f;           // 0..1 minigun "brain shake": screen jitter + edge vignette
static float           g_flameHeat = 0.0f;        // flamethrower warm-up: seconds the trigger's been held; below FLAME_WARMUP it spits raw fuel, above it ignites
#define FLAME_WARMUP 0.55f                         // warm-up before the flame catches: spray unignited liquid until then
#define MG_SPINUP 0.333f                          // barrel spins up for 1/3 s before bullets actually fire
static float           g_mgBarrelAngle = 0.0f;   // minigun barrel-spin angle: the barrel is a rigid (un-rigged) mesh,
                                                 // so it can't spin via the skeleton -- we rotate the geometry directly
#define MG_BARREL_MESH 1                          // minigun.glb mesh index of "barrels_minigun_0"
#define MG_IDLE_FRAME  0                          // 'allanims' frame to hold the arms at (a static grip)
static float           g_mgHeat = 0.0f;         // seconds of continuous minigun fire (5s -> overheat)
static int             g_mgLock = 0;            // minigun overheated: locked until trigger released
static int             g_mgFiring = 0;          // minigun was firing last frame (edge detect for cooldown sound)
static float           g_lmgPause = 0.0f;       // LMG: forced pause (s) after its fire sound ends
static int             g_lmgWasPlaying = 0;     // LMG: fire sound was playing last frame (edge detect)
static Camera3D        g_vmCam;
static int             g_inspect = 0;           // V: gun floats in the world
static float           g_savedMsg = 0.0f;       // >0: flash a "SAVED" confirmation
static int             g_tuneTarget = 0;        // tuning focus: 0 = whole viewmodel, 1 = gun-on-hand grip (B toggles, pinned guns only)
static float           g_spawnMsg = 0.0f;       // >0: flash a "SPAWN SET" confirmation
static int             g_menu = 0;              // ESC: options/pause menu open (frees cursor, pauses)
static int             g_paused = 0;            // P: pause the game (freezes the world, keeps mouse captured)
static int             g_fullscreen = 0;        // borderless monitor-fill on/off (set via the menu, persisted)
static int             g_quit = 0;              // menu Quit -> break the main loop
static Vector3         g_gunCentroid = { 0, 0, 0 };  // gun centroid (bind or posed), measured ONCE at load
static float           g_gunFitScale = 1.0f;         // inspect-view fit scale, measured ONCE at load
// Slots whose bind pose sits far from where the gun is actually drawn (posed):
// frame these from the POSED idle geometry. Most rigs (incl. every hand-tuned
// legacy weapon) have bind~=posed and stay on bind, so they're untouched. Set
// before the corresponding LoadWeapon call.
static int             g_posedBasis[16] = {0};

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
#define VM_ROLL0   0.0f
static Vector3 g_vmOff   = { -2.45f, -5.55f, 1.68f };
static float   g_vmScale = VM_SCALE0;
static float   g_vmYaw   = VM_YAW0;
static float   g_vmPitch = VM_PITCH0;
static float   g_vmRoll  = VM_ROLL0;

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
    int             aIdle, aShoot, aReload, aWalk;
    int             idleHold;                    // ready-pose anim frame to HOLD (-1 = default last frame); Z/X scrub it, saved to the tune file -- so the hands grip the gun instead of T-posing
    int             reloadSeq[6], reloadSeqN;   // multi-phase reload clips, in play order
    Vector3         centroid;
    float           fitScale;
    Vector3         off;  float scale, yaw, pitch, roll;     // live, persisted
    Vector3         off0; float scale0, yaw0, pitch0, roll0; // defaults for 0=reset
    const char     *label;
    const char     *tunePath;
    Sound           fireSnd;                     // per-weapon fire report
    int             hasSnd;                      // fireSnd loaded OK
    int             sndLoop;                     // 1 = loop while trigger held (auto); 0 = one-shot per shot
    int             burst;                       // >0: rounds per trigger pull (burst fire)
    int             autoReload;                  // 1: play the reload anim after each shot (pump)
    int             spinUp;                      // 1: minigun -- 5s fire cap, then cooldown sound + lockout
    int             soundGated;                  // 1: LMG -- fires while its sound plays, then a fixed pause
    int             flame;                       // 1: flamethrower -- continuous fire cone while the trigger is held
    int             melee;                       // 1: knife -- short-range arc swing on each click
    int             grenade;                     // 1: grenade launcher -- lobs an arcing grenade that explodes on impact
    int             pinGun;                      // 1: gun mesh is STATIC & detached from the arms -> draw it rigidly attached to handBone each frame
    int             handBone, handBoneL;         // right / left hand bones; the gun RIDES the right, -1 = not found
    Vector3         gunBindC;                    // bind-space centre of the static gun meshes
    Vector3         pinSeat;                     // bind-space point the gun centre is seated at: the PALM MIDPOINT (so it spans both hands), else the right palm
    Matrix          pinAlign;                    // auto-rotation: gun's principal (long) axis -> the palm-to-palm axis
    Vector3         pinOff; float pinScale, pinYaw, pinPitch, pinRoll;  // grip trim on top of the auto-seat, in hand-bind space (B-mode tunes it; saved to <tune>.pin)
    Vector3         armsROff;                    // minigun-style rigs: extra nudge applied to RIGHT-arm vertices only (baked into mesh.vertices; B-mode state 2)
    unsigned long long gunMask;                  // bit k = mesh k is a GUN part (unskinned OR rigid-bound to one bone) vs blended-skinned arms
    int             pinAuthored;                 // 1: raylib's bake reproduces the file's authored gun-in-hands placement -> keep it (no re-seat), just ride the hand
    int             playShoot;                   // 1: play the model's Shot clip on each shot (AK has a good one)
    float           fireCd;                      // per-shot cooldown override (0 -> default)
    Sound           auxSnd;                      // secondary sound: minigun spin-down, shotgun cock-between-shots
    int             hasAuxSnd;
} Weapon;
static Weapon g_weapons[16];
static int    g_curWeapon = 0;
static int    g_numWeapons = 0;
static int    g_audio = 0;                       // audio device ready (sounds load/play only when set)
static Sound  g_deathSnd[8];                      // one is played at random on every enemy kill
static int    g_nDeathSnd = 0;
static Sound  g_attackSnd;                         // played when an enemy lunges into an attack
static int    g_hasAttackSnd = 0;

static void SaveTune(const char *path, Vector3 off, float scale, float yaw, float pitch, float roll, int idleHold){
    FILE *f=fopen(path,"w"); if(!f) return;
    fprintf(f,"%.5f %.5f %.5f %.6f %.2f %.2f %.2f %d\n", off.x,off.y,off.z,scale,yaw,pitch,roll,idleHold);
    fclose(f);
}
static int LoadTune(const char *path, Vector3 *off, float *scale, float *yaw, float *pitch, float *roll, int *idleHold){
    FILE *f=fopen(path,"r"); if(!f) return 0;
    float ox,oy,oz,sc,yw,pt,rl=0.0f; int ih=-1;
    int n=fscanf(f,"%f %f %f %f %f %f %f %d",&ox,&oy,&oz,&sc,&yw,&pt,&rl,&ih);
    fclose(f);
    if (idleHold) *idleHold = (n>=8)?ih:-1;   // 8th value optional: old 7-value tunes keep the default
    if(n>=6){ *off=(Vector3){ox,oy,oz}; *scale=sc; *yaw=yw; *pitch=pt; *roll=(n>=7)?rl:0.0f; return 1; }
    return 0;
}
// Gun-pin grip tune (gun pose relative to the hand bone) -- kept in a sibling "<tune>.pin"
// file so it's separate from the whole-viewmodel transform.
static void SavePin(const char *tunePath, Vector3 off, float scale, float yaw, float pitch, float roll, Vector3 rOff){
    char p[280]; snprintf(p,sizeof p,"%s.pin",tunePath);
    FILE *f=fopen(p,"w"); if(!f) return;
    fprintf(f,"%.5f %.5f %.5f %.5f %.2f %.2f %.2f %.5f %.5f %.5f\n", off.x,off.y,off.z,scale,yaw,pitch,roll,rOff.x,rOff.y,rOff.z); fclose(f);
}
static void LoadPin(const char *tunePath, Vector3 *off, float *scale, float *yaw, float *pitch, float *roll, Vector3 *rOff){
    char p[280]; snprintf(p,sizeof p,"%s.pin",tunePath);
    FILE *f=fopen(p,"r"); if(!f) return;
    float ox,oy,oz,sc,yw,pt,rl,rx=0,ry=0,rz=0;
    int n=fscanf(f,"%f %f %f %f %f %f %f %f %f %f",&ox,&oy,&oz,&sc,&yw,&pt,&rl,&rx,&ry,&rz); fclose(f);
    if(n>=7){ *off=(Vector3){ox,oy,oz}; *scale=sc; *yaw=yw; *pitch=pt; *roll=rl; }
    if(rOff) *rOff = (n>=10) ? (Vector3){rx,ry,rz} : (Vector3){0,0,0};   // optional right-hand nudge (old 7-value pins keep zero)
}
// Custom player spawn (F2 saves your current pose; loaded at startup to override the
// map's spawn). Per-machine local content, like the weapon tune files. The save is
// keyed to the map's basename (spawn_<map>.txt) so a saved spawn for one map never
// leaks onto another -- the bind-mode pose for afterslime would otherwise dump the
// player into the void on every other .map. The no-map (built-in arena) case keeps
// the legacy spawn.txt name.
static const char *SpawnPath(char *buf, int sz){
    if (!g_mapPath){ snprintf(buf,sz,"spawn.txt"); return buf; }
    const char *b=strrchr(g_mapPath,'/'); b=b?b+1:g_mapPath;   // basename
    char stem[160]; snprintf(stem,sizeof stem,"%s",b);
    char *dot=strrchr(stem,'.'); if (dot) *dot=0;              // drop ".map"
    snprintf(buf,sz,"spawn_%s.txt",stem); return buf;
}
static void SaveSpawn(Vector3 pos, float yaw, float pitch){
    char path[224]; FILE *f=fopen(SpawnPath(path,sizeof path),"w"); if(!f) return;
    fprintf(f,"%.4f %.4f %.4f %.4f %.4f\n", pos.x,pos.y,pos.z,yaw,pitch); fclose(f);
}
static int LoadSpawn(Vector3 *pos, float *yaw, float *pitch){
    char path[224]; FILE *f=fopen(SpawnPath(path,sizeof path),"r"); if(!f) return 0;
    float x,y,z,yw,pt; int n=fscanf(f,"%f %f %f %f %f",&x,&y,&z,&yw,&pt); fclose(f);
    if(n==5){ *pos=(Vector3){x,y,z}; *yaw=yw; *pitch=pt; return 1; }
    return 0;
}
// Copy the live working-set tuning back into the current slot (call before a switch/quit).
static void StashActiveTuning(void){
    if (g_numWeapons<=0) return;
    Weapon *w=&g_weapons[g_curWeapon];
    w->off=g_vmOff; w->scale=g_vmScale; w->yaw=g_vmYaw; w->pitch=g_vmPitch; w->roll=g_vmRoll;
}
// Which side of the body does bone b belong to? Walk ancestry until a name carries a
// side prefix (handles unprefixed bones like the minigun's left 'palm_016', whose parent
// chain is L_*). +1 right, -1 left, 0 neutral (root/torso).
static int BoneSide(Model *m, int b){
    while (b>=0 && b<m->skeleton.boneCount){
        const char *bn=m->skeleton.bones[b].name; char low[40]; int j=0;
        for(;bn[j]&&j<39;j++){char c=bn[j]; low[j]=(c>='A'&&c<='Z')?c+32:c;} low[j]=0;
        if (strstr(low,"l_")==low || strstr(low,"left"))  return -1;
        if (strstr(low,"r_")==low || strstr(low,"right")) return  1;
        b=m->skeleton.bones[b].parent;
    }
    return 0;
}
// Shift only the RIGHT-arm vertices of the skinned arms meshes by dv (model space,
// baked straight into mesh.vertices -- the per-frame skinning pass re-uploads from
// there, so the edit shows immediately and composes with the whole-arms nudge).
// Used by minigun-style rigs where the two hands need seating independently.
static void ApplyArmsRDelta(Weapon *w, Vector3 dv){
    if (fabsf(dv.x)+fabsf(dv.y)+fabsf(dv.z) < 1e-9f) return;
    for (int k=0;k<w->model.meshCount && k<64;k++){
        Mesh *ms=&w->model.meshes[k];
        if ((w->gunMask>>k)&1ULL) continue;                       // arms meshes only
        if (!ms->boneIndices || !ms->boneWeights || !ms->vertices) continue;
        for (int v=0;v<ms->vertexCount;v++){
            int best=-1; float bw=0.0f;
            for (int c=0;c<4;c++){ float wt=ms->boneWeights[v*4+c]; if (wt>bw){ bw=wt; best=(int)ms->boneIndices[v*4+c]; } }
            if (best<0 || BoneSide(&w->model,best)!=1) continue;  // right-side verts only
            ms->vertices[v*3]+=dv.x; ms->vertices[v*3+1]+=dv.y; ms->vertices[v*3+2]+=dv.z;
        }
    }
}

// One-shot diagnostic for the pinned-gun path: after posing the model at the held
// frame, log (a) where raylib's OWN skinning matrix sends the wrist bind point,
// (b) where MY keyframe-derived skin matrix sends it, (c) where the most wrist-bound
// ARM VERTEX actually landed (mesh.animVertices = the rendered ground truth). If
// (a)==(b)==(c), gun and hand must coincide on screen; whichever differs is the lie.
static Matrix XformToMatrix(Transform t);      // fwd decl (defined near DrawPinnedGun)
static void DebugPinProbe(Weapon *w){
    if (!w->pinGun) return;                                       // only instrumented weapons
    if (w->handBone<0 || w->animN<=0 || !w->model.boneMatrices){  // log WHY it can't probe -- a NULL boneMatrices means arms never animate at all
        DebugLog("pinprobe","\"skip\":true,\"handBone\":%d,\"animN\":%d,\"boneMatricesNull\":%d",
                 w->handBone, w->animN, w->model.boneMatrices==NULL);
        return;
    }
    int hb=w->handBone;
    float hold=(w->idleHold>=0)?(float)w->idleHold:0.0f;
    ANIM_APPLY(w->model, w->anim[0], hold);                       // pose arms + boneMatrices at the held frame
    int kf=(int)hold; int nf=ANIM_FRAMES(w->anim[0]); if (kf<0) kf=0; if (nf>0 && kf>=nf) kf=nf-1;
    Vector3 pw=w->model.skeleton.bindPose[hb].translation;        // the wrist, bind space
    Vector3 raylibSays=Vector3Transform(pw, w->model.boneMatrices[hb]);
    Matrix mySkin=MatrixMultiply(MatrixInvert(XformToMatrix(w->model.skeleton.bindPose[hb])),
                                 XformToMatrix(w->anim[0].keyframePoses[kf][hb]));
    Vector3 mineSays=Vector3Transform(pw, mySkin);
    Vector3 posedKey=w->anim[0].keyframePoses[kf][hb].translation;
    // ground truth: the arm vertex most bound to the wrist, baked vs rendered
    Vector3 armBaked={0,0,0}, armPosed={0,0,0}; float bestW=0;
    for (int k=0;k<w->model.meshCount && k<64;k++){
        Mesh *ms=&w->model.meshes[k];
        if ((w->gunMask>>k)&1ULL) continue;                       // arms only
        if (!ms->boneWeights||!ms->boneIndices||!ms->animVertices) continue;
        for (int v=0;v<ms->vertexCount;v++)
            for (int c=0;c<4;c++)
                if ((int)ms->boneIndices[v*4+c]==hb && ms->boneWeights[v*4+c]>bestW){
                    bestW=ms->boneWeights[v*4+c];
                    armBaked=(Vector3){ms->vertices[v*3],ms->vertices[v*3+1],ms->vertices[v*3+2]};
                    armPosed=(Vector3){ms->animVertices[v*3],ms->animVertices[v*3+1],ms->animVertices[v*3+2]};
                }
    }
    DebugLog("pinprobe","\"hold\":%d,\"bindW\":[%.2f,%.2f,%.2f],\"keyPose\":[%.2f,%.2f,%.2f],\"raylibSkin\":[%.2f,%.2f,%.2f],\"mySkin\":[%.2f,%.2f,%.2f],\"armW\":%.2f,\"armBaked\":[%.2f,%.2f,%.2f],\"armPosed\":[%.2f,%.2f,%.2f]",
             kf, pw.x,pw.y,pw.z, posedKey.x,posedKey.y,posedKey.z, raylibSays.x,raylibSays.y,raylibSays.z,
             mineSays.x,mineSays.y,mineSays.z, bestW, armBaked.x,armBaked.y,armBaked.z, armPosed.x,armPosed.y,armPosed.z);
}

// Make slot n the live weapon: copy its model/clips/tuning into the globals.
static void ActivateWeapon(int n){
    g_curWeapon=n;
    Weapon *w=&g_weapons[n];
    g_gun=w->model; g_gunAnim=w->anim; g_gunAnimN=w->animN; g_hasGun=w->has;
    g_aIdle=w->aIdle; g_aShoot=w->aShoot; g_aReload=w->aReload; g_aWalk=w->aWalk;
    g_reloadSeqN=w->reloadSeqN; for(int i=0;i<w->reloadSeqN;i++) g_reloadSeq[i]=w->reloadSeq[i];
    g_reloadStep=-1; g_reloading=0;
    g_gunCentroid=w->centroid; g_gunFitScale=w->fitScale;
    g_vmOff=w->off; g_vmScale=w->scale; g_vmYaw=w->yaw; g_vmPitch=w->pitch; g_vmRoll=w->roll;
    g_curAnim=g_aIdle; g_animOnce=0; g_animT=0.0f; g_recoil=0.0f;
    g_burstLeft=0; g_spin=0.0f; g_mgBarrelAngle=0.0f; g_mgSpinT=0.0f; g_shake=0.0f; g_mgHeat=0.0f; g_mgLock=0; g_mgFiring=0; g_lmgPause=0.0f; g_lmgWasPlaying=0; g_flameHeat=0.0f;   // reset fire-mode state
    DebugPinProbe(w);    // pinned guns: log raylib-vs-mine wrist placement (no-op otherwise)
}
static void SwitchWeapon(int n){
    if (n<0 || n>=g_numWeapons || n==g_curWeapon || !g_weapons[n].has) return;
    // silence the weapon we're holstering so its loop doesn't bleed past the swap
    if (g_audio && g_weapons[g_curWeapon].hasSnd && IsSoundPlaying(g_weapons[g_curWeapon].fireSnd))
        StopSound(g_weapons[g_curWeapon].fireSnd);
    if (g_audio && g_weapons[g_curWeapon].hasAuxSnd && IsSoundPlaying(g_weapons[g_curWeapon].auxSnd))
        StopSound(g_weapons[g_curWeapon].auxSnd);
    StashActiveTuning();
    ActivateWeapon(n);
    DebugLog("weapon","\"slot\":%d,\"label\":\"%s\"", n, g_weapons[n].label);
}
// Measure a model's bbox CENTRE + max extent from its current vertex data.
// useAnim picks mesh.animVertices (the posed buffer raylib actually draws) over
// the bind-pose mesh.vertices. Returns a unit fallback for empty/vertexless rigs.
static void MeasureBBox(Model m, int useAnim, Vector3 *centre, float *maxDim){
    float lo[3]={1e30f,1e30f,1e30f}, hi[3]={-1e30f,-1e30f,-1e30f}; int any=0;
    for(int mi=0;mi<m.meshCount;mi++){ Mesh*me=&m.meshes[mi];
        float*vv=(useAnim&&me->animVertices)?me->animVertices:me->vertices; if(!vv)continue;
        for(int v=0;v<me->vertexCount;v++){ any=1; for(int k=0;k<3;k++){float c=vv[v*3+k]; if(c<lo[k])lo[k]=c; if(c>hi[k])hi[k]=c;} } }
    if(!any){ *centre=(Vector3){0,0,0}; *maxDim=1.0f; return; }
    *centre=(Vector3){(lo[0]+hi[0])*0.5f,(lo[1]+hi[1])*0.5f,(lo[2]+hi[2])*0.5f};
    *maxDim=fmaxf(hi[0]-lo[0],fmaxf(hi[1]-lo[1],hi[2]-lo[2]));
}
// Load a glb into slot, resolve idle/shoot/reload clips by name, load saved tuning.
static void LoadWeapon(int slot, const char *path, const char *alt,
                       const char *label, const char *tunePath,
                       Vector3 off0, float scale0, float yaw0, float pitch0, float roll0){
    Weapon *w=&g_weapons[slot];
    w->label=label; w->tunePath=tunePath;
    w->off0=off0; w->scale0=scale0; w->yaw0=yaw0; w->pitch0=pitch0; w->roll0=roll0;
    w->off=off0; w->scale=scale0; w->yaw=yaw0; w->pitch=pitch0; w->roll=roll0;
    w->aIdle=0; w->aShoot=1; w->aReload=2; w->has=0; w->anim=NULL; w->animN=0;
    const char *fp=path; if(!FileExists(fp)) fp=alt;
    if(!FileExists(fp)){ DebugLog("weapon","\"slot\":%d,\"error\":\"missing\"",slot); return; }
    w->model=LoadModel(fp);
    w->anim=LoadModelAnimations(fp,&w->animN);
    w->has=(w->model.meshCount>0);
    // Cull stray morph-target / blend-shape helper meshes: those ship with NO diffuse
    // texture AND the default white material, so raylib draws them as a white untextured
    // blob over the gun (the M16A3's "shape_pose" mesh -> "rifle looks unskinned"). Only
    // hide meshes that are BOTH untextured AND pure white -- legit flat-colour parts (the
    // flamethrower's tinted glass / status lights have real baseColorFactor) are kept.
    for (int k=0;k<w->model.meshCount;k++){
        int mi=w->model.meshMaterial[k]; if (mi<0) continue;
        MaterialMap dm=w->model.materials[mi].maps[MATERIAL_MAP_DIFFUSE];
        if (dm.texture.id==rlGetTextureIdDefault() && dm.color.r==255 && dm.color.g==255 && dm.color.b==255 && dm.color.a==255){
            w->model.meshes[k].vertexCount=0; w->model.meshes[k].triangleCount=0;   // draw nothing for this mesh
            DebugLog("weapon","\"slot\":%d,\"culledUntexturedMesh\":%d",slot,k);
        }
    }
    // Gun-pin: locate the right-hand bone so a detached static gun mesh can ride it
    // (set per-slot via pinGun). Prefer the RIGHT wrist/hand/palm; fall back to any.
    w->handBone=-1; w->handBoneL=-1; w->pinScale=1.0f; w->pinOff=(Vector3){0,0,0}; w->pinYaw=0; w->pinPitch=0; w->pinRoll=0;
    w->pinAlign=MatrixIdentity(); w->pinSeat=(Vector3){0,0,0};
    for (int b=0;b<w->model.skeleton.boneCount;b++){
        const char *bn=w->model.skeleton.bones[b].name; char low[40]; int j=0;
        for(;bn[j]&&j<39;j++){char c=bn[j]; low[j]=(c>='A'&&c<='Z')?c+32:c;} low[j]=0;
        if (!(strstr(low,"wrist")||strstr(low,"hand")||strstr(low,"palm"))) continue;
        int isL=(strstr(low,"l_")==low)||strstr(low,"left");
        int isR=(strstr(low,"r_")==low)||strstr(low,"right");
        if (isL){ if (w->handBoneL<0) w->handBoneL=b; }
        else if (isR){ if (w->handBone<0) w->handBone=b; }
        else if (w->handBone<0) w->handBone=b;            // unsided "hand" -> usable as the ride bone
    }
    // Which meshes are GUN parts? This raylib build gives EVERY mesh of a rigged model
    // bone data (static meshes get rigid-bound to a bone), so "boneCount==0" finds
    // nothing (the first pin attempt classified all 15 meshes as arms -> the pin was a
    // no-op). Real test: a gun part is unskinned OR rigid (every vertex 100%% on the
    // same single bone); the arms are BLENDED across many bones.
    w->gunMask=0ULL;
    for (int k=0;k<w->model.meshCount && k<64;k++){
        Mesh *ms=&w->model.meshes[k];
        if (ms->vertexCount<=0 || !ms->vertices) continue;
        int isGun=0;
        if (!ms->boneIndices || !ms->boneWeights) isGun=1;            // no skin data at all -> static gun part
        else {
            int rigid=1, rb=-2;                                       // rb: -2 unset, -1 unweighted, >=0 the single bone
            for (int v=0;v<ms->vertexCount && rigid;v++){
                int best=-1; float bw=0.0f;
                for (int c=0;c<4;c++){ float wt=ms->boneWeights[v*4+c]; if (wt>bw){ bw=wt; best=(int)ms->boneIndices[v*4+c]; } }
                int vb = (bw>=0.99f) ? best : ((bw<=0.01f) ? -1 : -3); // -3 = genuinely blended vertex
                if (vb==-3){ rigid=0; break; }
                if (rb==-2) rb=vb; else if (vb!=rb) rigid=0;          // spans multiple bones -> organic arms
            }
            isGun=rigid;
        }
        if (isGun) w->gunMask |= (1ULL<<k);
    }
    // Bind-space centre of the gun meshes. The pinned draw seats this point at the
    // palms, so the gun starts in the hands instead of wherever the artist parked it;
    // the .pin grip trim then nudges from there.
    w->gunBindC=(Vector3){0,0,0};
    { int nstat=0; Vector3 acc={0,0,0};
      for (int k=0;k<w->model.meshCount && k<64;k++){
        Mesh *ms=&w->model.meshes[k];
        if (!((w->gunMask>>k)&1ULL)) continue;
        Vector3 mn={1e9f,1e9f,1e9f}, mx={-1e9f,-1e9f,-1e9f};
        for (int v=0;v<ms->vertexCount;v++){
            float X=ms->vertices[v*3], Y=ms->vertices[v*3+1], Z=ms->vertices[v*3+2];
            if(X<mn.x)mn.x=X; if(Y<mn.y)mn.y=Y; if(Z<mn.z)mn.z=Z;
            if(X>mx.x)mx.x=X; if(Y>mx.y)mx.y=Y; if(Z>mx.z)mx.z=Z;
        }
        acc.x+=(mn.x+mx.x)*0.5f; acc.y+=(mn.y+mx.y)*0.5f; acc.z+=(mn.z+mx.z)*0.5f; nstat++;
      }
      if (nstat>0) w->gunBindC=(Vector3){acc.x/nstat,acc.y/nstat,acc.z/nstat};
      // Auto-seat + auto-align: the bind data shows the palms in their hold pose, so by
      // default seat the gun's CENTRE at the palm MIDPOINT and rotate its principal
      // (long) axis onto the palm-to-palm axis -- the gun then spans both hands like a
      // real two-handed hold instead of hanging centred on one palm.
      if (w->handBone>=0 && w->model.skeleton.bindPose){
        Vector3 R=w->model.skeleton.bindPose[w->handBone].translation;
        w->pinSeat=R;
        if (nstat>0 && w->handBoneL>=0){
            Vector3 Lp=w->model.skeleton.bindPose[w->handBoneL].translation;
            w->pinSeat=(Vector3){(R.x+Lp.x)*0.5f,(R.y+Lp.y)*0.5f,(R.z+Lp.z)*0.5f};
            double C[3][3]={{0,0,0},{0,0,0},{0,0,0}}; long nv=0;   // covariance of the gun verts
            for (int k=0;k<w->model.meshCount && k<64;k++){
                Mesh *ms=&w->model.meshes[k];
                if (!((w->gunMask>>k)&1ULL)) continue;
                for (int v=0;v<ms->vertexCount;v++){
                    double dx=ms->vertices[v*3]-w->gunBindC.x, dy=ms->vertices[v*3+1]-w->gunBindC.y, dz=ms->vertices[v*3+2]-w->gunBindC.z;
                    C[0][0]+=dx*dx; C[0][1]+=dx*dy; C[0][2]+=dx*dz;
                    C[1][1]+=dy*dy; C[1][2]+=dy*dz; C[2][2]+=dz*dz; nv++;
                }
            }
            C[1][0]=C[0][1]; C[2][0]=C[0][2]; C[2][1]=C[1][2];
            if (nv>0){
                double ax=1.0,ay=0.31,az=0.17;                      // power iteration -> dominant eigenvector = the gun's long axis
                for (int it=0;it<32;it++){
                    double nx=C[0][0]*ax+C[0][1]*ay+C[0][2]*az;
                    double ny=C[1][0]*ax+C[1][1]*ay+C[1][2]*az;
                    double nz=C[2][0]*ax+C[2][1]*ay+C[2][2]*az;
                    double L2=sqrt(nx*nx+ny*ny+nz*nz); if (L2<1e-12) break;
                    ax=nx/L2; ay=ny/L2; az=nz/L2;
                }
                Vector3 u={(float)ax,(float)ay,(float)az};
                Vector3 vdir=Vector3Normalize(Vector3Subtract(Lp,R));   // rear (right) palm -> front (left) palm
                if (Vector3DotProduct(u,vdir)<0.0f) u=Vector3Negate(u);
                Vector3 axv=Vector3CrossProduct(u,vdir); float al=Vector3Length(axv);
                float dot=Vector3DotProduct(u,vdir); if(dot>1)dot=1; if(dot<-1)dot=-1;
                if (al>1e-4f) w->pinAlign=MatrixRotate(Vector3Scale(axv,1.0f/al), acosf(dot));
            }
        }
      }
      // Measured from grenadelauncher.glb itself: the AUTHORED scene already has the
      // launcher in the hands (gun centre [0.0,11.0,19.9] -- 9.3 units from the palm
      // midpoint, the exact grip the hand animation was authored around). If raylib's
      // bake lands the gun there, the authored placement IS the grip: keep it verbatim
      // and just ride it on the hand bone, instead of re-seating by centroid/PCA.
      { Vector3 authoredC={0.02f,11.0f,19.88f};
        w->pinAuthored = (nstat>0 && w->handBone>=0 && Vector3Distance(w->gunBindC,authoredC)<3.0f); }
      DebugLog("gunpin","\"slot\":%d,\"gunMeshes\":%d,\"mask\":%llu,\"handR\":%d,\"handL\":%d,\"authored\":%d,\"gunC\":[%.1f,%.1f,%.1f],\"seat\":[%.1f,%.1f,%.1f]",
               slot, nstat, w->gunMask, w->handBone, w->handBoneL, w->pinAuthored, w->gunBindC.x, w->gunBindC.y, w->gunBindC.z, w->pinSeat.x, w->pinSeat.y, w->pinSeat.z);
    }
    LoadPin(w->tunePath,&w->pinOff,&w->pinScale,&w->pinYaw,&w->pinPitch,&w->pinRoll,&w->armsROff);
    ApplyArmsRDelta(w, w->armsROff);   // bake the saved right-hand nudge into the arm verts (no-op when zero)
    // Resolve clips by name with priorities (first match wins per role): a real
    // "idle" beats take/draw/hold; "shot" counts as a fire clip; the plain
    // "reload" beats variants like reload_full. Handles e.g. AK_Idle/Shot/Reload.
    int rIdle=-1, rIdleAlt=-1, rShoot=-1, rReload=-1, rWalk=-1;
    for (int i=0;i<w->animN;i++){
        const char *nm=w->anim[i].name; char low[64]; int j=0;
        for (; nm[j] && j<63; j++){ char c=nm[j]; if(c>='A'&&c<='Z') c+=32; low[j]=c; }
        low[j]=0;
        if (strstr(low,"idle")){ if(rIdle<0) rIdle=i; }
        else if (strstr(low,"take")||strstr(low,"draw")||strstr(low,"weild")||strstr(low,"wield")||strstr(low,"hold")){ if(rIdleAlt<0) rIdleAlt=i; }
        if ((strstr(low,"shoot")||strstr(low,"fire")||strstr(low,"shot")) && rShoot<0) rShoot=i;
        if (strstr(low,"reload") && rReload<0) rReload=i;
        if ((strstr(low,"walk")||strstr(low,"run")) && rWalk<0) rWalk=i;
    }
    w->aWalk = (rWalk>=0)?rWalk:-1;   // -1 = no walk clip (most guns); set below after aIdle resolves
    w->aIdle  = (rIdle>=0)?rIdle:(rIdleAlt>=0?rIdleAlt:0);
    w->aShoot = (rShoot>=0)?rShoot:w->aIdle;
    w->aReload= (rReload>=0)?rReload:w->aIdle;
    // Multi-phase reload: if the rig has the named phases, play them in order
    // (e.g. shotgun pump: prep -> load shell -> load last -> recover). Falls
    // back to the single aReload clip when these aren't present.
    w->reloadSeqN=0;
    const char *phases[]={"priortoreload","reloadone","reloadlastone","postfire"};
    for (int p=0;p<4 && w->reloadSeqN<6;p++){
        for (int i=0;i<w->animN;i++){
            const char *nm=w->anim[i].name; char low[64]; int j=0;
            for (; nm[j] && j<63; j++){ char c=nm[j]; if(c>='A'&&c<='Z') c+=32; low[j]=c; }
            low[j]=0;
            if (strstr(low,phases[p])){ w->reloadSeq[w->reloadSeqN++]=i; break; }
        }
    }
    if (w->aShoot>=w->animN)  w->aShoot=w->aIdle;    // unresolved -> valid clip
    if (w->aReload>=w->animN) w->aReload=w->aIdle;   // (aReload==aIdle means "no reload clip")
    // Framing basis. raylib draws the POSED mesh (animVertices), but some FPS rigs
    // ship a bind pose that sits somewhere completely different (the MP5's bind
    // bbox is centred ~44k units off-origin, so recentering on it draws the gun
    // into empty space and it vanishes). For flagged slots, pose to idle frame 0
    // and frame from that; everyone else keeps the bind bbox they were tuned
    // against. Either way the recenter lands w->centroid at the viewmodel offset.
    Vector3 cc; float md;
    if (g_posedBasis[slot] && w->animN>0 && w->aIdle>=0 && w->aIdle<w->animN){
        UpdateModelAnimation(w->model, w->anim[w->aIdle], 0);
        MeasureBBox(w->model,1,&cc,&md);     // POSED idle geometry
    } else {
        MeasureBBox(w->model,0,&cc,&md);     // bind pose
    }
    w->centroid=cc;
    w->fitScale=(md>0.001f)?1.5f/md:1.0f;
    // Auto framing (passed scale0<=0): land the centroid dead ahead, slightly
    // down, scaled so the model is ~0.8 units (lower third). A saved tune overrides.
    if (scale0<=0.0f){
        float as=(md>0.001f)?0.8f/md:0.0004f;
        w->scale0=as; w->scale=as;
        w->off0=(Vector3){ 0.20f, -0.35f, -1.30f }; w->off=w->off0;
        w->yaw0=yaw0; w->pitch0=pitch0; w->roll0=roll0;  // orientation still a guess; user rotates
    }
    w->idleHold=-1;
    LoadTune(w->tunePath,&w->off,&w->scale,&w->yaw,&w->pitch,&w->roll,&w->idleHold);
    DebugLog("weapon","\"slot\":%d,\"label\":\"%s\",\"bones\":%d,\"anims\":%d,\"idle\":%d,\"shoot\":%d,\"reload\":%d,\"bbox\":%.1f,\"scale\":%.6f,\"centroid\":[%.1f,%.1f,%.1f]",
             slot,label,w->model.skeleton.boneCount,w->animN,w->aIdle,w->aShoot,w->aReload,md,w->scale,w->centroid.x,w->centroid.y,w->centroid.z);
}

// Attach a fire sound to a weapon slot. loop=1 -> sustained machine-gun fire
// (held in Update); loop=0 -> one blast per shot (played in Fire). No-op until
// the audio device is up, and silently skips a missing file (game still runs).
static void LoadWeaponSound(int slot, const char *path, const char *alt, int loop){
    if (!g_audio || slot<0 || slot>=16) return;
    Weapon *w=&g_weapons[slot];
    const char *fp=path; if(!FileExists(fp)) fp=alt;
    if(!FileExists(fp)){ DebugLog("wsound","\"slot\":%d,\"error\":\"missing\",\"path\":\"%s\"",slot,JStr(path)); return; }
    w->fireSnd=LoadSound(fp);
    w->hasSnd=(w->fireSnd.frameCount>0);
    w->sndLoop=loop;
    DebugLog("wsound","\"slot\":%d,\"path\":\"%s\",\"frames\":%u,\"loop\":%d",slot,JStr(fp),w->fireSnd.frameCount,loop);
}

// Secondary per-weapon sound (minigun spin-down, shotgun cock-between-shots).
static void LoadWeaponAux(int slot, const char *path, const char *alt){
    if (!g_audio || slot<0 || slot>=16) return;
    Weapon *w=&g_weapons[slot];
    const char *fp=path; if(!FileExists(fp)) fp=alt;
    if(!FileExists(fp)) return;
    w->auxSnd=LoadSound(fp); w->hasAuxSnd=(w->auxSnd.frameCount>0);
}

// ---- Tracers + impact sparks ------------------------------------------------
typedef struct { Vector3 a, b; float life; } Tracer;
typedef struct { Vector3 pos, vel; float life, life0, size, rest; Color col; int blood, lasting, pool; float ix, iz; unsigned int seed; } Spark; // rest=floor Y; blood=1 draws as a liquid streak; lasting=1 settles into spatter; pool=1 is a flat continuous stain at the body's feet; seed=stable jitter
#define MAX_TRACERS 32
#define MAX_SPARKS  4096
static Tracer g_tracers[MAX_TRACERS];
static Spark  g_sparks[MAX_SPARKS];

// Scorch decals: persistent soot marks the flamethrower burns onto whatever it hits.
// Each is a cluster of flat dark discs lying on the surface (oriented to its normal).
typedef struct { Vector3 pos, n; float r; unsigned int seed; int active; } Scorch;
#define MAX_SCORCH 192
static Scorch g_scorch[MAX_SCORCH];
static int    g_scorchNext=0;
static float  g_scorchT=0.0f;            // throttle so the flame lays marks at a steady rate, not every frame
static void AddScorch(Vector3 p, Vector3 n, float r){
    g_scorch[g_scorchNext]=(Scorch){ p, Vector3Normalize(n), r, (unsigned int)GetRandomValue(1,1<<30), 1 };
    g_scorchNext=(g_scorchNext+1)%MAX_SCORCH;
}

// Minigun barrel smoke/steam: soft camera-facing billboards that rise, expand and fade.
// hot=1 firing (darker, faster gunsmoke); hot=0 spin-down/cooling (white steam).
typedef struct { Vector3 pos, vel; float life, life0, sz0, sz1; int hot; int flame; int liquid; } Smoke;
#define MAX_SMOKE 384
static Smoke      g_smoke[MAX_SMOKE];
static Texture2D  g_smokeTex;            // soft radial-gradient puff
static int        g_hasSmokeTex = 0;
static void SpawnSmoke(Vector3 o, int hot){
    for (int s=0;s<MAX_SMOKE;s++){
        if (g_smoke[s].life>0) continue;
        Smoke p={0};
        p.pos=o;
        p.pos.x += GetRandomValue(-2,2)/100.0f; p.pos.y += GetRandomValue(-2,2)/100.0f; p.pos.z += GetRandomValue(-2,2)/100.0f; // tight emission point
        p.vel=(Vector3){ GetRandomValue(-10,10)/100.0f, 0.30f+GetRandomValue(0,30)/100.0f, GetRandomValue(-10,10)/100.0f };     // finer column, less spread
        if (hot) p.vel.y += 0.30f;                              // gunsmoke puffs up faster
        p.life0 = (hot? 0.50f:1.00f) + GetRandomValue(0,55)/100.0f;
        p.life=p.life0;
        p.sz0 = 0.018f + GetRandomValue(0,2)/100.0f;            // small, fine particles at the muzzle
        p.sz1 = (hot?0.12f:0.20f) + GetRandomValue(0,8)/100.0f; // billow out modestly as they age
        p.hot = hot;
        g_smoke[s]=p; return;
    }
}
// Flamethrower jet: bright billboards shot FORWARD along the aim, with spread, that
// decelerate and curl up as they age. Reuses the smoke pool/draw (flame flag picks
// the additive fire gradient). o=muzzle, fwd=aim direction.
static void SpawnFlame(Vector3 o, Vector3 fwd){
    for (int s=0;s<MAX_SMOKE;s++){
        if (g_smoke[s].life>0) continue;
        Smoke p={0};
        p.pos=o;
        float spd=8.0f+GetRandomValue(0,50)/10.0f;
        Vector3 jit={GetRandomValue(-30,30)/100.0f,GetRandomValue(-30,30)/100.0f,GetRandomValue(-30,30)/100.0f};
        p.vel=Vector3Add(Vector3Scale(fwd,spd), Vector3Scale(jit,9.0f));   // strong lateral spread -> a fat, fanning gout
        p.vel.y += 0.6f;                                       // the jet licks upward as it travels
        p.life0=0.34f+GetRandomValue(0,22)/100.0f; p.life=p.life0;  // short-lived: a lick of fire, not lingering smoke
        p.sz0=0.16f+GetRandomValue(0,8)/100.0f;
        p.sz1=1.05f+GetRandomValue(0,55)/100.0f;               // billow large as it burns out -> a wide wall of fire
        p.hot=1; p.flame=1;
        g_smoke[s]=p; return;
    }
}
// Unignited fuel: the flamethrower's warm-up spit -- a pale liquid jet that arcs DOWN
// under gravity (heavy, not buoyant) and does no damage. Same nozzle/aim as the flame.
static void SpawnLiquid(Vector3 o, Vector3 fwd){
    for (int s=0;s<MAX_SMOKE;s++){
        if (g_smoke[s].life>0) continue;
        Smoke p={0};
        p.pos=o;
        float spd=6.0f+GetRandomValue(0,30)/10.0f;             // a bit slower/heavier than lit flame
        Vector3 jit={GetRandomValue(-12,12)/100.0f,GetRandomValue(-12,12)/100.0f,GetRandomValue(-12,12)/100.0f};
        p.vel=Vector3Add(Vector3Scale(fwd,spd), Vector3Scale(jit,3.0f));
        p.life0=0.45f+GetRandomValue(0,25)/100.0f; p.life=p.life0;  // lives a touch longer (it falls, doesn't burn up)
        p.sz0=0.05f+GetRandomValue(0,4)/100.0f;
        p.sz1=0.16f+GetRandomValue(0,10)/100.0f;               // a fine mist, doesn't billow like fire
        p.hot=0; p.liquid=1;
        g_smoke[s]=p; return;
    }
}
static void UpdateSmoke(float dt){
    for (int s=0;s<MAX_SMOKE;s++){ Smoke *p=&g_smoke[s]; if (p->life<=0) continue;
        p->life -= dt;
        if (p->liquid){                                        // unignited fuel: heavy, falls + air drag
            p->vel.y -= 9.0f*dt;
            p->vel = Vector3Scale(p->vel, 1.0f-2.4f*dt);
        } else {
            p->vel.y += 0.5f*dt;                               // smoke/flame: buoyancy
            if (p->flame) p->vel = Vector3Scale(p->vel, 1.0f-3.2f*dt);  // the fire jet decelerates fast (air drag)
            else { p->vel.x *= (1.0f-1.6f*dt); p->vel.z *= (1.0f-1.6f*dt); } // smoke: horizontal drag only
        }
        p->pos = Vector3Add(p->pos, Vector3Scale(p->vel,dt));
    }
}
static void DrawSmoke(Camera3D cam){
    if (!g_hasSmokeTex) return;
    rlDisableDepthMask();                                      // particles blend, don't occlude each other
    for (int s=0;s<MAX_SMOKE;s++){ Smoke *p=&g_smoke[s]; if (p->life<=0 || p->flame) continue;
        float age=1.0f-p->life/p->life0;                       // 0..1
        float sz=p->sz0+(p->sz1-p->sz0)*age;
        float fade=p->life/p->life0; fade*=fade;               // ease-out fade
        Color tint;
        if (p->liquid) tint=(Color){210,200,150,(unsigned char)(fade*150)};  // pale amber atomised fuel
        else { unsigned char g=p->hot?90:205; tint=(Color){g,g,g,(unsigned char)(fade*(p->hot?150:120))}; } // gunsmoke grey vs white steam
        DrawBillboard(cam, g_smokeTex, p->pos, sz, tint);
    }
    // Flame pass: additive so overlapping licks build into a bright hot core, and a
    // young->old colour ramp (white-yellow -> orange -> deep red) that reads as fire.
    BeginBlendMode(BLEND_ADDITIVE);
    for (int s=0;s<MAX_SMOKE;s++){ Smoke *p=&g_smoke[s]; if (p->life<=0 || !p->flame) continue;
        float age=1.0f-p->life/p->life0;                       // 0..1
        float sz=p->sz0+(p->sz1-p->sz0)*age;
        float fade=p->life/p->life0;                           // linear fade-out
        unsigned char a=(unsigned char)(fade*200.0f);
        Color tint = (age<0.25f) ? (Color){255,240,180,a}     // ignition: hot white-yellow
                   : (age<0.60f) ? (Color){255,140,40,a}      // body: orange
                                 : (Color){170,35,20,a};       // dying: deep red
        DrawBillboard(cam, g_smokeTex, p->pos, sz, tint);
    }
    EndBlendMode();
    rlEnableDepthMask();
}
// Soot decals: each scorch is a ragged cluster of flat near-black discs lying ON the
// burned surface (built in the surface's tangent plane, oriented to its normal), so
// overlapping marks build up grime. Depth-tested with the world so walls occlude them.
static void DrawScorch(void){
    for (int i=0;i<MAX_SCORCH;i++){
        Scorch *s=&g_scorch[i]; if (!s->active) continue;
        Vector3 n=s->n;
        Vector3 up = (fabsf(n.y)>0.9f) ? (Vector3){1,0,0} : (Vector3){0,1,0};
        Vector3 T=Vector3Normalize(Vector3CrossProduct(up,n));
        Vector3 B=Vector3CrossProduct(n,T);
        unsigned int sd=s->seed?s->seed:1u;
        for (int b=0;b<7;b++){                                  // overlapping discs -> a sooty, irregular smudge
            unsigned int h=sd*2654435761u + (unsigned int)b*2246822519u; h^=h>>13; h*=0x9E3779B1u;
            float t1=(float)((h>>3)&1023)/1023.0f, t2=(float)((h>>13)&1023)/1023.0f, t3=(float)((h>>23)&255)/255.0f;
            float a=t1*6.2831853f, rr=sqrtf(t2)*s->r;
            Vector3 c=Vector3Add(s->pos, Vector3Add(Vector3Scale(T,cosf(a)*rr), Vector3Scale(B,sinf(a)*rr)));
            float dr=s->r*(0.30f+0.22f*t3)*(1.0f-0.4f*(rr/s->r));   // solid centre, smaller flecks toward the edge
            Vector3 p0=Vector3Add(c, Vector3Scale(n, 0.012f));     // float just off the surface (no z-fight)
            Vector3 p1=Vector3Add(c, Vector3Scale(n, 0.028f));
            DrawCylinderEx(p0,p1, dr,dr, 10, (Color){18,15,13,(unsigned char)(110+(int)(t3*60))});
        }
    }
}

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
#define ENEMY_MOVE_R       0.48f      // movement collision radius: kept just BELOW NAV_WALLR (fits everywhere nav routes) but near the visual width so the body does not poke through walls
#define ENEMY_HANDS_R      0.70f      // body+arms reach: a stuck/idling enemy eases back until walls are this far, so its animated hands stop poking through
                                      // fits everywhere the nav graph routes it (no jamming in tight cells / on stairs)
#define ENEMY_HEIGHT       1.85f
#define ENEMY_YAW_OFFSET   0.0f       // tune if they face the wrong way
#define ENEMY_TOUCH_DMG    18.0f      // player hp/sec when an enemy is in melee
#define ENEMY_ATTACK_RANGE  1.8f       // enters attack/melee within this range
// Headshot zone: a narrower box at the TOP of the enemy silhouette. A shot
// whose aim ray pierces it deals HEADSHOT_MULT x damage. Sized for the Mixamo
// humanoid (~1.85m): the head/upper-neck sits in the top ~0.32m, narrower than
// the 0.5 body radius. 3x of ENEMY_DMG_PER_SHOT (34) = 102 >= ENEMY_HP, so a
// clean headshot one-shot-kills a full-HP enemy (the classic FPS payoff).
#define HEAD_ZONE_H        0.32f      // vertical extent of the head box (down from the top)
#define HEAD_RADIUS        0.22f      // half-width of the head box (tighter than the body)
#define HEADSHOT_MULT      3.0f       // headshot damage multiplier
// Flamethrower (slot 7): a continuous cone of fire. Burns any enemy inside the
// cone + range while the trigger is held, no per-shot cooldown.
#define FLAME_RANGE        8.5f       // reach of the fire cone (units)
#define FLAME_COS          0.80f      // cos of the half-angle (~37deg): a broad billowing gout, not a thin jet
#define FLAME_DPS          85.0f      // damage/sec to an enemy fully in the cone (~1.2s to kill at 100hp)
// Knife (slot 8): a short, wide melee swing on each click.
#define KNIFE_RANGE        2.6f       // lunge reach (units)
#define KNIFE_COS          0.55f      // cos half-angle (~57deg): a wide slash, forgiving to aim
#define KNIFE_DMG          75.0f      // per swing; two slashes (or a clean one on a softened enemy) kills
#define KNIFE_CD           0.42f      // seconds between swings (the slash cadence)
#define MAX_BURN 8       // localised flame-char marks per body (height up the body + how dark/wide that spot is)
typedef struct { Vector3 pos; float hp; int state; int clip; float animT; float deathT; float hitT; float roarT; float drawY; float stuckT; float embedT; float fallV; float burnY[MAX_BURN]; float burnR[MAX_BURN]; int burnN; } Enemy; // state: 0 dead,1 alive,2 dying; burnY/burnR/burnN = soot marks at the body heights the flame actually touched (NOT a whole-body tint); roarT=attack-roar cooldown; drawY=smoothed render height; stuckT=time chasing-but-not-moving (->idle); embedT=time wedged inside a wall (->respawn); fallV=render-only fall velocity while dropping off a ledge
static Enemy           g_enemies[MAX_ENEMIES];
#define MAX_CORPSES 32
typedef struct { Vector3 pos; float yaw; int active; float burnY[MAX_BURN]; float burnR[MAX_BURN]; int burnN; } Corpse;   // lasting dead bodies, posed at the death-anim end; carries the soot marks from when it was alive
static Corpse          g_corpses[MAX_CORPSES];
static int             g_corpseNext = 0;
static Model           g_enemy;
static ModelAnimation *g_enemyAnim = NULL;
static int             g_enemyAnimN = 0;
static int g_eWalk=0, g_eIdle=0, g_eRun=0, g_eHit=0, g_eAttack=0, g_eDeath=0; // clips resolved by name at load
static int g_eDeathFrame=0;   // frame to freeze the death clip at: its LAST keyframe often loops back to the standing
                              // start pose, so corpses must settle on an earlier "body on the floor" frame instead
static int             g_hasEnemy = 0;
static int             g_kills = 0;
static int             g_headshots = 0;        // running headshot-kill tally (HUD)
static float           g_hsFlash = 0.0f;       // >0: flash "HEADSHOT!" near the crosshair
static float           g_playerHp = 100.0f;
static int             g_godMode = 1;           // dev stage: invulnerable (no death, no "YOU DIED"). G toggles.
static int             g_dead = 0;              // player death state: world frozen, waiting on a respawn input
static float           g_deadT = 0.0f;          // seconds since death (brief delay before respawn is allowed)
static float           g_hurt = 0.0f;           // 0..1 red damage flash, spikes on hp loss then decays
static float           g_prevHp = 100.0f;       // last frame's hp, to detect a drop -> trigger the hurt flash
static int             g_noclip = 0;            // map mode: F toggles free-fly / noclip vs FPS collision
static Vector3         g_spawnResetPos = {0,0,0};  // where to drop the player back to (fell off the map / new map)
static float           g_voidY = -1e9f;            // below this Y the player has fallen out of the world
static int             g_returnToMenu = 0;         // ESC menu "Select Map" -> break the game loop back to the picker

static void EnemyArm(int i, Vector3 pos){       // common: bring enemy i to life at pos
    g_enemies[i].pos=pos; g_enemies[i].hp=ENEMY_HP; g_enemies[i].state=1;
    g_enemies[i].animT=(float)GetRandomValue(0,40); g_enemies[i].deathT=0;
    g_enemies[i].hitT=0; g_enemies[i].clip=g_eRun; g_enemies[i].roarT=0; g_enemies[i].drawY=pos.y;
    g_enemies[i].stuckT=0; g_enemies[i].embedT=0; g_enemies[i].fallV=0.0f; g_enemies[i].burnN=0;
}
// A spawn spot is good only if it's near the player's level, inside the player's
// walkable nav region (not sealed off behind a wall), AND has body clearance so the
// enemy isn't jammed in a corner where it can't move (the random point can sit nearer
// a wall than the nav cell centre, so re-check the actual sphere here).
static int EnemySpawnOK(float x, float fy, float z){
    return fabsf(fy-(g_pos.y-EYE_H))<6.0f
        && MapNavReachable((Vector3){x,fy,z})
        && !MapSphereHitsWall((Vector3){x,fy+0.9f,z},ENEMY_MOVE_R);   // body fits (same radius it moves with)
}
// Map spawn: a ring around the player, dropped onto the floor under each spot.
// Biased toward where the player is FACING so enemies appear in front, not behind.
static void SpawnEnemyMap(int i){
    float ahead = 1.5708f - g_yaw;                       // player's look direction in the ring's angle space
    for (int t=0;t<40;t++){
        float ang = ahead + GetRandomValue(-115,115)/100.0f; // within ~±66 deg of straight ahead
        float dist=6.0f+GetRandomValue(0,90)/10.0f;          // 6..15 units out
        float x=g_pos.x+cosf(ang)*dist, z=g_pos.z+sinf(ang)*dist;
        float top=g_pos.y+8.0f;
        float fd=MapRayNearest((Vector3){x,top,z},(Vector3){0,-1,0},80.0f);
        if (fd>0){ float fy=top-fd; if (EnemySpawnOK(x,fy,z)){ EnemyArm(i,(Vector3){x,fy,z}); return; } }
    }
    // ring failed: scan outward from the player for any reachable, roomy cell.
    for (int rad=2; rad<14; rad++) for (int a=0;a<16;a++){
        float ang=a*0.3927f, x=g_pos.x+cosf(ang)*rad, z=g_pos.z+sinf(ang)*rad;
        float top=g_pos.y+8.0f, fd=MapRayNearest((Vector3){x,top,z},(Vector3){0,-1,0},80.0f);
        if (fd>0){ float fy=top-fd; if (EnemySpawnOK(x,fy,z)){ EnemyArm(i,(Vector3){x,fy,z}); return; } }
    }
    // nothing valid near the player (e.g. player is off the nav mesh in a tight nook):
    // drop the enemy on a RANDOM walkable nav cell -- a guaranteed collision-free,
    // navigable spot -- instead of blindly next to the player (which may be inside a wall).
    if (g_navOK){
        for (int t=0;t<400;t++){
            int cx=GetRandomValue(0,g_navNX-1), cz=GetRandomValue(0,g_navNZ-1), ci=cz*g_navNX+cx;
            if (g_navNL[ci]>0){
                int l=GetRandomValue(0,g_navNL[ci]-1);   // any storey in this column is a valid, navigable spot
                EnemyArm(i,(Vector3){ g_navMn.x+(cx+0.5f)*g_navCS, g_navFloor[ci*NAV_MAXL+l], g_navMn.z+(cz+0.5f)*g_navCS });
                return;
            }
        }
    }
    EnemyArm(i,(Vector3){g_pos.x, g_pos.y-EYE_H, g_pos.z});        // absolute fallback: the player's own (occupied) spot
}
static void SpawnEnemy(int i){
    if (g_hasMap && g_mapCol.ready){ SpawnEnemyMap(i); return; }
    // arena: a random spot near a wall, away from the player
    float ang=(float)GetRandomValue(0,628)/100.0f;
    float dist=ARENA*0.6f + GetRandomValue(0,(int)(ARENA*0.3f));
    EnemyArm(i,(Vector3){ cosf(ang)*dist, 0, sinf(ang)*dist });
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
        Spark sp={0}; sp.pos=p; sp.vel=v; sp.life=lf; sp.life0=lf; sp.size=0.04f; sp.rest=p.y; sp.blood=0;
        sp.col=(Color){255,(unsigned char)(200+GetRandomValue(0,55)),60,255};
        g_sparks[s]=sp;
        break;
    }
}

// Detailed blood burst: a spray of crimson droplets of varied size flung along
// the bullet's travel (dir) in a wide cone, plus heavier slow "gobs" every few
// particles. Gravity + fade are handled by the shared spark integrator/draw.
static void SpawnBlood(Vector3 p, Vector3 dir, float intensity, float floorY) {
    int n=(int)(200*intensity); if (n>900) n=900;          // headshots spray harder (capped)
    for (int i=0;i<n;i++) for (int s=0;s<MAX_SPARKS;s++){
        if (g_sparks[s].life>0) continue;
        float spread=3.6f;
        Vector3 v={ dir.x*4.5f + GetRandomValue(-100,100)/100.0f*spread,
                    dir.y*4.5f + GetRandomValue(-100,100)/100.0f*spread + 1.7f,
                    dir.z*4.5f + GetRandomValue(-100,100)/100.0f*spread };
        int gob=(i%5==0);                                  // heavy gobs: slower, redder, and LASTING (settle into spatter)
        if (gob){ v.x*=0.45f; v.y=v.y*0.45f+0.4f; v.z*=0.45f; }
        float lf = 1.1f + GetRandomValue(0,120)/100.0f;
        // small droplets: in flight they render as thin streaks, not balls. Settled
        // gobs leave only FINE spatter dots -- the continuous pool below carries the volume.
        float sz = gob ? (0.016f+GetRandomValue(0,10)/1000.0f) : (0.010f+GetRandomValue(0,8)/1000.0f);
        Spark sp={0}; sp.pos=p; sp.vel=v; sp.life=lf; sp.life0=lf; sp.size=sz; sp.rest=floorY; sp.blood=1; sp.lasting=gob;
        sp.seed=(unsigned int)GetRandomValue(1,1<<30);     // stable per-splat outline jitter
        sp.col=(Color){ (unsigned char)(150+GetRandomValue(0,105)), (unsigned char)GetRandomValue(0,28), (unsigned char)GetRandomValue(0,18), 255 };
        g_sparks[s]=sp;
        break;
    }
    // A flat continuous pool stain spreading from the body's feet -- this is what
    // reads as "liquid"; the flying gobs above just stipple fine spatter around it.
    for (int s=0;s<MAX_SPARKS;s++){
        if (g_sparks[s].life>0) continue;
        Spark sp={0};
        sp.pos=(Vector3){p.x, floorY+0.02f, p.z}; sp.vel=(Vector3){0,0,0};   // born settled on the floor
        sp.life=1e9f; sp.life0=1e9f; sp.rest=floorY;
        sp.blood=1; sp.lasting=1; sp.pool=1;
        sp.size=0.20f+0.11f*intensity;                     // pool radius; headshots pool wider
        sp.seed=(unsigned int)GetRandomValue(1,1<<30);
        sp.col=(Color){ (unsigned char)(105+GetRandomValue(0,35)), (unsigned char)GetRandomValue(0,12), (unsigned char)GetRandomValue(0,8), 255 };
        g_sparks[s]=sp;
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

// switch the weapon animation, restart its cursor. Retained for future weapon
// actions even though the reload/fire paths now set clips directly.
static void __attribute__((unused)) SetAnim(int idx, int once){
    g_curAnim=idx; g_animT=0.0f; g_animOnce=once;
}

// Test the aim ray against alive enemies (treated as a vertical box). Returns
// the nearest enemy index hit within maxDist, -1 if none; sets *outDist/*outPt.
// *outHead is set to 1 when that nearest hit also pierces the enemy's head box.
static int HitEnemy(Vector3 ro, Vector3 rd, float maxDist, float *outDist, Vector3 *outPt, int *outHead){
    int best=-1; float bd=maxDist; if (outHead) *outHead=0;
    for (int i=0;i<MAX_ENEMIES;i++){
        if (g_enemies[i].state!=1) continue;
        Vector3 c=g_enemies[i].pos;          // c.y = the enemy's floor (0 in the arena, map height on a map)
        BoundingBox bb={ (Vector3){c.x-ENEMY_RADIUS, c.y, c.z-ENEMY_RADIUS},
                         (Vector3){c.x+ENEMY_RADIUS, c.y+ENEMY_HEIGHT, c.z+ENEMY_RADIUS} };
        RayCollision rc=GetRayCollisionBox((Ray){ro,rd},bb);
        if (rc.hit && rc.distance>0 && rc.distance<bd){
            bd=rc.distance; best=i; *outDist=rc.distance; *outPt=rc.point;
            // The head box lives inside the body box's vertical span, so the
            // nearest-enemy pick above is unaffected; we just additionally ask
            // whether this same ray clips the (narrower) head of that enemy.
            BoundingBox hb={ (Vector3){c.x-HEAD_RADIUS, c.y+ENEMY_HEIGHT-HEAD_ZONE_H, c.z-HEAD_RADIUS},
                             (Vector3){c.x+HEAD_RADIUS, c.y+ENEMY_HEIGHT,             c.z+HEAD_RADIUS} };
            RayCollision hrc=GetRayCollisionBox((Ray){ro,rd},hb);
            if (outHead) *outHead = (hrc.hit && hrc.distance>0);
        }
    }
    return best;
}

// Apply damage to alive enemy ei (dmg already includes any headshot multiplier).
// Shared by every weapon: bullets, the flame cone, and the knife. Handles the
// flinch, the kill -> dying state + tally + death sound. blood>0 spurts blood at
// hitPt along dir; head>0 also pops the "HEADSHOT!" flash + counts a headshot kill.
static void HurtEnemy(int ei, float dmg, int head, Vector3 dir, Vector3 hitPt, int blood){
    if (ei<0 || g_enemies[ei].state!=1) return;
    if (blood) SpawnBlood(hitPt, dir, head?1.9f:1.0f, g_enemies[ei].pos.y);
    g_enemies[ei].hp -= dmg;
    if (head) g_hsFlash=1.1f;
    if (g_enemies[ei].hp<=0){
        g_enemies[ei].state=2; g_enemies[ei].deathT=0; g_enemies[ei].animT=0;
        g_enemies[ei].drawY=g_enemies[ei].pos.y;          // settle the render height so the corpse doesn't pop
        g_kills++; if (head) g_headshots++;
        if (g_audio && g_nDeathSnd>0) PlaySound(g_deathSnd[GetRandomValue(0,g_nDeathSnd-1)]);
    } else {
        g_enemies[ei].hitT=0.45f;                         // non-fatal hit -> flinch
    }
}

// ---- Grenade launcher (slot 0) -- arcing projectile + radial explosion ----------
typedef struct { Vector3 pos, vel; float life; int active; } Grenade;
#define MAX_GREN     8
#define GREN_SPEED   30.0f     // launch speed (units/s)
#define GREN_GRAV    24.0f     // downward accel -> the lob arc
#define GREN_RADIUS  4.8f      // explosion blast radius
#define GREN_DMG     140.0f    // damage at the centre, linear falloff to 0 at the edge
#define GREN_LIFE    4.0f      // fuse: detonates after this long if it hasn't hit anything
#define GL_FIRE_CD   0.85f     // seconds between launches
// 'allanims' frame layout (605 frames total). Wrist-motion analysis of the clip shows
// the hold pose at frame 0 (and ~531-573), with action bursts at ~127-131, ~179-183,
// ~388-392 -- the first burst pair reads as the shell-load action:
#define GL_IDLE_FRAME   0      // rest/hold pose kept between shots
#define GL_RELOAD_FROM  120    // reload sub-range (R plays FROM..TO then returns to idle)
#define GL_RELOAD_TO    185
static Grenade g_gren[MAX_GREN];
static void LaunchGrenade(Camera3D cam){
    Vector3 dir=Vector3Normalize(Vector3Subtract(cam.target,cam.position));
    Vector3 muzzle=Vector3Add(cam.position, Vector3Scale(dir,0.6f));
    for (int i=0;i<MAX_GREN;i++) if (!g_gren[i].active){
        g_gren[i]=(Grenade){ muzzle, Vector3Scale(dir,GREN_SPEED), GREN_LIFE, 1 };
        g_gren[i].vel.y += 3.0f;                            // a touch of loft -> it arcs, not a flat line
        DebugLog("grenade","\"launched\":1");
        return;
    }
}
static void GrenadeExplode(Vector3 p){
    for (int i=0;i<MAX_ENEMIES;i++){                        // radial damage, linear falloff
        if (g_enemies[i].state!=1) continue;
        Vector3 c=(Vector3){g_enemies[i].pos.x, g_enemies[i].pos.y+ENEMY_HEIGHT*0.5f, g_enemies[i].pos.z};
        float d=Vector3Distance(p,c);
        if (d<=GREN_RADIUS) HurtEnemy(i, GREN_DMG*(1.0f-d/GREN_RADIUS), 0, (Vector3){0,1,0}, c, 1);
    }
    for (int q=0;q<26;q++) SpawnSmoke(p,1);                 // fireball: smoke...
    for (int q=0;q<34;q++) SpawnFlame(p,(Vector3){GetRandomValue(-100,100)/100.0f, GetRandomValue(-30,100)/100.0f, GetRandomValue(-100,100)/100.0f}); // ...radial flame
    SpawnImpact(p,(Vector3){0,1,0});                        // bright spark glints
    AddScorch(p,(Vector3){0,1,0}, 1.3f);                   // soot on the ground
    g_shake=fminf(1.0f, g_shake+0.7f);                     // concussion
    DebugLog("grenade","\"explode\":[%.1f,%.1f,%.1f]", p.x,p.y,p.z);
}
static void UpdateGrenades(float dt){
    for (int i=0;i<MAX_GREN;i++){ Grenade *g=&g_gren[i]; if (!g->active) continue;
        g->life-=dt;
        Vector3 prev=g->pos;
        g->vel.y -= GREN_GRAV*dt;
        g->pos=Vector3Add(g->pos, Vector3Scale(g->vel,dt));
        int hit=0; Vector3 hp=g->pos;
        Vector3 seg=Vector3Subtract(g->pos,prev); float L=Vector3Length(seg);
        if (g_hasMap && g_mapCol.ready){                    // hit map geometry along this step?
            if (L>1e-4f){ Vector3 d=Vector3Scale(seg,1.0f/L); float wd=MapRayNearest(prev,d,L+0.15f);
                if (wd>0.0f && wd<=L+0.1f){ hp=Vector3Add(prev,Vector3Scale(d,wd)); hit=1; } }
        } else if (g->pos.y<=0.12f){ hp=(Vector3){g->pos.x,0.12f,g->pos.z}; hit=1; }   // arena floor
        if (!hit && g->life<=0.0f){ hit=1; hp=g->pos; }     // fuse ran out -> air burst
        if (hit){ GrenadeExplode(hp); g->active=0; }
    }
}

static void Fire(Camera3D cam) {
    if (g_fireCd>0) return;
    Weapon *cw=&g_weapons[g_curWeapon];
    g_fireCd = (cw->fireCd>0.0f) ? cw->fireCd : 0.12f;   // per-weapon fire rate (slow shotguns etc.)
    // One-shot weapons (e.g. shotguns) sound their report here, per shot; auto
    // weapons loop in Update() instead.
    if (g_audio && cw->hasSnd && !cw->sndLoop) PlaySound(cw->fireSnd);
    Vector3 dir=Vector3Normalize(Vector3Subtract(cam.target,cam.position));
    Vector3 muzzle=Vector3Add(cam.position,Vector3Scale(dir,0.4f));
    Vector3 hit,nrm,end=Vector3Add(cam.position,Vector3Scale(dir,80.0f));
    float worldDist=80.0f;
    int worldHit=RaycastWorld(cam.position,dir,&hit,&nrm);
    if (worldHit){ end=hit; worldDist=Vector3Distance(cam.position,hit); }
    // enemy hit takes priority if it's closer than the world geometry
    float ed; Vector3 ep; int head=0;
    int ei=HitEnemy(cam.position,dir,worldDist,&ed,&ep,&head);
    if (ei>=0){
        end=ep;                                           // gobs pool on the enemy's floor
        HurtEnemy(ei, head ? ENEMY_DMG_PER_SHOT*HEADSHOT_MULT : ENEMY_DMG_PER_SHOT, head, dir, ep, 1);
    } else if (worldHit){
        SpawnImpact(hit,nrm);
    }
    for (int i=0;i<MAX_TRACERS;i++) if (g_tracers[i].life<=0){ g_tracers[i]=(Tracer){muzzle,end,0.05f}; break; }
    // Code-driven recoil instead of the Shoot clip (that clip repositions the
    // gun out of the viewmodel frame -> "gun goes away"). Kick rises to 1, decays.
    g_recoil=1.0f;
    if (cw->playShoot && g_aShoot!=g_aIdle && g_aShoot<g_gunAnimN && !g_reloading){
        g_curAnim=g_aShoot; g_animOnce=1; g_animT=0.0f;    // play the model's Shot clip (AK)
    }
    DebugLog("fire","\"enemy\":%d,\"head\":%d,\"end\":[%.2f,%.2f,%.2f]", ei, head, end.x,end.y,end.z);
}

// Can the enemy stand at (x,z) coming from height curY? The spot must have a floor
// that's a sane STEP UP (<=ENEMY_STEP) or a DROP it can take (<=NAV_MAXFALL, ~one
// storey -- so it'll leap off a balcony to chase) -- but NOT off into the void (no
// floor) or off a cliff taller than NAV_MAXFALL -- and be clear of walls. This single
// test drives all enemy movement: they slide along walls, climb stairs, drop off
// ledges toward the player, and still refuse bottomless edges.
#define ENEMY_STEP    0.7f    // max riser an enemy can step up (matches the player's auto-step)
static int EnemyMoveOK(float x, float z, float curY){
    if (!g_mapCol.ready) return 1;
    float top=curY+ENEMY_STEP, fd=MapRayNearest((Vector3){x,top,z},(Vector3){0,-1,0},80.0f);
    if (fd<=0) return 0;                                // no floor under the spot -> the void
    float ny=top-fd;                                   // destination floor height
    if (ny>curY+ENEMY_STEP)  return 0;                 // riser too tall to step up
    if (ny<curY-NAV_MAXFALL) return 0;                 // drop taller than a storey -> a cliff, don't leap
    return !MapSphereHitsWall((Vector3){x,ny+0.9f,z},ENEMY_MOVE_R);  // clear where it'd stand
}
// Move enemy i by (mx,mz), per-axis -- nothing walks through walls OR off ledges. Both
// the chase and the enemy-vs-enemy separation go through here.
static void EnemyNudge(int i, float mx, float mz){
    Enemy *e=&g_enemies[i];
    if (g_hasMap && g_mapCol.ready){
        if (EnemyMoveOK(e->pos.x+mx, e->pos.z, e->pos.y)) e->pos.x+=mx;
        if (EnemyMoveOK(e->pos.x, e->pos.z+mz, e->pos.y)) e->pos.z+=mz;
    } else { e->pos.x+=mx; e->pos.z+=mz; }
}

// Walk alive enemies toward the player; advance death timers; melee on contact.
static void UpdateEnemies(float dt){
    if (!g_hasEnemy) return;
    int prevCell = g_navPlayerCell;
    if (g_hasMap) MapNavUpdate(g_pos);                 // refresh the flow field if the player moved cells
    int flowChanged = (g_navPlayerCell != prevCell);   // player crossed into a new cell -> paths changed
    for (int i=0;i<MAX_ENEMIES;i++){
        Enemy *e=&g_enemies[i];
        if (e->state==1){
            float dx=g_pos.x-e->pos.x, dz=g_pos.z-e->pos.z;
            float d=sqrtf(dx*dx+dz*dz);
            if (e->hitT>0) e->hitT-=dt;
            if (e->roarT>0) e->roarT-=dt;
            // "Running in place" = it's trying to chase but the wall won't let it move at
            // all (an unreachable spot). stuckT accrues only while it tries-but-can't-move
            // (updated in the chase block below); a NAVIGATING enemy moves every frame even
            // when it curves around, so this never false-flags it. flowChanged resets it.
            if (flowChanged) e->stuckT=0.0f;
            int stuck = (g_hasMap && e->stuckT > 1.0f);
            // self-heal: if the body is wedged INSIDE wall geometry (spawned/pushed into an
            // impossible spot, e.g. clipping through a wall) it can never free itself ->
            // respawn it somewhere valid. Tracked independently of chase state, so it also
            // fires when the enemy is jammed in a wall right next to the player (attack range).
            if (g_mapCol.ready && MapSphereHitsWall((Vector3){e->pos.x, e->pos.y+0.9f, e->pos.z}, 0.35f)){
                e->embedT += dt;
                if (e->embedT > 0.8f){ SpawnEnemy(i); continue; }   // SpawnEnemy always picks a navigable cell now
            } else e->embedT = 0.0f;
            // clip priority: flinch > attack (in melee range) > idle-when-stuck > run (chasing)
            int want = (e->hitT>0) ? g_eHit : (d<=ENEMY_ATTACK_RANGE ? g_eAttack : (stuck ? g_eIdle : g_eRun));
            if (want!=e->clip){
                if (want==g_eAttack && e->roarT<=0 && g_audio && g_hasAttackSnd){   // lunging into an attack -> roar
                    PlaySound(g_attackSnd); e->roarT=2.5f;                          // cooldown so it doesn't spam at the range edge
                }
                e->clip=want; e->animT=0;
            }
            if (e->hitT<=0 && d>ENEMY_ATTACK_RANGE && !stuck){ // chase: follow the nav flow field, else beeline
                Vector3 sd; float mx=dx/d, mz=dz/d;
                if (g_hasMap && MapNavSteer(e->pos,&sd)){ mx=sd.x; mz=sd.z; }
                float step=ENEMY_SPEED*dt; Vector3 b4=e->pos;
                EnemyNudge(i, mx*step, mz*step);
                // wedged (the steer heads into a wall and per-axis collision blocks BOTH
                // axes)? sweep the heading outward to either side -- nearest angle first --
                // until one frees up, so the enemy slides out of corners instead of running
                // in place. Goes out to ±150° to escape concave pockets.
                if (g_hasMap && fabsf(e->pos.x-b4.x)<1e-5f && fabsf(e->pos.z-b4.z)<1e-5f){
                    const float ANG[10]={0.524f,-0.524f, 1.047f,-1.047f, 1.571f,-1.571f, 2.094f,-2.094f, 2.618f,-2.618f}; // ±30,60,90,120,150
                    for (int k=0;k<10;k++){
                        float c=cosf(ANG[k]), s=sinf(ANG[k]);
                        EnemyNudge(i, (mx*c-mz*s)*step, (mx*s+mz*c)*step);
                        if (fabsf(e->pos.x-b4.x)>1e-5f || fabsf(e->pos.z-b4.z)>1e-5f) break;
                    }
                }
                // tried to chase this frame: if it barely moved, it's grinding a wall; if it
                // moved at all (even sliding/curving), it's making its way -> not stuck.
                float mv=(e->pos.x-b4.x)*(e->pos.x-b4.x)+(e->pos.z-b4.z)*(e->pos.z-b4.z);
                if (mv < 0.0004f) e->stuckT += dt; else e->stuckT = 0.0f;   // 0.02 units/frame threshold
            }
            if (g_hasMap && stuck && e->hitT<=0 && d>ENEMY_ATTACK_RANGE){
                // stuck against a wall and idling -> ease back so the animated HANDS stop
                // poking through it. Drift toward whichever directions clear a hands-radius
                // sphere; settles once the body+arms no longer overlap the wall.
                float by=e->pos.y+0.9f;
                if (MapSphereHitsWall((Vector3){e->pos.x,by,e->pos.z},ENEMY_HANDS_R)){
                    float ox=0,oz=0;
                    const float A[8]={0.0f,0.785f,1.571f,2.356f,3.142f,3.927f,4.712f,5.498f};
                    for (int k=0;k<8;k++){ float cxx=cosf(A[k]),czz=sinf(A[k]);
                        if (!MapSphereHitsWall((Vector3){e->pos.x+cxx*0.3f,by,e->pos.z+czz*0.3f},ENEMY_HANDS_R)){ ox+=cxx; oz+=czz; } }
                    float l=sqrtf(ox*ox+oz*oz);
                    if (l>1e-4f) EnemyNudge(i, ox/l*ENEMY_SPEED*dt*0.6f, oz/l*ENEMY_SPEED*dt*0.6f);
                }
            }
            if (g_hasMap && g_mapCol.ready){                   // keep the enemy standing on the map floor
                // look down from only a step above the feet, not 4m up -- otherwise the
                // snap can grab an overhead landing and wedge the enemy under the stairs.
                float top=e->pos.y+ENEMY_STEP, fd=MapRayNearest((Vector3){e->pos.x,top,e->pos.z},(Vector3){0,-1,0},40.0f);
                if (fd>0) e->pos.y=top-fd;
            }
            // ease the RENDER height toward the real floor so stair steps ramp smoothly
            // instead of popping. A real ledge DROP (drawY well above the new floor, or
            // already mid-fall) is animated under gravity so the body falls and lands
            // instead of teleporting. Clamp the climb side so the feet ride ON each step.
            float dy=e->pos.y-e->drawY;
            if (e->fallV<0.0f || dy<-1.5f){                    // mid-fall, or just stepped off a ledge
                e->fallV -= 22.0f*dt; e->drawY += e->fallV*dt;
                if (e->drawY <= e->pos.y){ e->drawY=e->pos.y; e->fallV=0.0f; }   // landed
            } else if (fabsf(dy)>1.5f){
                e->drawY=e->pos.y;                            // big upward pop (spawn / stairs up) -> snap
            } else {
                e->drawY += dy*(1.0f-expf(-16.0f*dt));
                if (e->drawY < e->pos.y-0.03f) e->drawY = e->pos.y-0.03f;   // feet never sink into the stairs
            }
            if (e->clip==g_eAttack && e->hitT<=0 && !g_godMode) g_playerHp-=ENEMY_TOUCH_DMG*dt;
            if (g_enemyAnimN>0){                               // advance current clip (looping)
                int nf=ANIM_FRAMES(g_enemyAnim[e->clip]);
                float spd=(e->clip==g_eAttack)?90.0f:30.0f;    // attack plays much faster
                e->animT+=dt*spd;
                if (nf>0 && e->animT>=nf) e->animT-=nf;
            }
        } else if (e->state==2){                              // dying: play the death anim to its settle frame, then leave a corpse
            int nf = (g_enemyAnimN>0) ? g_eDeathFrame : 0;     // stop at the "on the floor" frame, not the spring-back last frame
            if (nf>0){ e->animT+=dt*60.0f; if (e->animT>(float)nf) e->animT=(float)nf; }
            e->deathT+=dt;
            // wait until the death anim has actually settled (body on the floor),
            // not a fixed timer -- otherwise the corpse is recorded mid-fall.
            if ((nf<=0 || e->animT>=(float)nf) && e->deathT>0.2f){
                float yaw=atan2f(g_pos.x-e->pos.x, g_pos.z-e->pos.z)*RAD2DEG + ENEMY_YAW_OFFSET;
                Corpse *cp=&g_corpses[g_corpseNext]; *cp=(Corpse){0};            // lasting body, posed at the final death frame
                cp->pos=e->pos; cp->yaw=yaw; cp->active=1; cp->burnN=e->burnN;    // keep the soot marks it earned while alive
                for (int k=0;k<e->burnN;k++){ cp->burnY[k]=e->burnY[k]; cp->burnR[k]=e->burnR[k]; }
                g_corpseNext=(g_corpseNext+1)%MAX_CORPSES;
                if (!g_noEnemies) SpawnEnemy(i); else e->state=0;
            }
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
                EnemyNudge(a, -dx*push, -dz*push);    // wall-gated, so crowding can't shove them through walls
                EnemyNudge(b,  dx*push,  dz*push);
            }
        }
    }
    if (g_playerHp<0) g_playerHp=0;
}

static void Collide(void) {
    float r=0.4f;
    if (g_hasMap){          // map: walls/floor are handled in Update(); here just push out of enemies
        for (int e=0;e<MAX_ENEMIES;e++){
            if (g_enemies[e].state!=1) continue;
            if (fabsf((g_pos.y-EYE_H)-g_enemies[e].pos.y) > ENEMY_HEIGHT) continue;   // different floor -> ignore
            float dx=g_pos.x-g_enemies[e].pos.x, dz=g_pos.z-g_enemies[e].pos.z;
            float d=sqrtf(dx*dx+dz*dz), mind=r+ENEMY_RADIUS;
            if (d<mind && d>1e-4f){
                float nx=g_pos.x+dx*(mind-d)/d, nz=g_pos.z+dz*(mind-d)/d, by=g_pos.y-0.55f;
                if (!MapSphereHitsWall((Vector3){nx,by,g_pos.z},0.42f)) g_pos.x=nx;   // don't get shoved into a wall
                if (!MapSphereHitsWall((Vector3){g_pos.x,by,nz},0.42f)) g_pos.z=nz;
            }
        }
        return;
    }
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

// Begin the active weapon's reload: multi-phase sequence if it has one, else the
// single reload clip, else nothing. Used by the R key and the shotgun's pump.
static void StartReload(void){
    if (g_reloading) return;
    if (g_reloadSeqN>0){ g_reloading=1; g_reloadStep=0; g_curAnim=g_reloadSeq[0]; g_animOnce=1; g_animT=0.0f; }
    else if (g_aReload!=g_aIdle && g_aReload<g_gunAnimN){ g_reloading=1; g_reloadStep=-1; g_curAnim=g_aReload; g_animOnce=1; g_animT=0.0f; }
}

// Options/pause menu: a vertical stack of buttons centred on screen. DrawMenu and
// the click hit-test (in Update) both call MenuRect, so the layout can't drift.
#define MENU_N 4
static Rectangle MenuRect(int i){
    int W=GetScreenWidth(), H=GetScreenHeight();
    float bw=360, bh=54, gap=16, total=MENU_N*bh+(MENU_N-1)*gap;
    return (Rectangle){ W/2.0f-bw/2.0f, H/2.0f-total/2.0f+24+i*(bh+gap), bw, bh };
}
// Persist the fullscreen choice across launches (per-machine, gitignored like the
// tune files). Default is windowed; the menu writes "fullscreen 1" once enabled.
static void LoadOptions(void){
    FILE *f=fopen("options.txt","r"); if(!f) return;
    int fs=0; if (fscanf(f,"fullscreen %d",&fs)==1) g_fullscreen=(fs!=0);
    fclose(f);
}
static void SaveOptions(void){
    FILE *f=fopen("options.txt","w"); if(!f) return;
    fprintf(f,"fullscreen %d\n", g_fullscreen?1:0); fclose(f);
}
// Fullscreen as a plain UNDECORATED window sized to the monitor -- NOT raylib's
// ToggleBorderlessWindowed(), which on macOS drops the window into a native
// fullscreen Space that swallows Cmd-Tab and stops delivering keys (so ESC could
// never reach the menu). A borderless normal window stays a normal window: Cmd-Tab
// works and ESC opens the menu. on=1 cover the monitor, on=0 restore 1280x720.
static void ApplyFullscreen(int on){
    int mon=GetCurrentMonitor();
    int mw=GetMonitorWidth(mon), mh=GetMonitorHeight(mon);
    if (on){
        SetWindowState(FLAG_WINDOW_UNDECORATED);
        SetWindowSize(mw, mh);
        SetWindowPosition(0, 0);
    } else {
        ClearWindowState(FLAG_WINDOW_UNDECORATED);
        SetWindowSize(1280, 720);
        SetWindowPosition((mw-1280)/2, (mh-720)/2);   // re-centre on the monitor
    }
}

// Bring the player back after death: full health, back to the spawn point. The
// field is left as-is -- the enemies that killed you are still out there.
static void RespawnPlayer(void){
    g_playerHp=100.0f; g_prevHp=100.0f; g_hurt=0.0f;
    g_dead=0; g_deadT=0.0f;
    g_pos = g_hasMap ? g_spawnResetPos : (Vector3){0,EYE_H,6};
    g_vy=0.0f; g_grounded=0; g_eyeSmooth=0.0f;
    DebugLog("respawn","\"pos\":[%.1f,%.1f,%.1f]", g_pos.x,g_pos.y,g_pos.z);
}

static void Update(void) {
    float dt=GetFrameTime(); if (dt>0.05f) dt=0.05f;

    // While P-paused, ANY of P / ESC / click resumes (handled here, before ESC would
    // otherwise open the options menu). Stays captured so play resumes seamlessly.
    if (g_paused){
        if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)){
            g_paused=0; DebugLog("pause","\"paused\":false");
        }
        return;
    }

    // ESC toggles the options/pause menu. While open the cursor is freed for
    // clicking and the whole gameplay update is skipped (early return), so the
    // world freezes behind the overlay.
    if (IsKeyPressed(KEY_ESCAPE)){
        g_menu=!g_menu;
        if (g_menu) EnableCursor(); else DisableCursor();
        DebugLog("menu","\"open\":%s", g_menu?"true":"false");
    }
    if (g_menu){
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)){
            Vector2 mp=GetMousePosition();
            for (int i=0;i<MENU_N;i++) if (CheckCollisionPointRec(mp,MenuRect(i))){
                if (i==0){                                  // Fullscreen toggle
                    g_fullscreen=!g_fullscreen; ApplyFullscreen(g_fullscreen); SaveOptions();
                    EnableCursor();                         // the toggle can re-grab; keep it free for the menu
                    DebugLog("options","\"fullscreen\":%s", g_fullscreen?"true":"false");
                } else if (i==1){ g_returnToMenu=1; g_menu=0;     // Select Map -> back to the picker
                    DebugLog("menu","\"selectMap\":true"); }
                else if (i==2){ g_menu=0; DisableCursor(); }      // Resume
                else if (i==3) g_quit=1;                          // Quit
            }
        }
        return;
    }

    // P pauses/unpauses the game: the gameplay update is skipped (early return) so
    // the world freezes, but the cursor stays captured so play resumes seamlessly.
    if (IsKeyPressed(KEY_P)){
        g_paused=!g_paused;
        DebugLog("pause","\"paused\":%s", g_paused?"true":"false");
    }
    if (g_paused) return;

    // Dead: the world is frozen (no move/fire, enemies stop too) behind the "YOU
    // DIED" overlay. After a short beat, ENTER or a click respawns you at spawn.
    if (g_dead){
        g_deadT += dt;
        if (g_hurt>0) g_hurt=fmaxf(0.0f, g_hurt-dt*1.2f);
        if (g_deadT>1.0f && (IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))
            RespawnPlayer();
        return;
    }

    // Keep the mouse captured the whole time we're playing. macOS releases the
    // cursor whenever the window loses focus (Cmd-Tab, notifications, etc.), so
    // re-grab it the instant we have focus again and it's become visible.
    if (IsWindowFocused() && !IsCursorHidden()) DisableCursor();

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
    Vector3 mv={0,0,0};
    if (moving && Vector3Length(wish)>0.001f){
        wish=Vector3Scale(Vector3Normalize(wish),speed*dt);
        mv=wish; g_bob+=dt*(IsKeyDown(KEY_LEFT_SHIFT)?14.0f:10.0f);
    }
    if (g_hasMap && !g_noclip){                         // block into walls per-axis (so you slide, never drift)
        float by=g_pos.y-0.55f;
        if (!MapSphereHitsWall((Vector3){g_pos.x+mv.x, by, g_pos.z}, 0.42f)) g_pos.x+=mv.x;
        if (!MapSphereHitsWall((Vector3){g_pos.x, by, g_pos.z+mv.z}, 0.42f)) g_pos.z+=mv.z;
    } else { g_pos.x+=mv.x; g_pos.z+=mv.z; }
    if (g_hasMap && g_noclip){                         // F: free-fly / noclip (inspect or escape)
        if (IsKeyDown(KEY_SPACE))        g_pos.y += speed*dt;
        if (IsKeyDown(KEY_LEFT_CONTROL)) g_pos.y -= speed*dt;
        g_vy=0; g_grounded=1;
    } else if (g_hasMap){                               // FPS collision against the loaded map mesh
        if (g_grounded && IsKeyPressed(KEY_SPACE)){ g_vy=6.0f; g_grounded=0; }
        float dn=MapRayNearest(g_pos,(Vector3){0,-1,0},80.0f);  // eye -> floor distance
        float floorEye = (dn>0) ? g_pos.y-dn+EYE_H : -1e30f;    // eye height standing on that floor
        const float STEP=0.7f;                                  // auto climb/descend up to this height
        if (g_vy<=0.0f && dn>0 && (g_pos.y-floorEye)<=STEP){
            // grounded (or within a step of the floor): glue the feet to it,
            // but feed the height change into g_eyeSmooth so the CAMERA eases
            // over the step instead of popping (the actual stair-jitter fix).
            g_eyeSmooth += floorEye - g_pos.y;
            if (g_eyeSmooth> 1.0f) g_eyeSmooth= 1.0f;
            if (g_eyeSmooth<-1.0f) g_eyeSmooth=-1.0f;
            g_pos.y=floorEye; g_vy=0.0f; g_grounded=1;
        } else {                                                // airborne: ballistic
            g_vy-=18.0f*dt; g_pos.y+=g_vy*dt; g_grounded=0;
            if (dn>0 && g_pos.y<=floorEye){ g_pos.y=floorEye; g_vy=0.0f; g_grounded=1; }   // land
            if (g_vy>0.0f){ float up=MapRayNearest(g_pos,(Vector3){0,1,0},0.4f); if (up>0) g_vy=0.0f; }  // ceiling
        }
    } else {
        if (g_grounded && IsKeyPressed(KEY_SPACE)){ g_vy=5.0f; g_grounded=0; }
        g_vy-=16.0f*dt; g_pos.y+=g_vy*dt;
        if (g_pos.y<=EYE_H){ g_pos.y=EYE_H; g_vy=0; g_grounded=1; }
    }
    // Fell out of the world (off a ledge into the void). Lethal in normal play; in
    // god mode just teleport back to spawn so dev play isn't an endless fall.
    if (g_hasMap && !g_noclip && g_pos.y < g_voidY){
        if (!g_godMode) g_playerHp = 0.0f;
        g_pos=g_spawnResetPos; g_vy=0.0f; g_grounded=0; g_eyeSmooth=0.0f;
        DebugLog("void","\"fell\":true,\"died\":%s", g_godMode?"false":"true");
    }
    Collide();

    // ease the step-smoothing offset back to 0 (~0.15s), then view from the
    // smoothed eye height so stairs glide instead of popping
    if (g_eyeSmooth!=0.0f){ g_eyeSmooth -= g_eyeSmooth*fminf(1.0f,dt*14.0f); if (fabsf(g_eyeSmooth)<0.002f) g_eyeSmooth=0.0f; }
    Vector3 eye=g_pos; eye.y-=g_eyeSmooth;
    if (g_shake>0.001f){                                   // minigun brain-shake: jitter the head a touch
        float a=g_shake*0.055f;
        eye.x += GetRandomValue(-1000,1000)/1000.0f*a;
        eye.y += GetRandomValue(-1000,1000)/1000.0f*a;
        eye.z += GetRandomValue(-1000,1000)/1000.0f*a;
    }
    g_cam.position=eye; g_cam.target=Vector3Add(eye,fwd); g_cam.up=(Vector3){0,1,0};
    if (g_shake>0.001f){                                   // plus a little look-direction wobble
        float a=g_shake*0.03f;
        g_cam.target.x += GetRandomValue(-1000,1000)/1000.0f*a;
        g_cam.target.y += GetRandomValue(-1000,1000)/1000.0f*a;
    }

    // shooting / reload
    if (g_fireCd>0) g_fireCd-=dt;
    if (g_recoil>0) g_recoil=fmaxf(0.0f, g_recoil - dt*7.0f);   // recoil settles in ~0.14s
    if (g_hsFlash>0) g_hsFlash-=dt;                            // "HEADSHOT!" flash fades out
    Weapon *cw=&g_weapons[g_curWeapon];
    int lmb=IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    int firingNow=0;                                       // looped fire sound active this frame?

    if (IsKeyPressed(KEY_R)){
        if (cw->grenade){ g_reloading=1; g_animOnce=0; g_curAnim=g_aIdle; g_animT=(float)GL_RELOAD_FROM; }  // GL: play the reload frame slice of 'allanims'
        else StartReload();
    }
    else if (!g_reloading){
        if (cw->burst>0){                                  // rifle: one 3-round burst per trigger pull
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && g_burstLeft==0) g_burstLeft=cw->burst;
            if (g_burstLeft>0){ firingNow=1; if (g_fireCd<=0.0f){ Fire(g_cam); g_burstLeft--; } }
        } else if (cw->grenade){                            // grenade launcher: lob one grenade per click
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && g_fireCd<=0.0f){
                g_fireCd=GL_FIRE_CD; g_recoil=1.0f;
                if (g_audio && cw->hasSnd) PlaySound(cw->fireSnd);
                LaunchGrenade(g_cam);
            }
        } else if (!cw->spinUp && !cw->soundGated && !cw->flame && !cw->melee && !cw->grenade && lmb){ // full-auto / one-shot (shotgun, sawnoff)
            int willFire=(g_fireCd<=0.0f);
            firingNow=1; Fire(g_cam);
            if (willFire && cw->autoReload){               // shotgun: pump (reload anim) + cock sound between shots
                StartReload();
                if (g_audio && cw->hasAuxSnd) PlaySound(cw->auxSnd);
            }
        }
    }

    // Flamethrower (slot 7): hold the trigger -> a continuous cone of fire. Spews
    // flame billboards along the aim and burns every alive enemy inside the cone +
    // range (LOS-checked on a map so it can't burn through walls). No per-shot cd.
    if (cw->flame){
        int firing = lmb && !g_reloading;
        if (firing){
            g_flameHeat += dt;                                           // warm up while held
            firingNow=1;                                                 // the roar plays through the whole warm-up
            // Emit from the GUN'S NOZZLE, not screen centre: offset right + a hair down
            // (where the viewmodel barrel tip sits) and forward past the barrel, then the
            // jet flies out along the aim. Built from the camera basis so it tracks the gun.
            Vector3 upv=Vector3CrossProduct(right,fwd);                  // camera up
            Vector3 muzzle=Vector3Add(g_pos, Vector3Add(Vector3Add(
                Vector3Scale(fwd,   0.95f),                              // out at the barrel tip
                Vector3Scale(right,  0.20f)),                            // to the right (held off-centre)
                Vector3Scale(upv,   -0.12f)));                           // barely below the view axis -> at the tip
            if (g_flameHeat < FLAME_WARMUP){                             // NOT lit yet: spit raw fuel, no damage
                for (int q=0;q<2;q++) SpawnLiquid(muzzle, fwd);
            } else {                                                     // ignited: flame + burn
                for (int q=0;q<5;q++) SpawnFlame(muzzle, fwd);          // a dense, wide gout streaming from the nozzle
                for (int i=0;i<MAX_ENEMIES;i++){
                    if (g_enemies[i].state!=1) continue;
                    Vector3 cen=(Vector3){g_enemies[i].pos.x, g_enemies[i].pos.y+ENEMY_HEIGHT*0.5f, g_enemies[i].pos.z};
                    Vector3 to=Vector3Subtract(cen, muzzle); float dist=Vector3Length(to);
                    if (dist>FLAME_RANGE || dist<1e-3f) continue;
                    Vector3 tn=Vector3Scale(to, 1.0f/dist);
                    if (Vector3DotProduct(tn, fwd) < FLAME_COS) continue;          // outside the cone
                    if (g_hasMap && g_mapCol.ready){                                // wall between -> shielded
                        float wd=MapRayNearest(muzzle, tn, dist); if (wd>0 && wd<dist-0.3f) continue; }
                    // Char ONLY where the flame actually licks the body: the ray's height
                    // at the enemy's distance -> a local mark that grows the longer you hold
                    // it there. Sweeping the stream leaves several marks; a tap leaves one small one.
                    Enemy *be=&g_enemies[i];
                    float ch=muzzle.y + fwd.y*dist - be->pos.y;                     // contact height above the feet
                    if (ch<0.15f) ch=0.15f; if (ch>ENEMY_HEIGHT-0.15f) ch=ENEMY_HEIGHT-0.15f;
                    int m=-1; for (int k=0;k<be->burnN;k++) if (fabsf(be->burnY[k]-ch)<0.30f){ m=k; break; }
                    if (m<0 && be->burnN<MAX_BURN){ m=be->burnN++; be->burnY[m]=ch; be->burnR[m]=0.08f; }
                    if (m>=0) be->burnR[m]=fminf(0.34f, be->burnR[m]+dt*0.7f);      // darkens/spreads with sustained flame
                    HurtEnemy(i, FLAME_DPS*dt, 0, tn, cen, 0);                      // burning: no blood spurt
                }
                // Scorch whatever the stream lands on (floor/wall/prop), throttled so marks
                // build up steadily rather than every frame. Map rays give distance only, so
                // the decal faces back along the aim (~the wall normal when you flame it head-on).
                g_scorchT += dt;
                if (g_scorchT >= 0.04f){
                    Vector3 sp, sn; int landed=0;
                    if (g_hasMap && g_mapCol.ready){
                        float wd=MapRayNearest(muzzle, fwd, FLAME_RANGE);
                        if (wd>0){ sp=Vector3Add(muzzle, Vector3Scale(fwd,wd)); sn=Vector3Scale(fwd,-1.0f); landed=1; }
                    } else { Vector3 hp,hn;
                        if (RaycastWorld(muzzle, fwd, &hp,&hn)){ if (Vector3Distance(muzzle,hp)<=FLAME_RANGE){ sp=hp; sn=hn; landed=1; } } }
                    if (landed){ g_scorchT=0.0f; AddScorch(sp, sn, 0.40f+GetRandomValue(0,35)/100.0f); }
                }
            }
        } else {
            g_flameHeat = fmaxf(0.0f, g_flameHeat - dt*2.0f);           // cools off when you release -> warms up again next time
        }
    }

    // Knife (slot 8): a wide melee slash on each click. Hits the nearest enemy in a
    // short frontal arc; reuses the model's Shot clip as the swing if it has one.
    if (cw->melee && !g_reloading && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && g_fireCd<=0.0f){
        g_fireCd=KNIFE_CD; g_recoil=1.0f;                                  // a swing kick
        if (g_aShoot!=g_aIdle && g_aShoot<g_gunAnimN){ g_curAnim=g_aShoot; g_animOnce=1; g_animT=0.0f; }
        if (g_audio && cw->hasSnd) PlaySound(cw->fireSnd);
        int bestE=-1; float bestD=KNIFE_RANGE;
        for (int i=0;i<MAX_ENEMIES;i++){
            if (g_enemies[i].state!=1) continue;
            Vector3 cen=(Vector3){g_enemies[i].pos.x, g_enemies[i].pos.y+ENEMY_HEIGHT*0.5f, g_enemies[i].pos.z};
            Vector3 to=Vector3Subtract(cen, g_pos); float d=Vector3Length(to);
            if (d>KNIFE_RANGE || d<1e-3f) continue;
            Vector3 tn=Vector3Scale(to, 1.0f/d);
            if (Vector3DotProduct(tn, fwd) < KNIFE_COS) continue;          // outside the slash arc
            if (d<bestD){ bestD=d; bestE=i; }                              // nearest in-arc enemy
        }
        if (bestE>=0){
            Vector3 cen=(Vector3){g_enemies[bestE].pos.x, g_enemies[bestE].pos.y+ENEMY_HEIGHT*0.6f, g_enemies[bestE].pos.z};
            HurtEnemy(bestE, KNIFE_DMG, 0, fwd, cen, 1);
        }
    }

    // minigun: hold the trigger -> the barrel SPINS UP for MG_SPINUP (1/3 s) with no
    // bullets, then once it's at speed it fires full-auto (hard 5s cap -> overheat +
    // cooldown sound). Firing also drives g_shake (screen jitter + edge vignette).
    if (cw->spinUp){
        if (!lmb) g_mgLock=0;                              // releasing clears the overheat lock
        int trigger = lmb && !g_reloading && !g_mgLock;
        if (trigger){
            g_mgSpinT += dt;
            g_spin = fminf(1.0f, g_mgSpinT/MG_SPINUP);     // barrel accelerates, reaching full at MG_SPINUP
            if (g_audio && cw->hasAuxSnd && IsSoundPlaying(cw->auxSnd)) StopSound(cw->auxSnd);  // revving -> cut cooldown
            if (g_mgSpinT >= MG_SPINUP){                   // AT SPEED -> actually shoot bullets
                firingNow=1; Fire(g_cam); g_mgHeat+=dt;
                if (g_mgHeat>=5.0f) g_mgLock=1;            // overheated after 5s of fire
            }
            g_shake = fminf(1.0f, g_shake + dt*3.5f);      // brain-shake builds as it revs + fires
        } else {
            if (g_mgFiring && g_spin>0.3f && g_audio && cw->hasAuxSnd) PlaySound(cw->auxSnd);   // just stopped -> cooldown sound
            g_mgSpinT=0.0f;
            g_spin=fmaxf(0.0f, g_spin-dt*0.5f);            // barrel spins down (~2s to stop)
            if (g_spin<=0.0f && g_audio && cw->hasAuxSnd && IsSoundPlaying(cw->auxSnd)) StopSound(cw->auxSnd);  // stopped -> cut sound
            g_mgHeat=0.0f;
            g_shake=fmaxf(0.0f, g_shake-dt*4.0f);
        }
        g_mgFiring = (trigger && g_mgSpinT>=MG_SPINUP);    // "firing" = actually shooting (drives the cooldown-sound edge)
        // smoke/steam out of the spinning barrels: thick gunsmoke while firing, lighter
        // steam while the barrel spins down and cools. Emitted at the muzzle in world space.
        if (g_spin>0.02f){
            Vector3 upv=Vector3CrossProduct(right,fwd);                          // camera up
            Vector3 muzzle=Vector3Add(g_pos, Vector3Add(Vector3Scale(fwd,1.1f), Vector3Scale(upv,-0.08f))); // dead-centre of the barrel, a hair low
            if (g_mgFiring){ for(int q=0;q<4;q++) SpawnSmoke(muzzle,1); }        // firing: a dense fine stream
            else if (GetRandomValue(0,1)==0) SpawnSmoke(muzzle,0);              // spin-down/cooling: light steam
        }
    } else {
        g_shake=fmaxf(0.0f, g_shake-dt*4.0f);              // any other weapon -> shake fades out
    }

    // LMG: fires while its (one-shot) sound plays, then a fixed pause before the
    // next burst -- "when the sound ends, stop for 3/4 s, then it can shoot again".
    if (cw->soundGated){
        int playing = g_audio && cw->hasSnd && IsSoundPlaying(cw->fireSnd);
        if (g_lmgPause>0.0f) g_lmgPause-=dt;               // forced pause -> no fire
        else if (lmb && !g_reloading){
            if (playing) Fire(g_cam);                      // shooting during the burst
            else if (g_audio && cw->hasSnd) PlaySound(cw->fireSnd);   // start a burst (plays the whole sound once)
        }
        if (g_lmgWasPlaying && !playing && g_lmgPause<=0.0f) g_lmgPause=0.75f;   // sound ended -> 3/4 s pause
        g_lmgWasPlaying=playing;
    }

    // looped fire sound (rifle burst / minigun) follows the actual firing
    if (g_audio && cw->hasSnd && cw->sndLoop && !cw->soundGated){
        if (firingNow){ if (!IsSoundPlaying(cw->fireSnd)) PlaySound(cw->fireSnd); }
        else if (IsSoundPlaying(cw->fireSnd)) StopSound(cw->fireSnd);
    }

    UpdateEnemies(dt);
    UpdateSmoke(dt);
    UpdateGrenades(dt);

    // advance weapon animation. idle = HOLD the last frame of Take (static ready
    // pose, no wandering). One-shot clips (Shoot/Reload) play through, then snap
    // back to the held idle.
    if (g_gunAnimN>0 && g_curAnim<g_gunAnimN){
        int nf=ANIM_FRAMES(g_gunAnim[g_curAnim]);
        if (cw->grenade){
            // grenade launcher: one baked 'allanims' clip. HOLD the idle frame between
            // shots; R scrubs the reload sub-range (GL_RELOAD_FROM..TO) then snaps back.
            float hold = (cw->idleHold>=0)?(float)cw->idleHold:(float)GL_IDLE_FRAME;   // Z/X-tuned grip frame
            if (g_reloading){
                g_animT += dt*30.0f;
                if (g_animT >= (float)GL_RELOAD_TO){ g_reloading=0; g_animT=hold; }
            } else g_animT = (nf>0) ? hold : 0.0f;
        } else if (g_weapons[g_curWeapon].spinUp && g_curAnim==g_aIdle && !g_animOnce){
            // minigun: the barrel mesh is rigid (not skinned) and 'allanims' only
            // animates the arms -- so scrubbing it just flails the hands. Instead
            // HOLD the arms at a static grip and spin the barrel geometrically
            // (DrawViewmodel rotates the barrel mesh by g_mgBarrelAngle).
            // Z/X-tuned idleHold overrides the default grip frame.
            g_animT = (cw->idleHold>=0) ? (float)cw->idleHold : ((nf>0) ? (float)MG_IDLE_FRAME : 0.0f);
            g_mgBarrelAngle += dt*g_spin*42.0f;            // ~6.7 rev/s at full spin-up
            if (g_mgBarrelAngle>2.0f*PI) g_mgBarrelAngle-=2.0f*PI;
        } else if (!g_animOnce && (g_curAnim==g_aIdle || g_curAnim==g_aWalk)){
            // locomotion: if the rig has a Walk clip, loop it while moving; otherwise
            // (or when standing still) HOLD the idle clip on its ready frame.
            if (g_aWalk>=0 && moving){
                if (g_curAnim!=g_aWalk){ g_curAnim=g_aWalk; g_animT=0.0f; }
                int wf=ANIM_FRAMES(g_gunAnim[g_curAnim]);
                g_animT += dt*30.0f; if (wf>0 && g_animT>=wf) g_animT-=wf;   // loop the walk cycle
            } else {
                if (g_curAnim!=g_aIdle){ g_curAnim=g_aIdle; g_animT=0.0f; }
                int idf=ANIM_FRAMES(g_gunAnim[g_curAnim]);
                if (cw->idleHold>=0) g_animT=(float)cw->idleHold;                 // Z/X-tuned grip frame wins
                else if (g_aWalk>=0){ g_animT+=dt*30.0f; if (idf>0 && g_animT>=idf) g_animT-=idf; }  // real FPS rig (has Walk): LOOP the idle -- it grips in every frame
                else g_animT = idf>0 ? (float)(idf-1) : 0.0f;                     // legacy rigs: freeze on the ready frame
            }
        } else {
            g_animT += dt*30.0f;
            // nf<=0 (empty/malformed clip) counts as instantly finished, so a
            // zero-frame reload phase can't wedge g_reloading=1 forever.
            if (nf<=0 || g_animT>=nf){
                if (g_animOnce){
                    if (g_reloadStep>=0 && g_reloadStep+1<g_reloadSeqN){
                        g_reloadStep++;                  // next reload phase
                        g_curAnim=g_reloadSeq[g_reloadStep]; g_animT=0.0f;
                    } else {                             // sequence (or one-shot) done
                        g_reloadStep=-1; g_reloading=0; g_curAnim=g_aIdle; g_animOnce=0; g_animT=0.0f;
                    }
                } else g_animT = nf>0 ? g_animT-nf : 0.0f;
            }
        }
    }

    // overlay + inspect + reset
    if (IsKeyPressed(KEY_GRAVE) || IsKeyPressed(KEY_TAB)) g_devOverlay=!g_devOverlay;
    if (IsKeyPressed(KEY_V)) g_inspect=!g_inspect;
    if (IsKeyPressed(KEY_G)){ g_godMode=!g_godMode; if (g_godMode) g_playerHp=100.0f;   // refill on re-enable
        DebugLog("mode","\"godMode\":%s", g_godMode?"true":"false"); }
    if (IsKeyPressed(KEY_F) && g_hasMap){ g_noclip=!g_noclip; g_vy=0.0f;   // map: fly/noclip <-> walk
        DebugLog("mode","\"noclip\":%s", g_noclip?"true":"false"); }
    if (IsKeyPressed(KEY_N)){                          // N: toggle orient mode (no enemies, weapon-aim tuning)
        g_noEnemies=!g_noEnemies;
        for (int i=0;i<MAX_ENEMIES;i++) g_enemies[i].state=0;   // clear the field
        for (int c=0;c<MAX_CORPSES;c++) g_corpses[c].active=0;  // and clear corpses
        g_playerHp=100.0f;                             // full health (clears a leftover YOU DIED)
        if (!g_noEnemies) for (int i=0;i<5;i++) SpawnEnemy(i);  // bring the wave back
        DebugLog("mode","\"noEnemies\":%s", g_noEnemies?"true":"false");
    }
    // Number keys -> weapon slots. Custom order so the two showpieces lead: 1=minigun,
    // 2=flamethrower, 3=grenade launcher, then the rest. (g_weapons indices: 0 grenade
    // launcher,1 remington,2 minigun,3 LMG,4 AK,5 MP5,6 Benelli,7 flamethrower,8 knife.)
    static const int kSlotForKey[9]={2,7,0,1,3,4,5,6,8};
    for (int k=0;k<9;k++) if (IsKeyPressed(KEY_ONE+k)) SwitchWeapon(kSlotForKey[k]);
    if (IsKeyPressed(KEY_ZERO)){ Weapon *w=&g_weapons[g_curWeapon]; g_vmOff=w->off0; g_vmScale=w->scale0; g_vmYaw=w->yaw0; g_vmPitch=w->pitch0; g_vmRoll=w->roll0; }
    // Save: ENTER (or F5). On Mac F5 is a system key (dictation/keyboard light)
    // and gets eaten by the OS, so ENTER is the reliable bind. g_savedMsg flashes
    // an on-screen confirmation so you KNOW it wrote.
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_F5)){
        StashActiveTuning();
        SaveTune(g_weapons[g_curWeapon].tunePath,g_vmOff,g_vmScale,g_vmYaw,g_vmPitch,g_vmRoll,g_weapons[g_curWeapon].idleHold);
        if (g_weapons[g_curWeapon].pinGun || g_weapons[g_curWeapon].spinUp){ Weapon *w=&g_weapons[g_curWeapon]; SavePin(w->tunePath,w->pinOff,w->pinScale,w->pinYaw,w->pinPitch,w->pinRoll,w->armsROff); }
        g_savedMsg=2.0f;   // seconds to show "SAVED"
        DebugLog("vmsave","\"slot\":%d,\"label\":\"%s\",\"path\":\"%s\",\"saved\":true",
                 g_curWeapon, g_weapons[g_curWeapon].label, g_weapons[g_curWeapon].tunePath);
    }
    if (g_savedMsg>0) g_savedMsg-=GetFrameTime();
    // H ("home"): remember the current spot+facing as the player spawn (written to
    // spawn.txt, loaded at startup). Press it where you want to start next launch.
    // (F2 also works, but macOS eats the function keys, so H is the real bind.)
    if (IsKeyPressed(KEY_H) || IsKeyPressed(KEY_F2)){
        SaveSpawn(g_pos,g_yaw,g_pitch); g_spawnMsg=2.0f;
        DebugLog("spawnsave","\"pos\":[%.2f,%.2f,%.2f],\"yaw\":%.3f,\"pitch\":%.3f", g_pos.x,g_pos.y,g_pos.z,g_yaw,g_pitch);
    }
    if (g_spawnMsg>0) g_spawnMsg-=GetFrameTime();

    // live viewmodel tuning (faster in orient mode so a mispositioned weapon is
    // easy to sweep back into frame)
    float ns=dt*(g_noEnemies?3.0f:0.5f);
    float rs=dt*(g_noEnemies?120.0f:60.0f);
    float ss=g_noEnemies?3.0f:1.0f;
    Weapon *cwp=&g_weapons[g_curWeapon];
    int bStates = cwp->spinUp ? 3 : (cwp->pinGun ? 2 : 1);   // minigun: VIEWMODEL/ARMS/RIGHT-HAND; pinned gun: VIEWMODEL/GRIP; rest: viewmodel only
    if (IsKeyPressed(KEY_B) && bStates>1) g_tuneTarget=(g_tuneTarget+1)%bStates;
    if (g_tuneTarget>=bStates) g_tuneTarget=0;
    if (g_tuneTarget==2){                                                        // minigun: nudge the RIGHT hand independently (baked into the verts)
        Weapon *w=cwp;
        float np=ns/fmaxf(g_vmScale,1e-6f)*0.07f;
        Vector3 dv={0,0,0};
        if (IsKeyDown(KEY_I)) dv.y+=np;  if (IsKeyDown(KEY_K)) dv.y-=np;
        if (IsKeyDown(KEY_L)) dv.x+=np;  if (IsKeyDown(KEY_J)) dv.x-=np;
        if (IsKeyDown(KEY_O)) dv.z+=np;  if (IsKeyDown(KEY_U)) dv.z-=np;
        if (fabsf(dv.x)+fabsf(dv.y)+fabsf(dv.z)>0.0f){
            ApplyArmsRDelta(w,dv);
            w->armsROff=Vector3Add(w->armsROff,dv);
        }
    } else if (g_tuneTarget==1){                                                 // tune the gun grip (pinned) / whole-arms nudge (minigun)
        Weapon *w=cwp;
        float np=ns/fmaxf(g_vmScale,1e-6f)*0.07f;   // pinOff is in MODEL (bind-space) units -> convert the view-space step
        if (IsKeyDown(KEY_I)) w->pinOff.y+=np;  if (IsKeyDown(KEY_K)) w->pinOff.y-=np;
        if (IsKeyDown(KEY_L)) w->pinOff.x+=np;  if (IsKeyDown(KEY_J)) w->pinOff.x-=np;
        if (IsKeyDown(KEY_O)) w->pinOff.z+=np;  if (IsKeyDown(KEY_U)) w->pinOff.z-=np;
        if (IsKeyDown(KEY_EQUAL)) w->pinScale*=(1.0f+dt*ss);  if (IsKeyDown(KEY_MINUS)) w->pinScale*=(1.0f-dt*ss);
        if (IsKeyDown(KEY_RIGHT_BRACKET)) w->pinYaw+=rs;  if (IsKeyDown(KEY_LEFT_BRACKET)) w->pinYaw-=rs;
        if (IsKeyDown(KEY_APOSTROPHE)) w->pinPitch+=rs;   if (IsKeyDown(KEY_SEMICOLON)) w->pinPitch-=rs;
        if (IsKeyDown(KEY_PERIOD)) w->pinRoll+=rs;        if (IsKeyDown(KEY_COMMA)) w->pinRoll-=rs;
    } else {
        if (IsKeyDown(KEY_I)) g_vmOff.y+=ns;  if (IsKeyDown(KEY_K)) g_vmOff.y-=ns;
        if (IsKeyDown(KEY_L)) g_vmOff.x+=ns;  if (IsKeyDown(KEY_J)) g_vmOff.x-=ns;
        if (IsKeyDown(KEY_O)) g_vmOff.z+=ns;  if (IsKeyDown(KEY_U)) g_vmOff.z-=ns;
        if (IsKeyDown(KEY_EQUAL)) g_vmScale*=(1.0f+dt*ss);  if (IsKeyDown(KEY_MINUS)) g_vmScale*=(1.0f-dt*ss);
        if (IsKeyDown(KEY_RIGHT_BRACKET)) g_vmYaw+=rs;  if (IsKeyDown(KEY_LEFT_BRACKET)) g_vmYaw-=rs;
        if (IsKeyDown(KEY_APOSTROPHE)) g_vmPitch+=rs;   if (IsKeyDown(KEY_SEMICOLON)) g_vmPitch-=rs;
        if (IsKeyDown(KEY_PERIOD)) g_vmRoll+=rs;        if (IsKeyDown(KEY_COMMA)) g_vmRoll-=rs;
    }
    // Z / X scrub the ready-pose HOLD frame for the current weapon (find the frame where
    // the hands grip the gun). SHIFT = coarse (x10). Held key auto-repeats. ENTER saves it.
    {
        int xup=(IsKeyPressed(KEY_X)||IsKeyPressedRepeat(KEY_X)), xdn=(IsKeyPressed(KEY_Z)||IsKeyPressedRepeat(KEY_Z));
        if (xup||xdn){
            Weapon *w=&g_weapons[g_curWeapon];
            int clip = w->grenade ? 0 : g_aIdle;
            int maxf = (g_gunAnimN>0 && clip<g_gunAnimN) ? ANIM_FRAMES(g_gunAnim[clip]) : 1;
            if (w->idleHold<0) w->idleHold = w->grenade ? GL_IDLE_FRAME : (maxf>0?maxf-1:0);
            int step=(IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT))?10:1;
            w->idleHold += xup?step:-step;
            if (w->idleHold<0) w->idleHold=0;  if (maxf>0 && w->idleHold>=maxf) w->idleHold=maxf-1;
            DebugLog("idlehold","\"slot\":%d,\"frame\":%d,\"maxf\":%d", g_curWeapon, w->idleHold, maxf);
        }
    }
    if (IsKeyPressed(KEY_P))
        DebugLog("vmxform","\"off\":[%.3f,%.3f,%.3f],\"scale\":%.4f,\"yaw\":%.1f,\"pitch\":%.1f,\"roll\":%.1f",
                 g_vmOff.x,g_vmOff.y,g_vmOff.z,g_vmScale,g_vmYaw,g_vmPitch,g_vmRoll);
    if (g_noEnemies){   // orient mode: log the selected weapon + orientation whenever it changes
        static float lo[7]={1e9f,0,0,0,0,0,0};
        float cur[7]={g_vmOff.x,g_vmOff.y,g_vmOff.z,g_vmScale,g_vmYaw,g_vmPitch,g_vmRoll};
        int changed=0; for(int k=0;k<7;k++) if(fabsf(cur[k]-lo[k])>1e-4f) changed=1;
        if (changed){ for(int k=0;k<7;k++) lo[k]=cur[k];
            DebugLog("orient","\"weapon\":\"%s\",\"off\":[%.3f,%.3f,%.3f],\"scale\":%.5f,\"yaw\":%.1f,\"pitch\":%.1f,\"roll\":%.1f",
                     g_weapons[g_curWeapon].label,g_vmOff.x,g_vmOff.y,g_vmOff.z,g_vmScale,g_vmYaw,g_vmPitch,g_vmRoll);
        }
    }

    for (int i=0;i<MAX_TRACERS;i++) if (g_tracers[i].life>0) g_tracers[i].life-=dt;
    for (int i=0;i<MAX_SPARKS;i++) if (g_sparks[i].life>0){
        Spark *sp=&g_sparks[i];
        int resting=(sp->vel.x==0 && sp->vel.y==0 && sp->vel.z==0 && sp->pos.y<=sp->rest+0.03f);
        if (resting){
            if (!sp->lasting) sp->life-=dt;        // glints + fine spray fade; lasting blood puddles stay put
        } else {
            sp->life-=dt;
            sp->vel.y-=12.0f*dt;
            sp->pos=Vector3Add(sp->pos,Vector3Scale(sp->vel,dt));
            if (sp->pos.y<=sp->rest){              // hit the floor -> settle
                float hl=sqrtf(sp->vel.x*sp->vel.x+sp->vel.z*sp->vel.z);  // keep the horizontal heading for a directional splat
                if (hl>0.001f){ sp->ix=sp->vel.x/hl; sp->iz=sp->vel.z/hl; }
                sp->pos.y=sp->rest+0.015f; sp->vel=(Vector3){0,0,0};
                if (sp->lasting){ sp->life=1e9f; sp->life0=1e9f; }   // lasting blood puddle (full opacity)
                else if (sp->life<0.5f) sp->life=0.5f;             // brief impact glow
            }
        }
    }

    // Damage feedback + death (after every hp change this frame: enemy melee, void).
    if (g_playerHp < g_prevHp-0.01f)                                // took a hit -> punch the red flash
        g_hurt = fminf(1.0f, g_hurt + (g_prevHp-g_playerHp)*0.04f + 0.12f);
    g_prevHp = g_playerHp;
    if (g_hurt>0) g_hurt = fmaxf(0.0f, g_hurt - dt*1.5f);
    if (!g_godMode && !g_noEnemies && g_playerHp<=0.0f && !g_dead){ // killed -> enter the death/respawn loop
        g_dead=1; g_deadT=0.0f;
        if (g_audio && cw->hasSnd && IsSoundPlaying(cw->fireSnd)) StopSound(cw->fireSnd);
        DebugLog("death","\"kills\":%d,\"headshots\":%d", g_kills, g_headshots);
    }
}

static void DrawWorld(void) {
    if (g_hasMap){                          // SPIKE: draw the loaded .map (no collision yet)
        rlDisableBackfaceCulling();         // brush winding flips under the Z->Y axis swap
        if (g_hasScrollShader && g_mapMeshScrollAny){
            // per-mesh draw so flowing liquids/warpzones can scroll their UVs (tcMod scroll)
            float t=(float)GetTime();
            for (int i=0;i<g_map.meshCount;i++){
                float sx=g_mapMeshScroll[i*2], sy=g_mapMeshScroll[i*2+1];
                Vector2 off={ sx*t, sy*t };
                SetShaderValue(g_scrollShader,g_scrollLoc,&off,SHADER_UNIFORM_VEC2);
                DrawMesh(g_map.meshes[i], g_map.materials[g_map.meshMaterial[i]], g_map.transform);
            }
        } else {
            DrawModel(g_map,(Vector3){0,0,0},1.0f,WHITE);
        }
        rlEnableBackfaceCulling();
    } else {
        DrawModelEx(g_floor,(Vector3){0,0,0},(Vector3){0,1,0},0,(Vector3){ARENA*2,1,ARENA*2},WHITE);
        DrawModelEx(g_wall,(Vector3){0,WALL_H/2, ARENA},(Vector3){0,1,0}, 0,(Vector3){ARENA*2,WALL_H,1},WHITE);
        DrawModelEx(g_wall,(Vector3){0,WALL_H/2,-ARENA},(Vector3){0,1,0}, 0,(Vector3){ARENA*2,WALL_H,1},WHITE);
        DrawModelEx(g_wall,(Vector3){ ARENA,WALL_H/2,0},(Vector3){0,1,0},90,(Vector3){ARENA*2,WALL_H,1},WHITE);
        DrawModelEx(g_wall,(Vector3){-ARENA,WALL_H/2,0},(Vector3){0,1,0},90,(Vector3){ARENA*2,WALL_H,1},WHITE);
        for (int c=0;c<NUM_CRATES;c++)
            DrawModelEx(g_crate,g_crates[c].pos,(Vector3){0,1,0},0,g_crates[c].size,g_crates[c].col);
    }
    for (int i=0;i<MAX_TRACERS;i++) if (g_tracers[i].life>0)
        DrawLine3D(g_tracers[i].a,g_tracers[i].b,(Color){255,240,150,255});
    for (int i=0;i<MAX_SPARKS;i++) if (g_sparks[i].life>0){
        Spark *sp=&g_sparks[i];
        int settled=(sp->vel.x==0 && sp->vel.y==0 && sp->vel.z==0);
        if (sp->pool){                                     // flat continuous pool stain: many small overlapping flat discs
            float R=sp->size, y0=sp->rest+0.006f, y1=sp->rest+0.012f;  // filling a disc -> reads as one wet liquid pool
            unsigned int s=sp->seed?sp->seed:1u;
            for (int b=0;b<26;b++){
                unsigned int h=s*2654435761u + (unsigned int)b*2654435789u; h^=h>>13; h*=0x9E3779B1u;
                float t1=(float)((h>>3)&1023)/1023.0f, t2=(float)((h>>13)&1023)/1023.0f, t3=(float)((h>>23)&255)/255.0f;
                float a=t1*6.2831853f, rr=sqrtf(t2)*R;      // sqrt -> evenly fill the disc area
                float ox=cosf(a)*rr, oz=sinf(a)*rr;
                float dr=R*(0.34f+0.16f*t3)*(1.0f-0.45f*(rr/R));   // solid centre, smaller discs toward the ragged edge
                DrawCylinderEx((Vector3){sp->pos.x+ox,y0,sp->pos.z+oz},
                               (Vector3){sp->pos.x+ox,y1,sp->pos.z+oz}, dr, dr, 10, sp->col);
            }
        } else if (sp->lasting && settled){                // settled gob -> a small lumpy spatter dot (a couple offset discs)
            float ax=sp->ix, az=sp->iz; if (ax==0.0f && az==0.0f){ ax=1.0f; az=0.0f; }
            float y0=sp->rest+0.011f, y1=sp->rest+0.018f;   // a thin slab lying on the floor, not a ball
            unsigned int s=sp->seed?sp->seed:1u;
            for (int b=0;b<2;b++){                          // offset discs break the circular outline
                unsigned int h=s*2654435761u + (unsigned int)b*2246822519u;
                float t1=(float)((h>>3)&255)/255.0f, t2=(float)((h>>11)&255)/255.0f, t3=(float)((h>>19)&255)/255.0f;
                float along=(t1*1.4f-0.2f)*sp->size;        // mostly forward of impact (the spatter trails ahead)
                float side =(t2-0.5f)*1.2f*sp->size;        // lateral scatter
                float ox=ax*along - az*side, oz=az*along + ax*side;
                float r=sp->size*(0.30f+0.30f*t3);          // small, varied
                DrawCylinderEx((Vector3){sp->pos.x+ox,y0,sp->pos.z+oz},
                               (Vector3){sp->pos.x+ox,y1,sp->pos.z+oz}, r, r, 8, sp->col);
            }
        } else if (sp->blood){                             // in-flight blood -> thin streak stretched along travel
            float k=(sp->life0>0)?sp->life/sp->life0:1.0f;
            Color c=sp->col; c.a=(unsigned char)(k*255.0f);
            float spd=Vector3Length(sp->vel);
            float r=(sp->size>0.03f?0.03f:sp->size)*0.55f; // capped so even gobs stay droplet-sized, never grapefruits
            if (spd>0.5f){                                 // moving: a tapered teardrop pointing the way it flies
                Vector3 dirn=Vector3Scale(sp->vel,1.0f/spd);
                float len=sp->size*1.5f+spd*0.025f;
                Vector3 tail=Vector3Subtract(sp->pos,Vector3Scale(dirn,len));
                DrawCylinderEx(sp->pos, tail, r, r*0.25f, 6, c);
            } else {                                       // nearly stopped: a small droplet
                DrawSphereEx(sp->pos, r, 5, 5, c);
            }
        } else {                                           // impact glints: tiny bright spark
            float k=(sp->life0>0)?sp->life/sp->life0:1.0f;
            float sz=sp->size*(0.45f+0.55f*k);
            Color c=sp->col; c.a=(unsigned char)(k*255.0f);
            DrawSphereEx(sp->pos, sz*0.6f, 6, 6, c);
        }
    }
    DrawScorch();                                          // flamethrower soot, laid on the surfaces it touched
    for (int i=0;i<MAX_GREN;i++) if (g_gren[i].active)     // in-flight grenades: a small dark sphere
        DrawSphereEx(g_gren[i].pos, 0.13f, 8, 8, (Color){45,50,42,255});
}

// Soot smudges on a body: one soft dark billboard per flame-char mark, at its height
// up the body, nudged just outside the near surface (toward the camera) so the model
// doesn't occlude it. vscale compresses the heights for a prone corpse. base = feet.
static void DrawBurnMarks(Vector3 base, float vscale, const float *by, const float *br, int n){
    if (!g_hasSmokeTex) return;
    for (int k=0;k<n;k++){
        Vector3 bp={base.x, base.y + by[k]*vscale, base.z};
        float hx=g_cam.position.x-bp.x, hz=g_cam.position.z-bp.z, L=sqrtf(hx*hx+hz*hz);
        if (L>1e-3f){ bp.x+=hx*0.52f/L; bp.z+=hz*0.52f/L; }      // sit on the near surface, not the body axis
        DrawBillboard(g_cam, g_smokeTex, bp, br[k]*2.2f, (Color){14,11,9,230});
    }
}

// Draw all alive/dying enemies. The shared model is posed to each enemy's own
// animation cursor right before drawing, so they animate independently.
static void DrawEnemies(void){
    if (!g_hasEnemy) return;
    for (int i=0;i<MAX_ENEMIES;i++){
        Enemy *e=&g_enemies[i];
        if (e->state==0) continue;
        // Occlusion cull: if the map blocks the camera's view of the enemy's feet, chest
        // AND head, it's fully behind a wall/stairs -> don't draw it, so no part of the
        // model pokes through the geometry to the player's side. Any point visible -> draw
        // (so an enemy peeking around cover still shows).
        if (g_hasMap && g_mapCol.ready){
            const float hy[3]={0.15f,0.9f,1.7f}; int vis=0;
            for (int p=0;p<3;p++){
                Vector3 pt={e->pos.x, e->drawY+hy[p], e->pos.z};
                Vector3 to=Vector3Subtract(pt,g_cam.position); float dist=Vector3Length(to);
                if (dist<0.5f){ vis=1; break; }                      // basically on top of us -> visible
                float hit=MapRayNearest(g_cam.position, Vector3Scale(to,1.0f/dist), dist);
                if (hit<=0.0f || hit>=dist-0.15f){ vis=1; break; }   // nothing blocks this point
            }
            if (!vis) continue;
        }
        float dx=g_pos.x-e->pos.x, dz=g_pos.z-e->pos.z;       // face the player (yaw about +Y)
        float yaw=atan2f(dx,dz)*RAD2DEG + ENEMY_YAW_OFFSET;
        if (g_enemyAnimN>0) ANIM_APPLY(g_enemy, g_enemyAnim[e->state==2?g_eDeath:e->clip], e->animT);
        Vector3 dpos={e->pos.x, e->drawY, e->pos.z};          // smoothed height -> no stair-step jitter
        DrawModelEx(g_enemy, dpos, (Vector3){0,1,0}, yaw,     // no sink: dying body falls onto the floor, not through
                    (Vector3){ENEMY_SCALE,ENEMY_SCALE,ENEMY_SCALE}, WHITE);
        if (e->burnN>0) DrawBurnMarks(dpos, 1.0f, e->burnY, e->burnR, e->burnN);   // localised char where the flame touched
        if (e->state==1 && e->hp<ENEMY_HP){                   // HP bar above damaged enemies
            Vector3 hp={e->pos.x, e->pos.y+ENEMY_HEIGHT+0.3f, e->pos.z};
            DrawCube(hp, 0.6f*(e->hp/ENEMY_HP), 0.08f, 0.02f, (Color){230,60,60,255});
        }
    }
    // lasting corpses: the shared model posed at the death clip's settle frame, lying where each enemy fell
    if (g_enemyAnimN>0){
        ANIM_APPLY(g_enemy, g_enemyAnim[g_eDeath], (float)g_eDeathFrame);
        for (int c=0;c<MAX_CORPSES;c++) if (g_corpses[c].active){
            DrawModelEx(g_enemy, g_corpses[c].pos, (Vector3){0,1,0}, g_corpses[c].yaw,
                        (Vector3){ENEMY_SCALE,ENEMY_SCALE,ENEMY_SCALE}, WHITE);
            if (g_corpses[c].burnN>0){   // prone body: compress the mark heights so soot sits on it, not floating above
                Vector3 b={g_corpses[c].pos.x, g_corpses[c].pos.y+0.05f, g_corpses[c].pos.z};
                DrawBurnMarks(b, 0.30f, g_corpses[c].burnY, g_corpses[c].burnR, g_corpses[c].burnN);
            }
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

// Draw a weapon model but spin ONE rigid mesh about its local Z axis. The minigun
// barrel cluster isn't rigged, so we rotate its geometry directly. Mirrors how
// DrawModelEx composes its transform so every other mesh draws byte-identically.
static void DrawGunSpinMesh(Model model, Vector3 pos, float scale, int spinMesh, float angle,
                            unsigned long long gunMask, Vector3 armsOff){
    Matrix mt = MatrixMultiply(model.transform, MatrixMultiply(MatrixScale(scale,scale,scale), MatrixTranslate(pos.x,pos.y,pos.z)));
    Matrix spun = MatrixMultiply(MatrixRotateZ(angle), mt);   // spin in local space (barrels run along local Z, centred on X=Y=0)
    // The skinned ARMS (meshes not in gunMask) can be slid as a rigid unit onto the
    // grips (this rig only renders sanely at its baked pose -- see gun-pin memory --
    // so the hands can't be re-posed per frame; a model-space nudge seats them).
    Matrix armsM = MatrixMultiply(MatrixTranslate(armsOff.x,armsOff.y,armsOff.z), mt);
    for (int i=0;i<model.meshCount;i++){
        int isGun = (i<64) && ((gunMask>>i)&1ULL);
        Matrix m = (i==spinMesh)?spun:(isGun?mt:armsM);
        DrawMesh(model.meshes[i], model.materials[model.meshMaterial[i]], m);
    }
}

// Compose a raylib Transform (S, R quat, T) into a matrix exactly the way raylib's own
// skinning does (scale, then rotate, then translate).
static Matrix XformToMatrix(Transform t){
    return MatrixMultiply(MatrixMultiply(MatrixScale(t.scale.x,t.scale.y,t.scale.z),
                                         QuaternionToMatrix(t.rotation)),
                          MatrixTranslate(t.translation.x,t.translation.y,t.translation.z));
}
// Draw a weapon whose gun meshes are STATIC and detached from the skinned arms (the
// grenade launcher: 'allanims' moves only the arm bones; the launcher was authored
// loose at the model root). The arms pose normally. For the gun meshes we build the
// hand bone's bind->pose matrix FROM THE ANIMATION KEYFRAME DATA (the model's runtime
// boneMatrices are NOT reliably populated by this raylib build -- which is why the
// first pin attempt didn't move the gun at all) and ride the gun on it:
//   recentre gun on its own bind centre -> grip trim (rot/scale about the gun, offset
//   in hand-bind space so it turns WITH the hand) -> seat at the palm -> skin -> view.
// Default trim (zero) = the gun's centre sits exactly in the palm.
static void DrawPinnedGun(Model model, ModelAnimation *anim, float frameF, Vector3 pos, float scale,
                          int handBone, Vector3 gunC, Matrix align, Vector3 seat, unsigned long long gunMask, int authored,
                          Vector3 pinOff, float pinScale, float pinYaw, float pinPitch, float pinRoll){
    Matrix mt = MatrixMultiply(model.transform, MatrixMultiply(MatrixScale(scale,scale,scale), MatrixTranslate(pos.x,pos.y,pos.z)));
    Matrix gunMat = mt;
    if (handBone>=0 && anim && anim->keyframePoses && anim->keyframeCount>0){
        int kf=(int)frameF; if (kf<0) kf=0; if (kf>=anim->keyframeCount) kf=anim->keyframeCount-1;
        Matrix bindM = XformToMatrix(model.skeleton.bindPose[handBone]);
        Matrix poseM = XformToMatrix(anim->keyframePoses[kf][handBone]);
        Matrix skin  = MatrixMultiply(MatrixInvert(bindM), poseM);      // bind space -> posed space (raylib's skinning product)
        Matrix trim  = MatrixMultiply(MatrixScale(pinScale,pinScale,pinScale),
                       MatrixMultiply(MatrixRotateZ(DEG2RAD*pinRoll),
                       MatrixMultiply(MatrixRotateX(DEG2RAD*pinPitch), MatrixRotateY(DEG2RAD*pinYaw))));
        Matrix pre;
        if (authored){
            // The bake already places the gun exactly where the hand animation was
            // authored to grip it -> KEEP that placement (identity at zero trim); the
            // user trim only pivots about the gun's own centre for fine nudges.
            pre = MatrixMultiply(MatrixMultiply(MatrixTranslate(-gunC.x,-gunC.y,-gunC.z), trim),
                                 MatrixTranslate(gunC.x+pinOff.x, gunC.y+pinOff.y, gunC.z+pinOff.z));
        } else {
            // recentre on the gun -> auto-align its long axis to the palm axis -> user
            // trim -> seat at the palm midpoint (+offset) -> ride the hand -> viewmodel
            pre = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixTranslate(-gunC.x,-gunC.y,-gunC.z), align), trim),
                                 MatrixTranslate(seat.x+pinOff.x, seat.y+pinOff.y, seat.z+pinOff.z));
        }
        gunMat = MatrixMultiply(MatrixMultiply(pre, skin), mt);
    }
    for (int i=0;i<model.meshCount;i++){
        int isGun = (i<64) && ((gunMask>>i)&1ULL);                    // arms (blended skin) pose at mt; gun parts ride the hand
        DrawMesh(model.meshes[i], model.materials[model.meshMaterial[i]], isGun?gunMat:mt);
    }
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
    Matrix rot=MatrixMultiply(MatrixRotateZ(DEG2RAD*g_vmRoll), MatrixMultiply(MatrixRotateX(DEG2RAD*(g_vmPitch - r*4.0f)), MatrixRotateY(DEG2RAD*g_vmYaw)));
    // recenter: place the model's centroid AT target (model is far off-origin)
    Vector3 vpos=Vector3Subtract(target, Vector3Scale(Vector3Transform(g_gunCentroid,rot), g_vmScale));
    rlDrawRenderBatchActive();
#if !defined(_WIN32)
    glClear(GL_DEPTH_BUFFER_BIT);   // weapon always on top
#endif
    BeginMode3D(g_vmCam);
        g_gun.transform=rot;
        if (g_weapons[g_curWeapon].spinUp)               // minigun: spin the barrel mesh, hands held static (+ B-tuned arms nudge)
            DrawGunSpinMesh(g_gun, vpos, g_vmScale, MG_BARREL_MESH, g_mgBarrelAngle,
                            g_weapons[g_curWeapon].gunMask, g_weapons[g_curWeapon].pinOff);
        else if (g_weapons[g_curWeapon].pinGun){          // grenade launcher: ride the static gun on the animated hand
            Weapon *w=&g_weapons[g_curWeapon];
            ModelAnimation *an=(g_gunAnimN>0 && g_curAnim<g_gunAnimN)?&g_gunAnim[g_curAnim]:NULL;
            DrawPinnedGun(g_gun, an, g_animT, vpos, g_vmScale, w->handBone, w->gunBindC, w->pinAlign, w->pinSeat, w->gunMask, w->pinAuthored,
                          w->pinOff, w->pinScale, w->pinYaw, w->pinPitch, w->pinRoll);
        } else
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
    // kills + headshots + player health
    DrawText(TextFormat("KILLS %d", g_kills), W-160, 36, 22, (Color){255,230,120,255});
    DrawText(TextFormat("HEADSHOTS %d", g_headshots), W-160, 60, 18, (Color){255,140,50,255});
    if (g_spawnMsg>0){                                 // F2 confirmation
        const char *t="SPAWN SET"; int fs=26, tw=MeasureText(t,fs);
        DrawText(t, W/2-tw/2, H/2+70, fs, (Color){120,220,255,255});
    }
    // "HEADSHOT!" punch near the crosshair: full for ~0.65s, then fades over 0.45s
    if (g_hsFlash>0){
        const char *t="HEADSHOT!"; int fs=36, tw=MeasureText(t,fs), x=W/2-tw/2, y=H/2-74;
        float k=fminf(1.0f, g_hsFlash/0.45f);
        DrawText(t, x+2, y+2, fs, (Color){0,0,0,(unsigned char)(180*k)});
        DrawText(t, x,   y,   fs, (Color){255,70,40,(unsigned char)(255*k)});
    }
    DrawRectangle(20, H-44, 224, 24, (Color){0,0,0,150});
    DrawRectangle(22, H-42, (int)(220*g_playerHp/100.0f), 20, (Color){200,40,40,255});
    DrawText(TextFormat("HP %d", (int)g_playerHp), 28, H-40, 16, RAYWHITE);
    if (g_godMode) DrawText("GOD", 112, H-40, 16, (Color){120,220,255,255});   // invulnerable (dev)
    if (g_dead){                                                   // death overlay
        const char *t="YOU DIED"; int fs=72, tw=MeasureText(t,fs);
        DrawText(t, W/2-tw/2+3, H/2-58+3, fs, (Color){0,0,0,200});
        DrawText(t, W/2-tw/2,   H/2-58,   fs, (Color){220,40,40,255});
        char sub[64]; snprintf(sub,sizeof sub,"%d kills", g_kills);
        int sw=MeasureText(sub,28); DrawText(sub, W/2-sw/2, H/2+20, 28, (Color){235,210,150,255});
        if (g_deadT>1.0f){                                         // respawn prompt fades in after the beat
            const char *p="ENTER or click to respawn"; int pw=MeasureText(p,22);
            DrawText(p, W/2-pw/2, H/2+60, 22, (Color){200,200,210,255});
        }
    }
    // Big unmistakable mode banner so "floating" can be diagnosed: INSPECT mode
    // intentionally floats the gun in front of you; press V to get back to FP.
    if (g_inspect)
        DrawText("INSPECT MODE - press V for first-person", W/2-220, 30, 24, (Color){255,180,40,255});
    if (g_devOverlay){
        DrawRectangle(0,0,380,90,(Color){0,0,0,150});
        DrawText("CHERNOBYL 2  -  M16A3 (LMB fire, R reload)",6,6,12,GRAY);
        DrawText(TextFormat("%s  [%s]  1-9=weapon N=mode V=inspect G=god 0=reset", g_inspect?"INSPECT":"FP", g_weapons[g_curWeapon].label),8,22,16,LIME);
        if (g_noEnemies){   // orient mode panel - bigger + drop-shadowed for legibility
            DrawRectangle(6,96,600,430,(Color){0,0,0,215});
            DrawRectangleLines(6,96,600,430,(Color){255,210,60,255});
            TextSh("ORIENT MODE  (N = back to play)",18,104,28,YELLOW);
            TextSh(TextFormat("weapon:  %s",g_weapons[g_curWeapon].label),18,142,24,(Color){120,230,255,255});
            TextSh(TextFormat("off [%.2f %.2f %.2f]  scale %.5f",g_vmOff.x,g_vmOff.y,g_vmOff.z,g_vmScale),18,176,20,RAYWHITE);
            TextSh(TextFormat("yaw %.1f  pitch %.1f  roll %.1f",g_vmYaw,g_vmPitch,g_vmRoll),18,202,20,RAYWHITE);
            Color kc=(Color){235,235,140,255};
            TextSh("J / L      move left / right",18,236,20,kc);
            TextSh("K / I      move down / up",18,262,20,kc);
            TextSh("U / O      move back / forward",18,288,20,kc);
            TextSh("- / =      scale  smaller / bigger",18,314,20,kc);
            TextSh("[ / ]      yaw    left / right",18,340,20,kc);
            TextSh("; / '      pitch  down / up",18,366,20,kc);
            TextSh(", / .      roll   twist left / right",18,392,20,kc);
            TextSh(TextFormat("Z / X      pose-hold frame: %d  (SHIFT=x10)", g_weapons[g_curWeapon].idleHold), 18,410,19,(Color){255,200,120,255});
            if (g_weapons[g_curWeapon].pinGun || g_weapons[g_curWeapon].spinUp){
                Weapon *w=&g_weapons[g_curWeapon];
                const char *what = (g_tuneTarget==2) ? "RIGHT HAND" : (g_tuneTarget==1 ? (w->pinGun?"GUN GRIP":"BOTH ARMS") : "VIEWMODEL");
                TextSh(TextFormat("B          tuning: %s   %s", what, g_tuneTarget?"(IJKL/UO move)":""), 18,432,19,(Color){120,230,255,255});
                if (g_tuneTarget==1) TextSh(TextFormat("   off %.2f %.2f %.2f  scale %.2f  yaw %.0f pit %.0f rol %.0f", w->pinOff.x,w->pinOff.y,w->pinOff.z,w->pinScale,w->pinYaw,w->pinPitch,w->pinRoll), 18,452,18,(Color){200,220,255,255});
                if (g_tuneTarget==2) TextSh(TextFormat("   right-hand off %.3f %.3f %.3f", w->armsROff.x,w->armsROff.y,w->armsROff.z), 18,452,18,(Color){200,220,255,255});
            }
            TextSh("0 reset      ENTER save  (or F5)",18,474,19,(Color){170,255,170,255});
            if (g_savedMsg>0) TextSh(TextFormat("SAVED  ->  %s",g_weapons[g_curWeapon].tunePath),18,498,20,(Color){90,255,90,255});
        }
        DrawText(TextFormat("vm off %.2f %.2f %.2f  scale %.5f  yaw %.0f pit %.0f",
                 g_vmOff.x,g_vmOff.y,g_vmOff.z,g_vmScale,g_vmYaw,g_vmPitch),8,42,13,RAYWHITE);
        DrawText("IJKL/UO move  -/= scale  [/] yaw  ;/' pitch  P log",8,60,12,GRAY);
    }
}

// Sunset-gradient sky behind the world. The .map's sky surfaces (skies/*) are
// skipped by the loader, so those openings reveal this. A vertical gradient
// needs no yaw response; we just slide the horizon with pitch (look up -> more
// sky). Stands in for the named distant_sunset cubemap (its images weren't
// fetchable); swap in a real cubemap later if wanted.
static void DrawSky(void){
    int W=GetScreenWidth(), H=GetScreenHeight();
    float hy=H*0.5f + g_pitch*(float)H*0.55f; if (hy<0) hy=0; if (hy>H) hy=H;
    Color zenith={48,58,104,255}, horizon={232,138,70,255}, ground={42,34,40,255};
    if (hy>0) DrawRectangleGradientV(0,0,W,(int)hy, zenith, horizon);            // sky: blue -> sunset
    if (hy<H) DrawRectangleGradientV(0,(int)hy,W,H-(int)hy, horizon, ground);    // horizon haze -> dark
}

// Options/pause overlay drawn on top of the frozen world. Hover-highlighted
// buttons; clicks are handled in Update against the same MenuRect layout.
static void DrawMenu(void){
    int W=GetScreenWidth(), H=GetScreenHeight();
    DrawRectangle(0,0,W,H,(Color){0,0,0,165});
    const char *title="OPTIONS"; int ts=46, tw=MeasureText(title,ts);
    TextSh(title, W/2-tw/2, (int)MenuRect(0).y-78, ts, (Color){255,210,60,255});
    Vector2 mp=GetMousePosition();
    char fsl[40]; snprintf(fsl,sizeof fsl,"Fullscreen:  %s", g_fullscreen?"ON":"OFF");
    const char *labels[MENU_N]={fsl,"Select Map","Resume","Quit"};
    for (int i=0;i<MENU_N;i++){
        Rectangle r=MenuRect(i); int hov=CheckCollisionPointRec(mp,r);
        DrawRectangleRec(r, hov?(Color){58,82,120,240}:(Color){22,30,44,225});
        DrawRectangleLinesEx(r,2, hov?(Color){255,210,60,255}:(Color){120,140,170,255});
        int fs=28, lw=MeasureText(labels[i],fs);
        TextSh(labels[i], (int)(r.x+r.width/2-lw/2), (int)(r.y+r.height/2-fs/2), fs, RAYWHITE);
    }
    Rectangle last=MenuRect(MENU_N-1);
    TextSh("ESC to close", W/2-72, (int)(last.y+last.height+24), 18, GRAY);
}

// Minimal "PAUSED" overlay (P-pause). Unlike the options menu this keeps the
// cursor captured, so it's just a dimmer + centred label over the frozen world.
static void DrawPaused(void){
    int W=GetScreenWidth(), H=GetScreenHeight();
    DrawRectangle(0,0,W,H,(Color){0,0,0,120});
    const char *t="PAUSED"; int ts=64, tw=MeasureText(t,ts);
    TextSh(t, W/2-tw/2, H/2-ts/2, ts, (Color){255,210,60,255});
    const char *h="press P, ESC, or click to resume"; int hs=22, hw=MeasureText(h,hs);
    TextSh(h, W/2-hw/2, H/2+ts/2+12, hs, RAYWHITE);
}

// Edge vignette: black bands fading inward on all four sides (corners darken twice ->
// a tunnel-vision look). k=0..1 drives the darkness; used for the minigun brain-shake.
static void DrawVignette(float k){
    if (k<=0.001f) return;
    int W=GetScreenWidth(), H=GetScreenHeight();
    unsigned char a=(unsigned char)(fminf(1.0f,k)*105.0f);   // peripheral darkness (half the original 210)
    Color edge={0,0,0,a}, clear={0,0,0,0};
    int bw=W/6, bh=H/6;                            // narrow edge bands -> only the periphery darkens, centre stays clear
    DrawRectangleGradientH(0,0,bw,H, edge, clear);          // left
    DrawRectangleGradientH(W-bw,0,bw,H, clear, edge);       // right
    DrawRectangleGradientV(0,0,W,bh, edge, clear);          // top
    DrawRectangleGradientV(0,H-bh,W,bh, clear, edge);       // bottom
}

// Red screen feedback: a punch on taking damage (g_hurt) plus a steady glow when
// health is low, drawn as red edge bands. On death the whole screen washes dark red.
static void DrawDamageOverlay(void){
    int W=GetScreenWidth(), H=GetScreenHeight();
    float lowhp = (!g_godMode && !g_dead && g_playerHp>0 && g_playerHp<35.0f) ? (1.0f-g_playerHp/35.0f) : 0.0f;
    float k = fmaxf(g_hurt, lowhp*0.45f);
    if (k>0.001f){
        unsigned char a=(unsigned char)(fminf(1.0f,k)*150.0f);
        Color edge={150,0,0,a}, clear={150,0,0,0};
        int bw=W/4, bh=H/4;
        DrawRectangleGradientH(0,0,bw,H, edge, clear);          // left
        DrawRectangleGradientH(W-bw,0,bw,H, clear, edge);       // right
        DrawRectangleGradientV(0,0,W,bh, edge, clear);          // top
        DrawRectangleGradientV(0,H-bh,W,bh, clear, edge);       // bottom
    }
    if (g_dead) DrawRectangle(0,0,W,H,(Color){45,0,0,(unsigned char)(fminf(1.0f,g_deadT*1.2f)*170.0f)});
}

static void Frame(void) {
    static int frameNo=0;
    Update();
    BeginDrawing();
        ClearBackground((Color){70,90,110,255});
        if (g_hasMap) DrawSky();                  // gradient sky shows through the map's sky openings
        BeginMode3D(g_cam);
            DrawWorld();
            DrawEnemies();
            DrawSmoke(g_cam);                     // minigun muzzle smoke/steam (billboards, drawn last)
        EndMode3D();
        if (g_inspect && !g_noEnemies) DrawInspect();
        DrawViewmodel();
        DrawVignette(g_shake);                    // minigun: blurred/darkened edges while it rips
        DrawDamageOverlay();                       // red edge flash on damage / low hp / death wash
        DrawHUD();
        if (g_menu) DrawMenu();
        else if (g_paused) DrawPaused();
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

// ---- Map selection menu (startup) ------------------------------------------
// Scans maps/*.map and lets the player pick one before the game loads. Each
// entry shows the map's worldspawn "message" title and, if present, a thumbnail
// from maps/shots/<name>.png (extracted from the original pk3 by tools/stage_map.sh).
#define MAPSEL_MAX 128
typedef struct { char path[256]; char name[96]; char title[96]; Texture2D shot; int hasShot; } MapEntry;
static MapEntry g_mapList[MAPSEL_MAX]; static int g_mapListN=0;

// Pull the worldspawn "message" "..." title out of the first chunk of a .map.
static void MapReadTitle(const char *path, char *out, int outsz){
    out[0]=0; FILE *f=fopen(path,"r"); if(!f) return;
    char buf[8192]; size_t n=fread(buf,1,sizeof buf-1,f); buf[n]=0; fclose(f);
    const char *m=strstr(buf,"\"message\"");
    if (m){ m=strchr(m+9,'"'); if(m){ const char *e=strchr(m+1,'"');
        if(e && e-m-1<outsz){ int L=(int)(e-m-1); memcpy(out,m+1,L); out[L]=0; } } }
}
static int MapEntryCmp(const void *a, const void *b){ return strcmp(((const MapEntry*)a)->name,((const MapEntry*)b)->name); }

static void ScanMaps(void){
    g_mapListN=0;
    DIR *d=opendir("maps"); if(!d) d=opendir("../maps");
    const char *dir = d ? (opendir("maps")?"maps":"../maps") : "maps";
    if(!d) return; closedir(d);
    d=opendir(dir); if(!d) return;
    struct dirent *e;
    while ((e=readdir(d)) && g_mapListN<MAPSEL_MAX){
        const char *nm=e->d_name; size_t L=strlen(nm);
        if (L<5 || strcmp(nm+L-4,".map")) continue;
        MapEntry *me=&g_mapList[g_mapListN];
        snprintf(me->path,sizeof me->path,"%s/%s",dir,nm);
        snprintf(me->name,sizeof me->name,"%.*s",(int)(L-4),nm);
        MapReadTitle(me->path,me->title,sizeof me->title);
        if (!me->title[0]) snprintf(me->title,sizeof me->title,"%s",me->name);
        me->hasShot=0;
        char sp[300]; snprintf(sp,sizeof sp,"maps/shots/%s.png",me->name);
        if (!FileExists(sp)) snprintf(sp,sizeof sp,"../maps/shots/%s.png",me->name);
        if (FileExists(sp)){ me->shot=LoadTexture(sp); if(me->shot.id>0){ me->hasShot=1;
            SetTextureFilter(me->shot,TEXTURE_FILTER_BILINEAR); } }
        g_mapListN++;
    }
    closedir(d);
    qsort(g_mapList,g_mapListN,sizeof(MapEntry),MapEntryCmp);
}

// Modal map picker. Returns a malloc'd path to the chosen .map, or NULL to quit.
// Runs its own draw loop (the window/GL context already exists).
static char *RunMapSelect(void){
    ScanMaps();
    if (g_mapListN==0) return NULL;                       // nothing to pick -> built-in arena
    int sel=0, top=0, frame=0;
    EnableCursor();
    while (!WindowShouldClose()){
        frame++;
        int W=GetScreenWidth(), H=GetScreenHeight();
        // layout: a scrolling list on the left, a big preview on the right. Shrink the
        // row height so the whole list fits on screen (down to a floor); only longer
        // lists than that scroll.
        int listX=60, listW=W*42/100, listY=150, avail=H-listY-70;
        int rowH=44; if (g_mapListN*rowH>avail){ rowH=avail/g_mapListN; if(rowH<26)rowH=26; }
        int visible=avail/rowH; if(visible<1)visible=1;
        Vector2 mp=GetMousePosition();
        // input ---------------------------------------------------------------
        if (IsKeyPressed(KEY_DOWN)||IsKeyPressed(KEY_S)) sel=(sel+1)%g_mapListN;
        if (IsKeyPressed(KEY_UP)  ||IsKeyPressed(KEY_W)) sel=(sel-1+g_mapListN)%g_mapListN;
        sel -= (int)GetMouseWheelMove(); if(sel<0)sel=0; if(sel>=g_mapListN)sel=g_mapListN-1;
        for (int i=top;i<top+visible && i<g_mapListN;i++){
            Rectangle r={(float)listX,(float)(listY+(i-top)*rowH),(float)listW,(float)(rowH-6)};
            if (CheckCollisionPointRec(mp,r)){
                sel=i;
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) goto chosen;
            }
        }
        if (sel<top) top=sel; if (sel>=top+visible) top=sel-visible+1;
        if (IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_SPACE)) goto chosen;
        if (IsKeyPressed(KEY_ESCAPE)){ EnableCursor(); return NULL; }
        // draw ----------------------------------------------------------------
        BeginDrawing();
        ClearBackground((Color){14,16,22,255});
        DrawRectangleGradientV(0,0,W,H,(Color){20,24,34,255},(Color){10,11,16,255});
        TextSh("CHERNOBYL 2", 60, 48, 30, (Color){120,140,170,255});
        TextSh("SELECT MAP", 60, 84, 52, (Color){255,210,60,255});
        // preview panel (right)
        int pvX=listX+listW+40, pvW=W-pvX-60, pvY=listY, pvH=H-listY-70;
        if (pvW>80){
            DrawRectangle(pvX,pvY,pvW,pvH,(Color){0,0,0,150});
            DrawRectangleLines(pvX,pvY,pvW,pvH,(Color){90,105,130,255});
            MapEntry *me=&g_mapList[sel];
            if (me->hasShot){
                float ar=(float)me->shot.width/me->shot.height;
                int iw=pvW-20, ih=(int)(iw/ar); if(ih>pvH-70){ ih=pvH-70; iw=(int)(ih*ar);}
                DrawTexturePro(me->shot,(Rectangle){0,0,(float)me->shot.width,(float)me->shot.height},
                    (Rectangle){(float)(pvX+(pvW-iw)/2),(float)(pvY+10),(float)iw,(float)ih},(Vector2){0,0},0,WHITE);
            } else {
                const char *ns="(no preview)"; int nw=MeasureText(ns,22);
                DrawText(ns,pvX+(pvW-nw)/2,pvY+pvH/2-11,22,(Color){90,100,120,255});
            }
            int tw=MeasureText(me->title,30);
            TextSh(me->title, pvX+(pvW-tw)/2, pvY+pvH-44, 30, RAYWHITE);
        }
        // list (left)
        for (int i=top;i<top+visible && i<g_mapListN;i++){
            Rectangle r={(float)listX,(float)(listY+(i-top)*rowH),(float)listW,(float)(rowH-6)};
            int on=(i==sel), hov=CheckCollisionPointRec(mp,r);
            DrawRectangleRec(r, on?(Color){58,82,120,240}:(hov?(Color){34,44,62,230}:(Color){22,28,40,210}));
            DrawRectangleLinesEx(r,2, on?(Color){255,210,60,255}:(Color){70,84,108,255});
            TextSh(g_mapList[i].title, (int)r.x+16, (int)r.y+(rowH-6)/2-11, 22, on?(Color){255,235,150,255}:RAYWHITE);
        }
        if (g_mapListN>visible){
            char sc[32]; snprintf(sc,sizeof sc,"%d / %d", sel+1, g_mapListN);
            DrawText(sc, listX, listY+visible*rowH+8, 18, (Color){120,135,160,255});
        }
        TextSh("UP/DOWN or mouse to choose     ENTER to play     ESC to quit",
               60, H-44, 20, (Color){150,165,190,255});
        EndDrawing();
        if (g_shotFrame>0 && frame>=g_shotFrame){          // headless: capture the menu and exit
            TakeScreenshot("chernobyl2-shot.png");
            DebugLog("mapselect","\"menushot\":%d,\"maps\":%d", frame, g_mapListN);
            EnableCursor(); return NULL;
        }
    }
    EnableCursor(); return NULL;
  chosen:;
    char *out=(char*)malloc(256); snprintf(out,256,"%s",g_mapList[sel].path);
    for (int i=0;i<g_mapListN;i++) if (g_mapList[i].hasShot) UnloadTexture(g_mapList[i].shot);
    DisableCursor();
    return out;
}

// Choose a spawn facing that looks into open space, not a wall/corner. Casts a ring
// of horizontal rays from the eye and picks the heading whose narrow cone is clearest
// (min distance over a few sub-rays, so a lucky gap through a doorway doesn't win).
// Returns a yaw in the game's convention (forward = (sin yaw, 0, cos yaw)).
static float PickOpenYaw(Vector3 eye){
    if (!g_mapCol.ready) return 0.0f;
    const int N=24; const float R=120.0f;
    float best=-1.0f, bestYaw=0.0f;
    for (int i=0;i<N;i++){
        float yaw=(float)i/(float)N*6.2831853f, score=1e30f;
        for (int j=-1;j<=1;j++){                       // sample the heading +- ~10 deg
            float a=yaw+(float)j*0.18f;
            float d=MapRayNearest(eye,(Vector3){sinf(a),0.0f,cosf(a)},R);
            if (d<0.0f) d=R;                           // nothing in range -> wide open
            if (d<score) score=d;
        }
        if (score>best){ best=score; bestYaw=yaw; }
    }
    return bestYaw;
}

// Load g_mapPath and (re)initialise everything tied to the current map: geometry,
// the UV-scroll shader, player spawn + nav, and a fresh wave of enemies. Safe to
// call repeatedly (e.g. after the ESC "Select Map" button). g_mapPath==NULL -> the
// built-in arena. The one-time setup (weapons, enemy model, sounds) is NOT redone.
static void StartSelectedMap(void){
    if (g_mapPath){
        g_map=LoadQ3MapModel(g_mapPath,&g_hasMap);
        int tt=0; for(int mi=0;mi<g_map.meshCount;mi++) tt+=g_map.meshes[mi].triangleCount;
        DebugLog("map","\"path\":\"%s\",\"ok\":%s,\"textures\":%d,\"tris\":%d,\"lights\":%d,\"bake_sec\":%.2f",
                 JStr(g_mapPath), g_hasMap?"true":"false", g_map.meshCount, tt, g_nMapLights, g_mapAOsec);
        if (!g_hasMap) TraceLog(LOG_WARNING,"map: failed to load %s", g_mapPath);
    } else g_hasMap=0;

    // Animated UV-scroll shader for flowing liquids / warpzones (tcMod scroll):
    // raylib's default textured shader plus a uvOffset uniform. Built once, reused.
    if (g_hasMap && g_mapMeshScrollAny){
        if (!g_hasScrollShader){
            const char *vs="#version 330\n"
                "in vec3 vertexPosition; in vec2 vertexTexCoord; in vec4 vertexColor;\n"
                "uniform mat4 mvp; out vec2 fragTexCoord; out vec4 fragColor;\n"
                "void main(){ fragTexCoord=vertexTexCoord; fragColor=vertexColor; gl_Position=mvp*vec4(vertexPosition,1.0); }\n";
            const char *fs="#version 330\n"
                "in vec2 fragTexCoord; in vec4 fragColor;\n"
                "uniform sampler2D texture0; uniform vec4 colDiffuse; uniform vec2 uvOffset;\n"
                "out vec4 finalColor;\n"
                "void main(){ vec4 t=texture(texture0, fragTexCoord+uvOffset); finalColor=t*colDiffuse*fragColor; }\n";
            g_scrollShader=LoadShaderFromMemory(vs,fs);
            if (g_scrollShader.id>0){ g_scrollLoc=GetShaderLocation(g_scrollShader,"uvOffset"); g_hasScrollShader=1; }
        }
        if (g_hasScrollShader){
            for (int i=0;i<g_map.materialCount;i++) g_map.materials[i].shader=g_scrollShader;
            DebugLog("mapfx","\"scrollShader\":true");
        }
    }

    if (g_hasMap){                                    // start at a map spawn point, drop to the floor
        g_pos = g_hasSpawn ? (Vector3){g_mapSpawn.x, g_mapSpawn.y+2.0f, g_mapSpawn.z} : (Vector3){0,12,0};
        g_yaw=0.0f; g_pitch=0.0f; g_vy=0.0f; g_grounded=0;
        Vector3 sp; float sy,sp2; int haveCustom=0;   // a saved spawn (F2) overrides the map's start
        if (LoadSpawn(&sp,&sy,&sp2)){ g_pos=sp; g_yaw=sy; g_pitch=sp2; haveCustom=1;
            DebugLog("spawn","\"custom\":true,\"pos\":[%.1f,%.1f,%.1f]", sp.x,sp.y,sp.z); }
        if (g_lookSet){ g_yaw=g_lookYaw*0.01745329252f; g_pitch=g_lookPitch*0.01745329252f; }  // dev: aim camera
        else if (!haveCustom){ g_yaw=PickOpenYaw(g_pos);          // face the most open direction, not a wall
            DebugLog("spawnface","\"yaw\":%.1f", g_yaw*57.29578f); }
        MapNavBuild(g_pos);                           // flood the walkable nav grid from the start point
        DebugLog("nav","\"grid\":[%d,%d],\"walkable\":%d,\"ok\":%d", g_navNX, g_navNZ, g_navCount, g_navOK);
        g_spawnResetPos=g_pos; g_voidY=-12.0f;        // below the lowest floor (normalised to y=0) -> fell off
    } else { g_pos=(Vector3){0,EYE_H,6}; g_voidY=-1e9f; }

    g_playerHp=100.0f; g_vy=0.0f; g_eyeSmooth=0.0f;   // fresh start for the (new) map
    g_cam=(Camera3D){ g_pos, Vector3Add(g_pos,(Vector3){0,0,-1}), (Vector3){0,1,0}, 70.0f, CAMERA_PERSPECTIVE };
    for (int i=0;i<MAX_ENEMIES;i++) g_enemies[i].state=0;          // clear any survivors from the previous map
    if (g_hasEnemy && !g_noEnemies) for (int i=0;i<5;i++) SpawnEnemy(i);
}

// Tear down the current map before loading another. The scroll shader is shared by
// all map materials, so restore the default shader first or UnloadModel would free
// our shared one (it's kept for the next map and released at shutdown).
static void EndCurrentMap(void){
    if (!g_hasMap) return;
    if (g_hasScrollShader)
        for (int i=0;i<g_map.materialCount;i++){
            g_map.materials[i].shader.id   = rlGetShaderIdDefault();
            g_map.materials[i].shader.locs = rlGetShaderLocsDefault();
        }
    UnloadModel(g_map);
    g_hasMap=0;
}

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

int main(int argc, char **argv) {
    int vmWeapon=-1; float vmOv[7]={0}; int vmHas=0;          // dev: --weapon / --vm framing overrides
    for (int i=1;i<argc;i++){
        if (!strcmp(argv[i],"--debug")) g_debug=1;
        else if (!strcmp(argv[i],"--frames") && i+1<argc) g_maxFrames=atoi(argv[++i]);
        else if (!strcmp(argv[i],"--shot") && i+1<argc) g_shotFrame=atoi(argv[++i]);
        else if (!strcmp(argv[i],"--no-enemies")) g_noEnemies=1;
        else if (!strcmp(argv[i],"--map") && i+1<argc) g_mapPath=argv[++i];   // SPIKE: load a .map
        else if (!strcmp(argv[i],"--look") && i+2<argc){ g_lookYaw=(float)atof(argv[++i]); g_lookPitch=(float)atof(argv[++i]); g_lookSet=1; }   // dev: aim spawn camera (deg)
        else if (!strcmp(argv[i],"--weapon") && i+1<argc) vmWeapon=atoi(argv[++i]);   // dev: start on weapon N
        else if (!strcmp(argv[i],"--vm") && i+1<argc) vmHas=(sscanf(argv[++i],"%f %f %f %f %f %f %f",&vmOv[0],&vmOv[1],&vmOv[2],&vmOv[3],&vmOv[4],&vmOv[5],&vmOv[6])==7);   // dev: override viewmodel "x y z scale yaw pitch roll"
    }
    // Send the JSON event stream straight to a log file (not stdout/stderr, so
    // it stays clean of raylib's own warnings). Fresh file each run.
    if (g_debug){
        g_dbg=fopen("chernobyl2-debug.log","w");
        if (!g_dbg) g_dbg=stderr;   // fall back if the file can't be opened
    }

    SetTraceLogLevel(LOG_WARNING);
    SetTraceLogCallback(FilteredTraceLog);   // drop raylib's harmless u32->u16 index spam
    InitWindow(1280,720,"Chernobyl 2");
    SetExitKey(KEY_NULL);              // ESC opens the options menu instead of quitting
    int ready=IsWindowReady();
    if (ready){                        // soft radial puff for the minigun smoke billboards
        Image si=GenImageGradientRadial(64,64,0.25f,(Color){255,255,255,255},(Color){255,255,255,0});
        g_smokeTex=LoadTextureFromImage(si); UnloadImage(si);
        SetTextureFilter(g_smokeTex,TEXTURE_FILTER_BILINEAR); g_hasSmokeTex=(g_smokeTex.id!=0);
    }
    DebugLog("boot","\"windowReady\":%s,\"raylib\":\"%s\"", ready?"true":"false", RAYLIB_VERSION);

    const char *gunPath="assets/rifle.glb";
    if (!FileExists(gunPath)) gunPath="../assets/rifle.glb";
    DebugLog("model","\"path\":\"%s\"", JStr(gunPath));

    if (!ready){   // headless: nothing to render without a GL context
        DebugLog("shutdown","\"reason\":\"no-gl-context\"");
        return 0;
    }

    // Windowed (1280x720) by default. Fullscreen is opt-in via the in-game options
    // menu (ESC), which uses borderless windowed mode (resizes to the monitor's
    // resolution) and persists the choice; re-apply it here if it was left on.
    // Skipped under --shot so headless captures stay a deterministic 1280x720.
    // Everything renders off GetScreenWidth/Height + the framebuffer aspect, so
    // the HUD and 3D views adapt to whatever resolution we land on.
    LoadOptions();
    if (g_fullscreen && g_shotFrame<=0){
        ApplyFullscreen(1);
        DebugLog("window","\"borderless\":true,\"w\":%d,\"h\":%d,\"monitor\":%d",
                 GetScreenWidth(), GetScreenHeight(), GetCurrentMonitor());
    }

    SetTargetFPS(120);   // 120 Hz target (ProMotion etc.); all motion is dt-scaled so physics is unchanged
    DisableCursor();

    // window + Dock/Cmd-Tab icon
    {
        const char *ip="assets/icon.png"; if (!FileExists(ip)) ip="../assets/icon.png";
        if (FileExists(ip)){
            Image ic=LoadImage(ip);
            if (ic.data){ ImageFormat(&ic,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8); SetWindowIcon(ic); UnloadImage(ic); }
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
            MacSetDockIcon(ip);            // SetWindowIcon is ignored on macOS; set the Dock image directly
#endif
        }
    }

    InitAudioDevice();                 // weapon fire sounds; harmless if it fails
    g_audio = IsAudioDeviceReady();
    DebugLog("audio","\"ready\":%s", g_audio?"true":"false");

    if (g_audio){                      // a random one of these plays on every enemy kill
        const char *deaths[]={ "enemy_death1.mp3","enemy_death2.mp3","enemy_death3.mp3","enemy_death4.mp3" };
        for (int d=0; d<(int)(sizeof deaths/sizeof deaths[0]); d++){
            char p1[64],p2[64]; snprintf(p1,sizeof p1,"assets/%s",deaths[d]); snprintf(p2,sizeof p2,"../assets/%s",deaths[d]);
            const char *sp=FileExists(p1)?p1:(FileExists(p2)?p2:NULL);
            if (sp){ Sound s=LoadSound(sp); if (s.frameCount>0) g_deathSnd[g_nDeathSnd++]=s; }
        }
        const char *ap="assets/enemy_attack.mp3";
        if (!FileExists(ap)) ap="../assets/enemy_attack.mp3";
        if (FileExists(ap)){ g_attackSnd=LoadSound(ap); g_hasAttackSnd=(g_attackSnd.frameCount>0); }
        DebugLog("enemysnd","\"deaths\":%d,\"attack\":%s", g_nDeathSnd, g_hasAttackSnd?"true":"false");
    }

    // (Map selection + load happens in the per-map outer loop below, via
    // RunMapSelect() + StartSelectedMap(), so the ESC "Select Map" button can
    // return here without redoing the one-time setup.)

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

    g_vmCam=(Camera3D){ (Vector3){0,0,0}, (Vector3){0,0,-1}, (Vector3){0,1,0}, 55.0f, CAMERA_PERSPECTIVE };  // g_cam is set per-map in StartSelectedMap

    g_posedBasis[0]=1;   // grenade launcher: animated rig -> frame from the posed idle, not the bind pose
    LoadWeapon(0, "assets/grenadelauncher.glb", "../assets/grenadelauncher.glb", "Grenade Launcher", "vm_tune_grenade.txt",
               (Vector3){0,0,0}, -1.0f, 0.0f, 0.0f, 0.0f);   // auto-framed; tune live with N
    g_posedBasis[1]=1;   // remington: animated FPS rig
    LoadWeapon(1, "assets/remington.glb", "../assets/remington.glb", "Remington", "vm_tune_remington.txt",
               (Vector3){0,0,0}, -1.0f, 0.0f, 0.0f, 0.0f);   // auto-framed; tune live with N
    LoadWeapon(2, "assets/minigun.glb", "../assets/minigun.glb", "Minigun", "vm_tune_minigun.txt",
               (Vector3){ 0.098f, -0.143f, -0.637f }, 0.96758f, 184.1f, -6.1f, 0.0f);  // dialed in
    LoadWeapon(3, "assets/lmg.glb", "../assets/lmg.glb", "LMG", "vm_tune_lmg.txt",
               (Vector3){ -2.45f, -5.55f, 1.68f }, -1.0f, 180.0f, 0.0f, 0.0f);
    LoadWeapon(4, "assets/ak74.glb", "../assets/ak74.glb", "AK-74M", "vm_tune_ak.txt",
               (Vector3){ 0.200f, -0.218f, -0.642f }, 0.290106f, -176.0f, 88.1f, 0.0f);  // dialed in
    // new weapons (auto-framed; tune each live with N, ENTER saves). MP5 + Benelli
    // are clean rigs that animate fire+reload; the flamethrower/knife are model-only
    // for now (they hitscan -- no flame/melee mechanic yet).
    g_posedBasis[5]=1;   // MP5's bind pose is ~44k units off-origin; frame from the posed idle pose
    LoadWeapon(5, "assets/mp5.glb",          "../assets/mp5.glb",          "MP5",          "vm_tune_mp5.txt",     (Vector3){0.33f,-0.49f,-1.04f}, 0.000026f, 106.0f, -90.0f, 0.0f);
    LoadWeapon(6, "assets/benelli.glb",      "../assets/benelli.glb",      "Benelli M4",   "vm_tune_benelli.txt", (Vector3){0.30f,-0.40f,-1.08f}, 0.000021f, 104.0f, 90.0f, 0.0f);
    LoadWeapon(7, "assets/flamethrower.glb", "../assets/flamethrower.glb", "Flamethrower", "vm_tune_flame.txt",   (Vector3){0.05045f,-0.37347f,-0.43725f}, 0.021775f, -330.04f, 52.32f, 134.41f);
    LoadWeapon(8, "assets/knife.glb",        "../assets/knife.glb",        "Knife",        "vm_tune_knife.txt",   (Vector3){0,0,0}, -1.0f, 0.0f, 0.0f, 0.0f);
    g_numWeapons=9;
    ActivateWeapon( (vmWeapon>=0)?vmWeapon:0 );   // boot on slot 0 / grenade launcher (or --weapon N for tuning)
    if (vmHas){ g_vmOff=(Vector3){vmOv[0],vmOv[1],vmOv[2]}; g_vmScale=vmOv[3]; g_vmYaw=vmOv[4]; g_vmPitch=vmOv[5]; g_vmRoll=vmOv[6]; }

    // Per-weapon fire sounds. The two automatics loop while the trigger is held;
    // the shotgun fires one blast per shot.
    LoadWeaponSound(0, "assets/shotgun_fire.mp3", "../assets/shotgun_fire.mp3", 0);  // grenade launcher: a one-shot launch thunk (stand-in)
    LoadWeaponSound(1, "assets/shotgun_fire.mp3", "../assets/shotgun_fire.mp3", 0);  // remington shotgun: one-shot
    LoadWeaponSound(2, "assets/minigun_fire.mp3", "../assets/minigun_fire.mp3", 1);  // minigun
    LoadWeaponSound(3, "assets/lmg_fire.mp3",     "../assets/lmg_fire.mp3",     1);  // LMG (biggun)
    LoadWeaponSound(4, "assets/ak74_fire_single.mp3", "../assets/ak74_fire_single.mp3", 0);  // AK-74M: one clean shot per bullet (first crack trimmed out of the burst clip)
    LoadWeaponSound(5, "assets/rifle_fire.mp3",   "../assets/rifle_fire.mp3",   1);  // MP5: reuse the MG loop
    LoadWeaponSound(6, "assets/shotgun_fire.mp3", "../assets/shotgun_fire.mp3", 0);  // Benelli M4: one-shot per shot
    LoadWeaponAux(2, "assets/minigun_cooldown.mp3", "../assets/minigun_cooldown.mp3"); // minigun spin-down
    if (g_weapons[2].hasAuxSnd) SetSoundVolume(g_weapons[2].auxSnd, 0.4f);  // spin-down was too loud/abrupt -> soften it
    g_weapons[0].grenade=1;      // grenade launcher: lobs an arcing grenade, explodes on impact
    g_weapons[0].pinGun=1;       // its launcher mesh is detached from the arms -> ride it on the hand bone
    DebugPinProbe(&g_weapons[0]);   // boot ActivateWeapon ran BEFORE these flags -> probe explicitly now
    g_weapons[1].playShoot=1; g_weapons[1].fireCd=0.7f;   // remington: plays Shot clip per blast, semi-auto cadence; R plays Reload
    g_weapons[2].spinUp=1;       // minigun: 5s fire cap -> cooldown sound + lockout
    g_weapons[3].soundGated=1;   // LMG:     fire while the sound plays, then a 0.75s pause
    g_weapons[4].playShoot=1;    // AK-74M:  plays its Shot clip on fire; R plays AK_Reload
    g_weapons[5].playShoot=1;    // MP5:     plays its Shoot clip on fire; R plays Reload
    g_weapons[6].playShoot=1;    // Benelli: plays its Fire clip on fire; R plays Reload
    g_weapons[7].flame=1;        // Flamethrower: continuous fire cone while held
    g_weapons[8].melee=1;        // Knife:        wide melee slash per click
    LoadWeaponSound(7, "assets/flame_fire.mp3", "../assets/flame_fire.mp3", 1);  // flamethrower: looped roar (silent if asset absent)
    LoadWeaponSound(8, "assets/knife_swing.mp3","../assets/knife_swing.mp3", 0); // knife: one-shot swing (silent if asset absent)

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
        // Pick the death clip's "settled on the floor" frame. Many death anims end
        // with a trailing keyframe that snaps back to the standing start pose, so
        // freezing the corpse at the literal last frame leaves it standing. Walk
        // the root bone's vertical track and take the LATEST frame still near its
        // lowest point -- that's the body lying down, just before any spring-back.
        g_eDeathFrame = (g_enemyAnimN>0) ? ANIM_FRAMES(g_enemyAnim[g_eDeath])-1 : 0;
#if defined(RAYLIB_VERSION_MAJOR) && RAYLIB_VERSION_MAJOR >= 6
        if (g_enemyAnimN>0 && g_enemy.skeleton.boneCount>0){
            ModelAnimation *d=&g_enemyAnim[g_eDeath];
            int nf=ANIM_FRAMES(*d), root=0;
            for (int b=0;b<g_enemy.skeleton.boneCount;b++)
                if (strstr(g_enemy.skeleton.bones[b].name,"Hips")||strstr(g_enemy.skeleton.bones[b].name,"hips")){ root=b; break; }
            float minY=1e30f, maxY=-1e30f;
            for (int f=0;f<nf;f++){ float y=d->keyframePoses[f][root].translation.y; if(y<minY)minY=y; if(y>maxY)maxY=y; }
            float thresh=minY+(maxY-minY)*0.2f;            // "near the floor" band
            for (int f=nf-1;f>=0;f--)                      // latest frame still in that band
                if (d->keyframePoses[f][root].translation.y<=thresh){ g_eDeathFrame=f; break; }
        }
#endif
        DebugLog("enemydeath","\"frames\":%d,\"settleFrame\":%d", g_enemyAnimN>0?ANIM_FRAMES(g_enemyAnim[g_eDeath]):0, g_eDeathFrame);
        g_hasEnemy=(g_enemy.meshCount>0);
        BoundingBox eb=GetModelBoundingBox(g_enemy);
        DebugLog("enemy","\"meshes\":%d,\"bones\":%d,\"anims\":%d,\"size\":[%.1f,%.1f,%.1f]",
                 g_enemy.meshCount, g_enemy.skeleton.boneCount, g_enemyAnimN,
                 eb.max.x-eb.min.x, eb.max.y-eb.min.y, eb.max.z-eb.min.z);
    } else DebugLog("enemy","\"error\":\"assets/enemy.glb not found\"");
    // (the starting wave of enemies is spawned per-map in StartSelectedMap)

#if defined(__EMSCRIPTEN__)
    if (!g_mapPath){ char *p=RunMapSelect(); if (p) g_mapPath=p; }
    StartSelectedMap();
    emscripten_set_main_loop(Frame,0,1);
#else
    // Per-map outer loop: pick a map (unless one was named on the CLI), play it, and
    // loop back to the picker when the ESC menu's "Select Map" sets g_returnToMenu.
    for (;;){
        if (!g_mapPath){
            char *p=RunMapSelect();
            if (!p){ if (g_mapListN>0){ DebugLog("mapselect","\"quit\":true"); break; } } // user quit (no maps -> arena)
            else { g_mapPath=p; g_mapPathOwned=1; DebugLog("mapselect","\"path\":\"%s\"", JStr(g_mapPath)); }
        }
        StartSelectedMap();
        g_returnToMenu=0;
        int fc=0;
        while (!WindowShouldClose() && !g_quit && !g_returnToMenu){ Frame(); if (g_maxFrames>0 && ++fc>=g_maxFrames) break; }
        if (WindowShouldClose() || g_quit || g_maxFrames>0) break;   // real exit (headless --frames always exits)
        EndCurrentMap();                                             // "Select Map": tear down, back to the picker
        if (g_mapPathOwned){ free((void*)g_mapPath); g_mapPathOwned=0; }
        g_mapPath=NULL;
    }
#endif

    StashActiveTuning();
    for (int i=0;i<g_numWeapons;i++) SaveTune(g_weapons[i].tunePath,g_weapons[i].off,g_weapons[i].scale,g_weapons[i].yaw,g_weapons[i].pitch,g_weapons[i].roll,g_weapons[i].idleHold);
    DebugLog("shutdown","\"ok\":true");
    for (int i=0;i<g_numWeapons;i++) if (g_weapons[i].has){
        if (g_weapons[i].anim) UnloadModelAnimations(g_weapons[i].anim,g_weapons[i].animN);
        UnloadModel(g_weapons[i].model);
    }
    if (g_hasEnemy){ if (g_enemyAnim) UnloadModelAnimations(g_enemyAnim,g_enemyAnimN); UnloadModel(g_enemy); }
    UnloadModel(g_floor); UnloadModel(g_wall); UnloadModel(g_crate);
    if (g_hasScrollShader) UnloadShader(g_scrollShader);
    if (g_hasMap) UnloadModel(g_map);
    UnloadTexture(g_floorTex); UnloadTexture(g_wallTex); UnloadTexture(g_crateTex);
    if (g_audio){
        for (int i=0;i<g_numWeapons;i++){ if (g_weapons[i].hasSnd) UnloadSound(g_weapons[i].fireSnd);
                                          if (g_weapons[i].hasAuxSnd) UnloadSound(g_weapons[i].auxSnd); }
        CloseAudioDevice();
    }
    CloseWindow();
    return 0;
}
