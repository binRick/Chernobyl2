// net.h -- the game's one network connection: a WebSocket to the multiplayer relay
// (server/mp-relay.js). Binary messages only, taken one at a time from the game's own thread.
//
//   NetOpen(url)        start connecting (ws:// or wss://); 0 if it cannot even try
//   NetState()          0 closed, 1 connecting, 2 open
//   NetSend(p,n)        one message (dropped unless open)
//   NetRecv(buf,max)    the next waiting message's length, copied into buf; 0 when there is none
//   NetClose()
//
// The browser build uses the page's WebSocket; the Mac build the system's URLSession
// (src/net_mac.m). Anywhere else there is no multiplayer: the connection never opens.
#ifndef C2_NET_H
#define C2_NET_H

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
EM_JS(int, NetOpen, (const char *url), {
    try {
        if (Module.c2ws) { try { Module.c2ws.onmessage = null; Module.c2ws.close(); } catch (e) {} }
        Module.c2q = [];
        var ws = new WebSocket(UTF8ToString(url));
        ws.binaryType = 'arraybuffer';
        ws.onmessage = function (ev) { if (Module.c2ws === ws && ev.data instanceof ArrayBuffer) Module.c2q.push(new Uint8Array(ev.data)); };
        Module.c2ws = ws;
        return 1;
    } catch (e) { return 0; }
});
EM_JS(int, NetState, (void), {
    var ws = Module.c2ws; if (!ws) return 0;
    return ws.readyState === 0 ? 1 : (ws.readyState === 1 ? 2 : 0);
});
EM_JS(void, NetSend, (const void *p, int n), {
    var ws = Module.c2ws; if (!ws || ws.readyState !== 1) return;
    try { ws.send(HEAPU8.slice(p, p + n)); } catch (e) {}
});
EM_JS(int, NetRecv, (unsigned char *buf, int max), {
    var q = Module.c2q; if (!q || !q.length) return 0;
    var m = q.shift(); if (m.length > max) return -1;
    HEAPU8.set(m, buf); return m.length;
});
EM_JS(void, NetClose, (void), {
    if (Module.c2ws) { try { Module.c2ws.onmessage = null; Module.c2ws.close(); } catch (e) {} }
    Module.c2ws = null; Module.c2q = [];
});
#define C2_HAS_NET 1
#elif defined(C2_MAC)
int NetOpen(const char *url); int NetState(void); void NetSend(const void *p, int n);
int NetRecv(unsigned char *buf, int max); void NetClose(void);
#define C2_HAS_NET 1
#else
static int  NetOpen(const char *url){ (void)url; return 0; }
static int  NetState(void){ return 0; }
static void NetSend(const void *p, int n){ (void)p; (void)n; }
static int  NetRecv(unsigned char *buf, int max){ (void)buf; (void)max; return 0; }
static void NetClose(void){}
#define C2_HAS_NET 0
#endif
#endif
