// Chernobyl skeletal-animation prototype.
//
// Goal: de-risk the #1 step of the "raylib, evolved in place" upgrade path —
// prove that raylib's skeletal-animation pipeline loads a rigged glTF, skins
// it on the GPU/CPU, and plays its animation clips. This is the single
// cheapest thing to verify before committing to replacing Chernobyl's
// billboard-sprite enemies with real 3D animated characters.
//
// It is a STANDALONE viewer, not a patch to game.c — so the 11k-line core
// stays untouched while we confirm the API works on every ship target.
//
// Builds for (mirrors Chernobyl):
//   - macOS  (native, via Makefile)
//   - Web    (Emscripten -> WASM/WebGL2; see `make web`)
//
// Debug logging (follows Chernobyl's --debug convention, but emits JSON as
// requested — Chernobyl's own log is a 5 Hz human-readable text snapshot;
// this is the JSON evolution of that idea):
//   Run with --debug. The viewer writes a newline-delimited JSON event log
//   (one object per line) to stderr describing what loaded and what is
//   playing: model metadata (mesh/material/bone counts), every animation
//   clip (name, frame count, bone count), animation switches, and sampled
//   frames. debug.sh tee's the run into /tmp/skeletal_proto_debug.log.
//
//   Each line is a self-contained JSON object with at least {"t":<time>,
//   "ev":"<event>"}. Parse with: grep '^{' log | jq -c
//
//   Events: boot, model, anim, anim_change, frame (sampled), shutdown, error.
//
//   The `model` + `anim` events ARE the verification artifact: if boot reports
//   boneCount>0 and >=1 animation with frameCount>0, skinning works.
//
// Controls: SPACE / N = next clip, P = prev clip, R = toggle auto-rotate,
//           [ / ] = slower / faster playback, ESC = quit.
//
// Headless self-test: `--frames N` auto-quits after N rendered frames, so the
// model/anim verification events can be captured non-interactively, e.g.
//   ./build/skeletal_proto --debug --frames 8 2>&1 | grep '^{' | jq -c .
// ----------------------------------------------------------------------------

#include "raylib.h"
#include "raymath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#if defined(PLATFORM_WEB)
#include <emscripten/emscripten.h>
#endif

#define GLSL_VERSION_DESKTOP 330
#define GLSL_VERSION_ES      100

// raylib 6.0 redesigned skeletal animation: bones moved into Model.skeleton,
// ModelAnimation.frameCount -> keyframeCount, and UpdateModelAnimation takes a
// float frame (interpolated). These macros let the same source compile on both
// 6.0 (Chernobyl's pinned version) and 5.5 (current Homebrew).
#if defined(RAYLIB_VERSION_MAJOR) && RAYLIB_VERSION_MAJOR >= 6
  #define MDL_BONECOUNT(m) ((m).skeleton.boneCount)
  #define MDL_HASBIND(m)   ((m).skeleton.bones != NULL)
  #define ANIM_FRAMES(a)   ((a).keyframeCount)
#else
  #define MDL_BONECOUNT(m) ((m).boneCount)
  #define MDL_HASBIND(m)   ((m).bindPose != NULL)
  #define ANIM_FRAMES(a)   ((a).frameCount)
#endif

#ifndef DEFAULT_MODEL
#define DEFAULT_MODEL "assets/robot.glb"   // CC0, ships with raylib examples
#endif

// ---- Debug logging (Chernobyl's --debug convention, JSON output) --------
static int   g_debug     = 0;     // set by --debug
static FILE *g_dbgFile   = NULL;  // stderr when debug on
static int   g_maxFrames = 0;     // >0 = auto-quit after N frames (headless self-test)
static const char *g_animPath = NULL; // --anim <file>: load clips from here, not the model
                                       // (IQM splits mesh/anim; most glTF are self-contained)

static void DebugInit(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) g_debug = 1;
    }
    if (g_debug) g_dbgFile = stderr;
}

