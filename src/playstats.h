// playstats: anonymous play-session stats for ximg.app games.
// Sends, per session: game, version, platform, a random install id and session id,
// and the session's play time (a beat every 60 s of play and one at exit).
// No personal data. The server derives a country from the connection and does not
// keep the IP. Dashboard: https://stats.ximg.app/games/ (owner only).
// Fire-and-forget: never blocks the frame, silently does nothing offline.
//
//   PlayStatsInit("helldrop", "1.0", dataDir);   // once, after the window opens
//   PlayStatsUpdate(GetFrameTime());               // every frame
//   PlayStatsShutdown();                           // before CloseWindow()
//
// Set PLAYSTATS_OFF=1 in the environment (or never call Init) to send nothing;
// test, headless and capture runs must not call Init.
#ifndef PLAYSTATS_H
#define PLAYSTATS_H
#ifdef __cplusplus
extern "C" {
#endif
void PlayStatsInit(const char *game, const char *version, const char *dataDir);
void PlayStatsUpdate(float dt);
void PlayStatsShutdown(void);
#ifdef __cplusplus
}
#endif
#endif
