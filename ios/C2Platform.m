// C2Platform.m -- what the game asks the phone for (declared in src/main.c under C2_IOS):
// where its bundle and its Documents folder are, and an HTTP request for the leaderboard.
#import <Foundation/Foundation.h>

const char *C2BundleDir(void) {
    static char p[1024];
    if (!p[0]) strlcpy(p, [[[NSBundle mainBundle] resourcePath] fileSystemRepresentation], sizeof p);
    return p;
}

const char *C2DocsDir(void) {
    static char p[1024];
    if (!p[0]) {
        NSString *d = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
        strlcpy(p, d ? d.fileSystemRepresentation : ".", sizeof p);
    }
    return p;
}

// GET url, or POST jsonBody to it. Blocks until done (8 s at most): the game calls it from a
// worker thread, never from the frame. Writes the reply into out; returns 1 if there was one.
int C2HttpRequest(const char *url, const char *jsonBody, char *out, int outSz) {
    if (!url || !out || outSz < 2) return 0;
    out[0] = 0;
    @autoreleasepool {
        NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:@(url)]
                                                            cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:8.0];
        if (jsonBody) {
            req.HTTPMethod = @"POST";
            [req setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
            req.HTTPBody = [NSData dataWithBytes:jsonBody length:strlen(jsonBody)];
        }
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        __block NSData *got = nil;
        [[[NSURLSession sharedSession] dataTaskWithRequest:req completionHandler:^(NSData *d, NSURLResponse *r, NSError *e) {
            if (!e) got = d;
            dispatch_semaphore_signal(done);
        }] resume];
        dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(9 * NSEC_PER_SEC)));
        if (!got.length) return 0;
        NSUInteger n = MIN(got.length, (NSUInteger)(outSz - 1));
        memcpy(out, got.bytes, n); out[n] = 0;
        return 1;
    }
}