static void DebugLog(const char *ev, const char *fmt, ...) {
    if (!g_debug || !g_dbgFile) return;
    // Each line: {"t":<seconds>,"ev":"<ev>",<extra fields from fmt>}
    fprintf(g_dbgFile, "{\"t\":%.3f,\"ev\":\"%s\"", GetTime(), ev);
    if (fmt && fmt[0]) {
        va_list ap; va_start(ap, fmt);
        fputc(',', g_dbgFile);
        vfprintf(g_dbgFile, fmt, ap);
        va_end(ap);
    }
    fprintf(g_dbgFile, "}\n");
    fflush(g_dbgFile);
}

// JSON string escaper for model/clip names (names can contain quotes/backslash).
static const char *JStr(const char *s) {
    static char buf[256];
    size_t j = 0;
    if (!s) s = "";
    for (size_t i = 0; s[i] && j < sizeof(buf) - 2; i++) {
        char c = s[i];
        if (c == '"' || c == '\\') { buf[j++] = '\\'; buf[j++] = c; }
        else if (c == '\n')        { buf[j++] = '\\'; buf[j++] = 'n'; }
        else if ((unsigned char)c < 0x20) { /* skip control chars */ }
        else                        { buf[j++] = c; }
    }
    buf[j] = 0;
    return buf;
}

// ---- Shared state (file-scope so the Emscripten frame callback can reach it)
static Model           g_model;
static ModelAnimation *g_anims     = NULL;
static int             g_animCount = 0;
static int             g_animIndex = 0;
static float           g_animTime  = 0.0f;  // seconds into current clip
static float           g_animFps   = 30.0f; // playback rate
static float           g_speed     = 1.0f;
static int             g_autoRotate = 1;
static Camera3D        g_camera;
static float           g_modelScale = 1.0f;
static Vector3         g_modelCenter = {0};
static int             g_frameNo  = 0;

static void LogActiveClip(const char *ev) {
    if (g_animCount <= 0) { DebugLog(ev, "\"clips\":0"); return; }
    ModelAnimation a = g_anims[g_animIndex];
    DebugLog(ev, "\"idx\":%d,\"name\":\"%s\",\"frames\":%d,\"bones\":%d",
             g_animIndex, JStr(a.name), ANIM_FRAMES(a), a.boneCount);
}

