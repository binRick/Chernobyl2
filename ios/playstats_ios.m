// playstats_ios.m: iOS transport for playstats.c (fire-and-forget NSURLSession POST).
#import <Foundation/Foundation.h>

void PlayStatsPostIOS(const char *url, const char *json)
{
    if (!url || !json) return;
    NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:[NSString stringWithUTF8String:url]]];
    req.HTTPMethod = @"POST";
    [req setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
    req.HTTPBody = [[NSString stringWithUTF8String:json] dataUsingEncoding:NSUTF8StringEncoding];
    req.timeoutInterval = 8.0;
    [[[NSURLSession sharedSession] dataTaskWithRequest:req
        completionHandler:^(NSData *d, NSURLResponse *r, NSError *e) { (void)d; (void)r; (void)e; }] resume];
}

void PlayStatsIOSInstallId(const char *fresh, char *out, int cap)
{
    NSUserDefaults *u = NSUserDefaults.standardUserDefaults;
    NSString *v = [u stringForKey:@"ximg.playstats.install"];
    if (v.length < 16) { v = [NSString stringWithUTF8String:fresh]; [u setObject:v forKey:@"ximg.playstats.install"]; }
    snprintf(out, (size_t)cap, "%s", v.UTF8String);
}

void PlayStatsIOSVersion(char *out, int cap)
{
    NSString *v = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    snprintf(out, (size_t)cap, "%s", v ? v.UTF8String : "");
}
