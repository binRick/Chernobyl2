// playstats.c: see playstats.h. Endpoint https://ximg.app/api/play/{start,beat}.
#include "playstats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
EM_JS(void, ps_js_post, (const char *url, const char *body, int beacon), {
    try {
        var u = UTF8ToString(url);
        var b = UTF8ToString(body);
        if (beacon && navigator.sendBeacon) navigator.sendBeacon(u, new Blob([b], {type: 'text/plain'}));
        else fetch(u, {method: 'POST', body: b, keepalive: true}).catch(function () {});
    } catch (e) {}
});
EM_JS(char *, ps_js_install, (const char *game, const char *fresh), {
    var k = 'ximg.playstats.' + UTF8ToString(game);
    var v = null;
    try { v = localStorage.getItem(k); if (!v) { v = UTF8ToString(fresh); localStorage.setItem(k, v); } }
    catch (e) { v = UTF8ToString(fresh); }
    return stringToNewUTF8(v);
});
EM_JS(void, ps_js_arm_pagehide, (const char *url, const char *game, const char *session), {
    var u = UTF8ToString(url) + 'beat';
    var g = UTF8ToString(game);
    var s = UTF8ToString(session);
    Module.__playstats = { seconds: 0 };
    addEventListener('pagehide', function () {
        try {
            navigator.sendBeacon(u, new Blob([JSON.stringify({game: g, session: s,
                seconds: Module.__playstats.seconds, end: 1})], {type: 'text/plain'}));
        } catch (e) {}
    });
});
EM_JS(void, ps_js_seconds, (int s), { if (Module.__playstats) Module.__playstats.seconds = s; });
#endif

#define PS_URL "https://ximg.app/api/play/"
#define PS_BEAT_SECONDS 60.0f

static int   ps_on = 0;
static char  ps_game[32], ps_version[24], ps_session[40], ps_install[40];
static double ps_played = 0.0;
static float ps_sinceBeat = 0.0f;

#if defined(__EMSCRIPTEN__)
#  define PS_PLATFORM "web"
#elif defined(__APPLE__) && TARGET_OS_IPHONE
#  define PS_PLATFORM "ios"
#elif defined(__APPLE__)
#  define PS_PLATFORM "macos"
#elif defined(_WIN32)
#  define PS_PLATFORM "windows"
#elif defined(__ANDROID__)
#  define PS_PLATFORM "android"
#else
#  define PS_PLATFORM "linux"
#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE
// playstats_ios.m: the POST, the install id (NSUserDefaults: the bundle is read-only
// and the cwd is usually inside it) and the real bundle version.
void PlayStatsPostIOS(const char *url, const char *json);
void PlayStatsIOSInstallId(const char *fresh, char *out, int cap);
void PlayStatsIOSVersion(char *out, int cap);
#endif

// Own generator: never touches rand(), so a game's seeded randomness is unchanged.
static unsigned long long ps_seed;
static unsigned long long ps_next(void) {
    ps_seed ^= ps_seed << 13; ps_seed ^= ps_seed >> 7; ps_seed ^= ps_seed << 17; return ps_seed;
}
static void ps_randid(char *out, int len) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < len; i++) out[i] = hex[ps_next() & 15];
    out[len] = 0;
}

static void ps_post(const char *what, const char *json) {
    char url[96];
    snprintf(url, sizeof url, PS_URL "%s", what);
#if defined(__EMSCRIPTEN__)
    ps_js_post(url, json, strstr(json, "\"end\":1") != NULL);
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    PlayStatsPostIOS(url, json);
#elif defined(_WIN32)
    char esc[512]; int n = 0;
    for (const char *p = json; *p && n < (int)sizeof esc - 2; p++) { if (*p == '"') esc[n++] = '\\'; esc[n++] = *p; }
    esc[n] = 0;
    char cmd[800];
    snprintf(cmd, sizeof cmd, "start /b \"\" curl -s -m 8 -o NUL -X POST -H \"Content-Type: application/json\" -d \"%s\" %s", esc, url);
    system(cmd);
#else
    // json holds only [A-Za-z0-9._:{},"-] (ids and versions are sanitised), so single quotes are safe.
    char cmd[640];
    snprintf(cmd, sizeof cmd, "curl -s -m 8 -o /dev/null -X POST -H 'Content-Type: application/json' -d '%s' '%s' >/dev/null 2>&1 &",
             json, url);
    int r = system(cmd); (void)r;
#endif
}