static void UpdateDrawFrame(void) {
    // --- input ---
    if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_N)) {
        if (g_animCount > 0) {
            g_animIndex = (g_animIndex + 1) % g_animCount;
            g_animTime = 0; LogActiveClip("anim_change");
        }
    }
    if (IsKeyPressed(KEY_P)) {
        if (g_animCount > 0) {
            g_animIndex = (g_animIndex - 1 + g_animCount) % g_animCount;
            g_animTime = 0; LogActiveClip("anim_change");
        }
    }
    if (IsKeyPressed(KEY_R))            g_autoRotate = !g_autoRotate;
    if (IsKeyPressed(KEY_LEFT_BRACKET))  g_speed = fmaxf(0.1f, g_speed - 0.25f);
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) g_speed = fminf(4.0f, g_speed + 0.25f);

    if (g_autoRotate) UpdateCamera(&g_camera, CAMERA_ORBITAL);

    // --- advance + apply skeletal animation ---
    if (g_animCount > 0) {
        ModelAnimation a = g_anims[g_animIndex];
        int nframes = ANIM_FRAMES(a);
        if (nframes > 0) {
            g_animTime += GetFrameTime() * g_animFps * g_speed;
            float frameF = fmodf(g_animTime, (float)nframes);
#if defined(RAYLIB_VERSION_MAJOR) && RAYLIB_VERSION_MAJOR >= 6
            UpdateModelAnimation(g_model, a, frameF);        // 6.0: float, interpolated
#else
            UpdateModelAnimation(g_model, a, (int)frameF);   // 5.5: integer frame
#endif
        }
    }

    // --- sampled frame telemetry (every 30 rendered frames) ---
    if (g_debug && (g_frameNo % 30 == 0)) {
        int frame = (g_animCount > 0 && ANIM_FRAMES(g_anims[g_animIndex]) > 0)
                        ? (int)g_animTime % ANIM_FRAMES(g_anims[g_animIndex]) : 0;
        DebugLog("frame", "\"n\":%d,\"fps\":%d,\"clip\":%d,\"f\":%d,"
                          "\"cam\":[%.2f,%.2f,%.2f]",
                 g_frameNo, GetFPS(), g_animIndex, frame,
                 g_camera.position.x, g_camera.position.y, g_camera.position.z);
    }
    g_frameNo++;

    // --- draw ---
    BeginDrawing();
        ClearBackground((Color){ 24, 26, 30, 255 });
        BeginMode3D(g_camera);
            DrawModel(g_model, (Vector3){0, 0, 0}, g_modelScale, WHITE);
            DrawGrid(20, 1.0f);
        EndMode3D();

        // HUD
        DrawRectangle(0, 0, 360, 116, (Color){ 0, 0, 0, 150 });
        DrawText("raylib skeletal-anim prototype", 12, 10, 18, RAYWHITE);
        if (g_animCount > 0) {
            DrawText(TextFormat("clip %d/%d  \"%s\"", g_animIndex + 1, g_animCount,
                                g_anims[g_animIndex].name),
                     12, 36, 16, LIME);
            DrawText(TextFormat("frames: %d   speed: %.2fx",
                                ANIM_FRAMES(g_anims[g_animIndex]), g_speed),
                     12, 58, 16, RAYWHITE);
        } else {
            DrawText("NO ANIMATIONS in model (static mesh only)", 12, 36, 16, RED);
        }
        DrawText(TextFormat("bones: %d   meshes: %d", MDL_BONECOUNT(g_model), g_model.meshCount),
                 12, 80, 16, SKYBLUE);
        DrawText("SPACE/N next  P prev  R rotate  [ ] speed", 12, 98, 14, GRAY);
        DrawFPS(GetScreenWidth() - 90, 10);
    EndDrawing();
}

// Compute a uniform scale + recenter so any model fits a ~4-unit viewport.
static void FitModelToView(void) {
    BoundingBox bb = GetModelBoundingBox(g_model);
    Vector3 size = Vector3Subtract(bb.max, bb.min);
    float maxDim = fmaxf(size.x, fmaxf(size.y, size.z));
    g_modelScale = (maxDim > 0.0001f) ? (4.0f / maxDim) : 1.0f;
    g_modelCenter = (Vector3){ (bb.min.x + bb.max.x) * 0.5f,
                               (bb.min.y + bb.max.y) * 0.5f,
                               (bb.min.z + bb.max.z) * 0.5f };
}

int main(int argc, char **argv) {
    DebugInit(argc, argv);

    // Args: [model.glb] [--debug] [--frames N]. First non-flag token (that
    // isn't the value of --frames) overrides the default model path.
    const char *modelPath = DEFAULT_MODEL;
    int gotModel = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            g_maxFrames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--anim") == 0 && i + 1 < argc) {
            g_animPath = argv[++i];
        } else if (argv[i][0] != '-' && !gotModel) {
            modelPath = argv[i]; gotModel = 1;
        }
    }
    const char *animPath = g_animPath ? g_animPath : modelPath;

    if (!FileExists(modelPath)) {
        DebugLog("error", "\"msg\":\"model not found\",\"path\":\"%s\"", JStr(modelPath));
        TraceLog(LOG_ERROR, "model not found: %s (run ./run.sh to fetch it)", modelPath);
        return 1;
    }

    SetTraceLogLevel(LOG_WARNING);   // keep raylib's own logs out of the JSONL
    InitWindow(1024, 768, "Chernobyl skeletal-anim prototype");
    // InitWindow can fail to create a GL context in a headless/CI context
    // (no WindowServer). IsWindowReady() tells us; if false we still run the
    // CPU-only animation-parsing verification (LoadModelAnimations needs no
    // GPU) but skip LoadModel/rendering, which WOULD segfault without a context.
    int windowReady = IsWindowReady();
    if (windowReady) SetTargetFPS(60);

