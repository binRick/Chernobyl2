// net_mac.m -- the game's one network connection on the Mac: a WebSocket to the multiplayer relay
// (server/mp-relay.js), through the system's own URLSession (so wss:// needs no TLS library of ours).
// The browser build does the same with the page's WebSocket (src/net.h). Messages arrive on the
// session's queue and wait in a list; the game takes them from its own thread, once a frame.
#import <Foundation/Foundation.h>

static NSURLSession *gSession; static NSURLSessionWebSocketTask *gTask;
static NSMutableArray<NSData *> *gIn; static NSLock *gLock;
static volatile int gState = 0;                  // 0 closed, 1 connecting, 2 open
static int gGen = 0;                             // which connection a late callback belongs to

// (Built without ARC, as macicon.m is: the retains and releases are written out, and each entry point has its own
// autorelease pool -- the game's thread has none.)
static void Listen(NSURLSessionWebSocketTask *task, int gen) {
    [task receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage *m, NSError *err) {
        if (gen != gGen) return;
        if (err || !m) { gState = 0; return; }
        gState = 2;
        if (m.type == NSURLSessionWebSocketMessageTypeData && m.data) { [gLock lock]; [gIn addObject:m.data]; [gLock unlock]; }
        Listen(task, gen);
    }];
}

int NetOpen(const char *url) {
    @autoreleasepool {
        if (!gLock) { gLock = [NSLock new]; gIn = [NSMutableArray new];
                      gSession = [[NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration ephemeralSessionConfiguration]] retain]; }
        NSURL *u = [NSURL URLWithString:[NSString stringWithUTF8String:url]];
        if (!u) return 0;
        if (gTask) { [gTask cancel]; [gTask release]; gTask = nil; }
        [gLock lock]; [gIn removeAllObjects]; [gLock unlock];
        int gen = ++gGen;
        gTask = [[gSession webSocketTaskWithURL:u] retain];
        gState = 1;
        [gTask resume];
        NSURLSessionWebSocketTask *task = gTask;
        [task sendPingWithPongReceiveHandler:^(NSError *err) { if (gen == gGen) gState = err ? 0 : 2; }];   // open once the relay answers
        Listen(task, gen);
    }
    return 1;
}
int NetState(void) { return gState; }
void NetSend(const void *p, int n) {
    if (gState != 2 || !gTask || n <= 0) return;
    @autoreleasepool {
        int gen = gGen;
        NSURLSessionWebSocketMessage *m = [[NSURLSessionWebSocketMessage alloc] initWithData:[NSData dataWithBytes:p length:(NSUInteger)n]];
        [gTask sendMessage:m completionHandler:^(NSError *err) { if (err && gen == gGen) gState = 0; }];
        [m release];
    }
}
int NetRecv(unsigned char *buf, int max) {       // the next waiting message's length (copied into buf), or 0
    if (!gLock) return 0;
    NSData *d = nil;
    [gLock lock]; if (gIn.count) { d = [[gIn objectAtIndex:0] retain]; [gIn removeObjectAtIndex:0]; } [gLock unlock];
    if (!d) return 0;
    int n = (int)d.length;
    if (n > max) n = -1;                         // (too big for the game's buffer: dropped)
    else memcpy(buf, d.bytes, (size_t)n);
    [d release];
    return n;
}
void NetClose(void) {
    gGen++; gState = 0;
    @autoreleasepool {
        if (gTask) { [gTask cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure reason:nil]; [gTask release]; gTask = nil; }
        if (gLock) { [gLock lock]; [gIn removeAllObjects]; [gLock unlock]; }
    }
}
