// record.h -- dev: --record <out.mp4> films the game from inside (demo videos).
// Picture: each frame's back buffer, read just before EndDrawing, piped as raw RGBA to an ffmpeg
// child that stamps frames by wall clock and writes constant 60 fps 1920x1080 H.264.
// Sound: raylib's final mix (AttachAudioMixedProcessor) appended to <out>.wav; on stop the two
// are muxed into <out>. Off unless --record is given; any failure logs one "record:" line and
// the game carries on.
#ifndef C2_RECORD_H
#define C2_RECORD_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdatomic.h>
#include <time.h>
#include <math.h>
#include "raylib.h"
#include "rlgl.h"

static FILE  *g_recPipe = NULL;          // ffmpeg's stdin
static FILE  *g_recWav  = NULL;
static atomic_int g_recAudioOn = 0;
static atomic_llong g_recSamples = 0;    // stereo sample frames written
static double g_recT0 = 0, g_recTLast = 0;   // wall clock at the first and the latest filmed frame
static long long g_recSampLast = 0;         // sound sample frames taken by the latest filmed frame
static int    g_recW = 0, g_recH = 0;
static char   g_recOut[1024], g_recTmp[1100], g_recWavPath[1100];

static double RecNow(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9; }

static void RecAudio(void *buf, unsigned int frames){      // audio thread: float stereo
    if (!atomic_load(&g_recAudioOn) || !g_recWav) return;
    fwrite(buf, sizeof(float)*2, frames, g_recWav);
    atomic_fetch_add(&g_recSamples, frames);
}

static void RecWavHeader(FILE *f, unsigned rate, unsigned bytes){
    unsigned short fmt=3, ch=2, bits=32, align=8; unsigned brate=rate*8, riff=36+bytes, sub=16;
    fseek(f,0,SEEK_SET);
    fwrite("RIFF",1,4,f); fwrite(&riff,4,1,f); fwrite("WAVEfmt ",1,8,f); fwrite(&sub,4,1,f);
    fwrite(&fmt,2,1,f); fwrite(&ch,2,1,f); fwrite(&rate,4,1,f); fwrite(&brate,4,1,f);
    fwrite(&align,2,1,f); fwrite(&bits,2,1,f); fwrite("data",1,4,f); fwrite(&bytes,4,1,f);
}

static void RecordStop(void);

// After InitWindow (and InitAudioDevice if there is sound). Returns 1 when recording.
static int RecordStart(const char *mp4){
    if (system("command -v ffmpeg >/dev/null 2>&1")!=0){ fprintf(stderr,"record: ffmpeg not found on PATH; not recording\n"); return 0; }
    signal(SIGPIPE, SIG_IGN);
    snprintf(g_recOut,sizeof g_recOut,"%s",mp4);
    snprintf(g_recTmp,sizeof g_recTmp,"%s.video.mp4",mp4);
    snprintf(g_recWavPath,sizeof g_recWavPath,"%.1000s",mp4);
    { char *dot=strrchr(g_recWavPath,'.'); if (dot) strcpy(dot,".wav"); else strcat(g_recWavPath,".wav"); }
    g_recW=GetRenderWidth(); g_recH=GetRenderHeight();
    char cmd[4096];
    snprintf(cmd,sizeof cmd,"ffmpeg -v error -y -f rawvideo -pix_fmt rgba -s %dx%d -use_wallclock_as_timestamps 1 -i - "
             "-vf scale=1920:1080:flags=lanczos,format=yuv420p -r 60 -fps_mode cfr -c:v h264_videotoolbox -b:v 24M '%s'",
             g_recW,g_recH,g_recTmp);
    g_recPipe=popen(cmd,"w");
    if (!g_recPipe){ fprintf(stderr,"record: could not start ffmpeg\n"); return 0; }
    if (IsAudioDeviceReady()){                                   // the sound is taken from the first filmed frame on (RecordFrame),
        g_recWav=fopen(g_recWavPath,"wb");                       // not from here: loading comes between, and sound from then would
        if (g_recWav){ RecWavHeader(g_recWav,48000,0); AttachAudioMixedProcessor(RecAudio); }   // put every take's sound early
    }
    atexit(RecordStop);
    fprintf(stderr,"record: %dx%d -> %s\n",g_recW,g_recH,g_recOut);
    return 1;
}

// Just before EndDrawing: flush the batch so the HUD is in, then send the frame.
static void RecordFrame(void){
    if (!g_recPipe) return;
    rlDrawRenderBatchActive();
    unsigned char *px=rlReadScreenPixels(g_recW,g_recH);      // already top-down, alpha 255
    size_t n=(size_t)g_recW*g_recH*4;
    if (fwrite(px,1,n,g_recPipe)!=n){ fprintf(stderr,"record: ffmpeg stopped taking frames; recording off\n"); pclose(g_recPipe); g_recPipe=NULL; }
    else { if (g_recT0==0){ g_recT0=RecNow(); atomic_store(&g_recAudioOn,1); }   // first frame in: picture and sound start together
           g_recTLast=RecNow(); g_recSampLast=atomic_load(&g_recSamples); }      // ...and end together (RecordStop cuts the sound here)
    RL_FREE(px);
}

static void RecordStop(void){
    if (!g_recPipe && !g_recWav) return;
        if (g_recPipe){ pclose(g_recPipe); g_recPipe=NULL; }
    int haveWav=0;
    if (g_recWav){
        atomic_store(&g_recAudioOn,0);
        struct timespec z={0,50*1000000}; nanosleep(&z,NULL);          // let a callback in flight finish
        long long s=g_recSampLast; double span=g_recTLast-g_recT0;     // the sound up to the last filmed frame (not shutdown's)
        double r=span>0.5?s/span:48000; unsigned rate=(fabs(r-44100)<fabs(r-48000))?44100:48000;   // the device's rate, from what it sent
        s+=(long long)(rate/60);                                        // (and that last frame's own 1/60 s)
        if (s>atomic_load(&g_recSamples)) s=atomic_load(&g_recSamples);
        RecWavHeader(g_recWav,rate,(unsigned)(s*8)); fclose(g_recWav); g_recWav=NULL; haveWav=s>0;
    }
    char cmd[4096];
    if (haveWav) snprintf(cmd,sizeof cmd,"ffmpeg -nostdin -v error -y -i '%s' -i '%s' -map 0:v -map 1:a -c:v copy -af apad -c:a aac -b:a 192k -shortest -movflags +faststart '%s' && rm -f '%s'",
                          g_recTmp,g_recWavPath,g_recOut,g_recTmp);
    else         snprintf(cmd,sizeof cmd,"ffmpeg -nostdin -v error -y -i '%s' -c copy -movflags +faststart '%s' && rm -f '%s'",g_recTmp,g_recOut,g_recTmp);
    if (system(cmd)!=0) fprintf(stderr,"record: mux failed; raw video left at %s\n",g_recTmp);
}
#endif