static void ps_clean(char *dst, const char *src, int cap) {
    int n = 0;
    for (; src && *src && n < cap - 1; src++) {
        char c = *src;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')
            dst[n++] = c;
    }
    dst[n] = 0;
}

static void ps_load_install(const char *dataDir) {
#if defined(__APPLE__) && TARGET_OS_IPHONE
    (void)dataDir;
    char fresh[33], got[64]; ps_randid(fresh, 32);
    PlayStatsIOSInstallId(fresh, got, sizeof got);
    ps_clean(ps_install, got, sizeof ps_install);
    if (strlen(ps_install) < 16) snprintf(ps_install, sizeof ps_install, "%s", fresh);
#elif defined(__EMSCRIPTEN__)
    char fresh[33]; ps_randid(fresh, 32);
    char *got = ps_js_install(ps_game, fresh);
    ps_clean(ps_install, got, sizeof ps_install);
    free(got);
#else
    char path[1024];
    snprintf(path, sizeof path, "%s%splaystats.id", dataDir && *dataDir ? dataDir : ".",
             dataDir && *dataDir && dataDir[strlen(dataDir) - 1] != '/' ? "/" : "");
    FILE *f = fopen(path, "r");
    char buf[64] = {0};
    if (f) { if (!fgets(buf, sizeof buf, f)) buf[0] = 0; fclose(f); }
    ps_clean(ps_install, buf, sizeof ps_install);
    if (strlen(ps_install) < 16) {
        ps_randid(ps_install, 32);
        if ((f = fopen(path, "w"))) { fputs(ps_install, f); fclose(f); }
    }
#endif
}

static void ps_beat(int end) {
    char json[256];
    snprintf(json, sizeof json, "{\"game\":\"%s\",\"session\":\"%s\",\"seconds\":%d,\"end\":%d}",
             ps_game, ps_session, (int)ps_played, end);
    ps_post("beat", json);
}

void PlayStatsInit(const char *game, const char *version, const char *dataDir) {
    const char *off = getenv("PLAYSTATS_OFF");
    if (ps_on || (off && *off && *off != '0')) return;
    ps_seed = (unsigned long long)time(NULL) * 0x9E3779B97F4A7C15ull ^ (unsigned long long)(size_t)&ps_on ^ (unsigned long long)clock();
    if (!ps_seed) ps_seed = 88172645463325252ull;
    for (int i = 0; i < 8; i++) ps_next();
    ps_clean(ps_game, game, sizeof ps_game);
    ps_clean(ps_version, version, sizeof ps_version);
#if defined(__APPLE__) && TARGET_OS_IPHONE
    { char v[24] = {0}; PlayStatsIOSVersion(v, sizeof v); if (v[0]) ps_clean(ps_version, v, sizeof ps_version); }
#endif
    ps_randid(ps_session, 24);
    ps_load_install(dataDir);
    ps_on = 1;
    char json[256];
    snprintf(json, sizeof json,
             "{\"game\":\"%s\",\"version\":\"%s\",\"platform\":\"" PS_PLATFORM "\",\"install\":\"%s\",\"session\":\"%s\"}",
             ps_game, ps_version, ps_install, ps_session);
    ps_post("start", json);
#if defined(__EMSCRIPTEN__)
    // A closed tab never reaches PlayStatsShutdown: send the last beat from pagehide.
    ps_js_arm_pagehide(PS_URL, ps_game, ps_session);
#endif
}

void PlayStatsUpdate(float dt) {
    if (!ps_on) return;
    if (dt > 1.0f) dt = 1.0f;          // a long stall or time in the background is not play
    if (dt < 0.0f) dt = 0.0f;
    ps_played += dt;
    ps_sinceBeat += dt;
#if defined(__EMSCRIPTEN__)
    { static float js = 0; js += dt; if (js >= 5.0f) { js = 0; ps_js_seconds((int)ps_played); } }
#endif
    if (ps_sinceBeat >= PS_BEAT_SECONDS) { ps_sinceBeat = 0.0f; ps_beat(0); }
}

void PlayStatsShutdown(void) {
    if (!ps_on) return;
    ps_beat(1);
    ps_on = 0;
}
