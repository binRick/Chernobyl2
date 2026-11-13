// ChernobylBridge.h - C interface between the ObjC iOS bridge and the C game.
//
// The bridge owns the EAGLContext + CADisplayLink + UI hierarchy. The game
// owns CORE state, gameplay simulation, and rendering. Everything that
// crosses the line between them lives in this header so neither side has to
// know about the other's framework dependencies.

#ifndef CHERNOBYL_BRIDGE_H
#define CHERNOBYL_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

// ── rcore_ios.c entry points (called by the bridge) ─────────────────────────
// Bridge -> rcore: pre-init dimensions and per-frame state.

void ChernobylRcoreSetScreenSize(int w, int h);
void ChernobylRcoreSetFbo(unsigned int fbo, unsigned int colorRbo);
void ChernobylRcoreSetEaglContext(void *eaglCtx);
void *ChernobylRcoreGetEaglContext(void);
void ChernobylRcoreRequestClose(void);
void ChernobylRcoreSetStorageBasePath(const char *path);

void ChernobylRcoreTouchBegin(int id, float x, float y);
void ChernobylRcoreTouchMove(int id, float x, float y);
void ChernobylRcoreTouchEnd(int id);

// ── game.c entry points (called by the bridge) ──────────────────────────────
// On iOS the game is no longer in charge of the top-level loop. The bridge
// drives these in the sequence: Init -> Step (per CADisplayLink) -> Shutdown.

void ChernobylGameInit(void);     // Equivalent of main() up to the while-loop
void ChernobylGameStep(void);     // One tick - wraps StepFrame() + per-frame tail
void ChernobylGameShutdown(void); // Cleanup currently after the while-loop in main()

// Bridge -> game: NSBundle.resourcePath for asset loads. Must be called BEFORE
// ChernobylGameInit so InitWindow's texture/sound loaders can resolve paths.
void ChernobylGameSetBundleDir(const char *path);

#ifdef __cplusplus
}
#endif

#endif // CHERNOBYL_BRIDGE_H