#if defined(PLATFORM_WEB)
    DebugLog("boot", "\"platform\":\"web\",\"glsl\":%d,\"windowReady\":%s,\"model\":\"%s\"",
             GLSL_VERSION_ES, windowReady ? "true" : "false", JStr(modelPath));
#else
    DebugLog("boot", "\"platform\":\"native\",\"glsl\":%d,\"windowReady\":%s,\"model\":\"%s\"",
             GLSL_VERSION_DESKTOP, windowReady ? "true" : "false", JStr(modelPath));
#endif

    // Animations parse on the CPU — load them first so the core verification
    // (clip names / frame counts / bone counts) works even with no GL context.
    g_anims = LoadModelAnimations(animPath, &g_animCount);

    if (windowReady) {
        g_model = LoadModel(modelPath);
        DebugLog("model", "\"path\":\"%s\",\"animPath\":\"%s\",\"meshes\":%d,\"materials\":%d,"
                          "\"bones\":%d,\"transforms\":%s,\"clips\":%d",
                 JStr(modelPath), JStr(animPath), g_model.meshCount, g_model.materialCount,
                 MDL_BONECOUNT(g_model), MDL_HASBIND(g_model) ? "true" : "false", g_animCount);
    } else {
        // No GPU: report what we can without touching the Model.
        int bones = (g_animCount > 0) ? g_anims[0].boneCount : 0;
        DebugLog("model", "\"path\":\"%s\",\"animPath\":\"%s\",\"meshes\":\"skipped-no-gl\","
                          "\"bones\":%d,\"clips\":%d",
                 JStr(modelPath), JStr(animPath), bones, g_animCount);
    }

    // One line per clip so you can see exactly what the rig exposes.
    // IsModelAnimationValid needs the Model, so it's only meaningful when the
    // model loaded; headless reports validity as "unchecked".
    for (int i = 0; i < g_animCount; i++) {
        ModelAnimation a = g_anims[i];
        const char *valid = windowReady
            ? (IsModelAnimationValid(g_model, a) ? "true" : "false") : "\"unchecked\"";
        DebugLog("anim", "\"idx\":%d,\"name\":\"%s\",\"frames\":%d,\"bones\":%d,\"valid\":%s",
                 i, JStr(a.name), ANIM_FRAMES(a), a.boneCount, valid);
    }

    if (!windowReady) {
        // Headless: verification done (clips parsed). Nothing to render.
        DebugLog("shutdown", "\"frames\":0,\"reason\":\"no-gl-context\"");
        UnloadModelAnimations(g_anims, g_animCount);
        return 0;
    }

    FitModelToView();

    g_camera = (Camera3D){
        .position   = (Vector3){ 6.0f, 4.0f, 6.0f },
        .target     = (Vector3){ 0.0f, 1.5f, 0.0f },
        .up         = (Vector3){ 0.0f, 1.0f, 0.0f },
        .fovy       = 50.0f,
        .projection = CAMERA_PERSPECTIVE,
    };

#if defined(PLATFORM_WEB)
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
    while (!WindowShouldClose()) {
        UpdateDrawFrame();
        if (g_maxFrames > 0 && g_frameNo >= g_maxFrames) break;
    }
#endif

    DebugLog("shutdown", "\"frames\":%d", g_frameNo);

    UnloadModelAnimations(g_anims, g_animCount);
    UnloadModel(g_model);
    CloseWindow();
    return 0;
}
