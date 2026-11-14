// Sound board (dev): `--sounds` / ./dev-sounds.sh. Every sound the game can make, grouped by what
// it is for, each row showing its trigger, where it comes from (file, a cut of a file, or
// synthesized in code) and its length -- click a row to hear it. Rows marked MISSING are cues
// the game has no sound for yet (or is borrowing another sound for): the shopping list.
//   click a row: play (again: restart)   SPACE: stop everything   wheel / up / down: scroll
//   TAB: next group   M: only the MISSING and BORROWED rows   ESC: quit
typedef struct { const char *group, *name, *when, *src; Sound *snd; int *has; Music *mus; int status; int slot, cand; } SbRow;   // status: 0 ok, 1 borrowed (stand-in), 2 missing, 3 a CANDIDATE (not in the game)
// slot: a trap sound's index+1 (it can take a recording); cand: a candidate's index+1.
// CANDIDATES: any .mp3 / .wav / .ogg dropped in assets/candidates/ is listed here and nowhere else -- the game does not load
// it. Click to hear it. With the pointer on it, a number key APPLIES it to that trap sound (the file is copied to
// assets/traps/ and assets/traps/sounds.txt records it); BACKSPACE on a trap sound puts the synthesized one back.
#define SB_MAXCAND 160
static Sound g_candSnd[SB_MAXCAND]; static char g_candName[SB_MAXCAND][96], g_candPath[SB_MAXCAND][160], g_candWhen[SB_MAXCAND][64]; static int g_candN=0, g_candScanned=0;
static char g_slotSrc[16][112];

#define SB_MAX 420
static SbRow g_sb[SB_MAX]; static int g_sbN=0;
static int g_sbOne=1;                                  // "has" for sounds with no flag of their own
static void SbAdd(const char *group, const char *name, const char *when, const char *src, Sound *snd, int *has, int status){
    if (g_sbN<SB_MAX) g_sb[g_sbN++]=(SbRow){group,name,when,src,snd,has,NULL,status};
}
// ON / OFF. Any sound on the board can be switched off for the game: E (or right-click) on its row. An "off" sound is
// swapped for a moment of silence wherever the game holds it, so everything that would play it plays nothing; the names
// are kept in sounds_off.txt and applied at every launch (SoundsApplyOff, after loading). Rows that share one recording
// go off together. (Looping music rows -- the ambience, the wave tracks -- are not switchable here.)
static Sound g_sbSilence; static int g_sbSilenceOK=0; static Sound g_sbOrig[SB_MAX]; static unsigned char g_sbOff[SB_MAX];
static char g_offNames[SB_MAX][72]; static int g_nOffNames=0;
static void SbOffLoad(void){ g_nOffNames=0; FILE *f=fopen("sounds_off.txt","r"); if (!f) return; char ln[96];
    while (fgets(ln,sizeof ln,f) && g_nOffNames<SB_MAX){ size_t n=strlen(ln); while (n>0 && (ln[n-1]=='\n'||ln[n-1]=='\r')) ln[--n]=0; if (n>0) snprintf(g_offNames[g_nOffNames++],72,"%s",ln); }
    fclose(f); }
static void SbOffSave(void){ FILE *f=fopen("sounds_off.txt","w"); if (!f) return; for (int i=0;i<g_sbN;i++) if (g_sbOff[i]) fprintf(f,"%s\n",g_sb[i].name); fclose(f); }
static void SbSetOff(int i, int off){                                              // (and every other row holding the same recording)
    if (i<0 || i>=g_sbN || !g_sb[i].snd || g_sb[i].mus || g_sb[i].status>=2) return;
    if (!g_sbSilenceOK){ short z[64]={0}; Wave w={ .frameCount=64, .sampleRate=22050, .sampleSize=16, .channels=1, .data=z }; g_sbSilence=LoadSoundFromWave(w); g_sbSilenceOK=1; }
    Sound *h=g_sb[i].snd;
    if (off && !g_sbOff[i]){ StopSound(*h); g_sbOrig[i]=*h; *h=g_sbSilence; }
    else if (!off && g_sbOff[i]) *h=g_sbOrig[i];
    for (int q=0;q<g_sbN;q++) if (g_sb[q].snd==h){ if (q!=i) g_sbOrig[q]=g_sbOrig[i]; g_sbOff[q]=(unsigned char)off; }
}
// APPLYING a candidate puts it in the game. Each candidate file is named <what-it-is-for>__<title>.mp3; that first part says
// where it goes: a trap sound (arm, steam, ...) replaces the synthesized one at once; voice_<enemy>_<cue> becomes that
// enemy's own cry (assets/voice/, from the next launch); mutant-bolt and priest-raises-the-dead replace their stand-ins;
// anything else is filed under assets/extra/ -- chosen, but the game has no code that plays that event yet.
static char g_sbMsg[160]; static float g_sbMsgT=0;
static const char *SbCandKey(int c){ static char k[64]; const char *n=g_candName[c], *u=strstr(n,"__"); int l=u?(int)(u-n):0; if (l<=0||l>63){ k[0]=0; return k; } memcpy(k,n,l); k[l]=0; return k; }
static void SbCandSave(void){ FILE *f=fopen("assets/candidates/applied.txt","w"); if (!f) return; for (int c=0;c<g_candN;c++) if (g_candWhen[c][0]) fprintf(f,"%s %s\n",g_candWhen[c],g_candName[c]); fclose(f); }
static int SbCopy(const char *src, const char *dst){ int n=0; unsigned char *d=LoadFileData(src,&n); int ok=(d && n>0 && SaveFileData(dst,d,n)); if (d) UnloadFileData(d); return ok; }
static void SbApplyCandidate(int c, const char *keyOver){
    char key[64]; snprintf(key,sizeof key,"%s",(keyOver&&keyOver[0])?keyOver:SbCandKey(c)); if (!key[0]){ snprintf(g_sbMsg,sizeof g_sbMsg,"this file's name does not say what it is for: use a number key (trap sounds)"); g_sbMsgT=5; return; }
    char dst[128]; int ok=0, slot=-1; for (int k=0;k<TS_N;k++) if (!strcmp(key,kTrapSndKey[k])) slot=k;
    if (slot>=0){ snprintf(dst,sizeof dst,"assets/traps/sound_%s%s",key,GetFileExtension(g_candPath[c])); ok=SbCopy(g_candPath[c],dst) && TrapSoundSet(slot,dst); if (ok) TrapSoundsSave();
        snprintf(g_sbMsg,sizeof g_sbMsg,ok?"APPLIED: trap sound '%s' is now %s":"could not apply %s",ok?kTrapSndName[slot]:key,g_candName[c]); }
    else if (!strncmp(key,"voice_",6)){ snprintf(dst,sizeof dst,"assets/voice/%s.mp3",key+6); ok=SbCopy(g_candPath[c],dst);
        snprintf(g_sbMsg,sizeof g_sbMsg,ok?"APPLIED: %s  ->  %s  (the game uses it from the next launch)":"could not write %s",ok?g_candName[c]:dst,dst); }
    else if (!strcmp(key,"mutant-bolt") || !strcmp(key,"priest-raises-the-dead")){ int bolt=(key[0]=='m'); snprintf(dst,sizeof dst,bolt?"assets/mutant_bolt.mp3":"assets/priest_raise.mp3"); ok=SbCopy(g_candPath[c],dst);
        if (ok){ Sound sd=LoadSound(dst); if (sd.frameCount>0){ if (bolt){ g_eBoltSnd=sd; g_hasEBolt=1; } else { g_eRaiseSnd=sd; g_hasERaise=1; } } }
        snprintf(g_sbMsg,sizeof g_sbMsg,ok?"APPLIED: %s  ->  %s":"could not write %s",ok?g_candName[c]:dst,dst); }
    else { MakeDirectory("assets/extra"); snprintf(dst,sizeof dst,"assets/extra/%s.mp3",key); ok=SbCopy(g_candPath[c],dst); int ex=-1; for (int k=0;k<EX_N;k++) if (!strcmp(key,kExtraKey[k])) ex=k;
        if (ok && ex>=0) ExtraLoad(ex);
        snprintf(g_sbMsg,sizeof g_sbMsg,!ok?"could not write %s":(ex>=0)?"APPLIED: %s  (in the game now)":"CHOSEN: filed as %s -- the game has no code that plays this event yet",dst); }
    g_sbMsgT=7.0f; if (!ok) return;
    for (int q=0;q<g_candN;q++) if (!strcmp(g_candWhen[q],key)) g_candWhen[q][0]=0;
    snprintf(g_candWhen[c],64,"%s",key); SbCandSave(); DebugLog("sbapply","\"key\":\"%s\",\"file\":\"%s\"",key,g_candName[c]);
}
// What a row's candidates are filed under (the first part of their file names): a trap sound's key, voice_<enemy>_<cue>
// for an enemy's cry, else the row's own name in lower case with dashes.
static const char *SbRowKey(const SbRow *r){ static char k[80]; k[0]=0;
    if (r->cand>0) return k;
    if (r->slot>0){ snprintf(k,sizeof k,"%s",kTrapSndKey[r->slot-1]); return k; }
    const char *v=r->src?strstr(r->src,"assets/voice/"):NULL;
    if (v){ v+=13; int n=0; char b[48]; while (v[n] && v[n]!='.' && n<47){ b[n]=v[n]; n++; } b[n]=0; snprintf(k,sizeof k,"voice_%s",b); return k; }
    int n=0, dash=1; for (const char *c=r->name; *c && n<78; c++){ char ch=(char)tolower((unsigned char)*c);
        if ((ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')){ k[n++]=ch; dash=0; } else if (!dash){ k[n++]='-'; dash=1; } }
    while (n>0 && k[n-1]=='-') n--; k[n]=0; return k; }
// UN-APPLYING takes it back out: the copy the apply made is removed and the sound goes back to what it was -- the
// synthesized trap sound, the stand-in, or nothing (an enemy's own cry: from the next launch).
static void SbUnapplyCandidate(int c){
    char key[64]; snprintf(key,sizeof key,"%s",g_candWhen[c]); if (!key[0]) return;
    int slot=-1; for (int k=0;k<TS_N;k++) if (!strcmp(key,kTrapSndKey[k])) slot=k;
    if (slot>=0){ char old[96]; snprintf(old,sizeof old,"%s",g_trapSndFile[slot]); TrapSoundSet(slot,NULL); TrapSoundsSave(); if (old[0]) remove(old);
        snprintf(g_sbMsg,sizeof g_sbMsg,"UN-APPLIED: trap sound '%s' is the synthesized one again",kTrapSndName[slot]); }
    else if (!strncmp(key,"voice_",6)){ remove(TextFormat("assets/voice/%s.mp3",key+6)); snprintf(g_sbMsg,sizeof g_sbMsg,"UN-APPLIED: assets/voice/%s.mp3 removed (the game goes back from the next launch)",key+6); }
    else if (!strcmp(key,"mutant-bolt")){ remove("assets/mutant_bolt.mp3"); if (g_hasSy){ g_eBoltSnd=LoadSoundAlias(g_sy[SY_BOLT]); g_hasEBolt=1; } snprintf(g_sbMsg,sizeof g_sbMsg,"UN-APPLIED: the mutant's bolt is the synthesized one again"); }
    else if (!strcmp(key,"priest-raises-the-dead")){ remove("assets/priest_raise.mp3"); if (FileExists("assets/wave_start.wav")){ g_eRaiseSnd=LoadSound("assets/wave_start.wav"); g_hasERaise=(g_eRaiseSnd.frameCount>0); } snprintf(g_sbMsg,sizeof g_sbMsg,"UN-APPLIED: the priest's raising is the stand-in again"); }
    else { remove(TextFormat("assets/extra/%s.mp3",key)); for (int k=0;k<EX_N;k++) if (!strcmp(key,kExtraKey[k])){ if (g_hasExtra[k]) StopSound(g_extraSnd[k]); g_hasExtra[k]=0; } snprintf(g_sbMsg,sizeof g_sbMsg,"UN-APPLIED: assets/extra/%s.mp3 removed",key); }
    g_candWhen[c][0]=0; SbCandSave(); g_sbMsgT=6.0f; DebugLog("sbunapply","\"key\":\"%s\",\"file\":\"%s\"",key,g_candName[c]);
}
static void SbBuild(void);
static void SoundsApplyOff(void){                                                  // the game, at launch: what was switched off stays off
    SbOffLoad(); if (g_nOffNames<=0) return;
    SbBuild(); memset(g_sbOff,0,sizeof g_sbOff); int n=0;
    for (int i=0;i<g_sbN;i++) for (int q=0;q<g_nOffNames;q++) if (!g_sbOff[i] && !strcmp(g_sb[i].name,g_offNames[q])){ SbSetOff(i,1); n++; }
    DebugLog("soundsoff","\"n\":%d",n);
}
static void SbBuild(void){
    g_sbN=0;
    // ---- WEAPONS
    SbAdd("WEAPONS","Minigun fire (loop)","trigger held, spun up","assets/minigun_fire.mp3",&g_weapons[2].fireSnd,&g_weapons[2].hasSnd,0);
    SbAdd("WEAPONS","Minigun spin-down","trigger released","assets/minigun_cooldown.mp3",&g_weapons[2].auxSnd,&g_weapons[2].hasAuxSnd,0);
    SbAdd("WEAPONS","Minigun spin-up","trigger pressed, barrels winding up","assets/minigun_spinup.wav (cut)",&g_spinUpSnd,&g_hasSpinUpSnd,0);
    SbAdd("WEAPONS","Flamethrower roar (loop)","trigger held","assets/flame_fire.mp3",&g_weapons[5].fireSnd,&g_weapons[5].hasSnd,0);
    SbAdd("WEAPONS","Flamethrower ignite / shut off","start and end of a burst",g_hasExtra[EX_FLAME]?"assets/extra/ (applied)":"-",&g_extraSnd[EX_FLAME],&g_hasExtra[EX_FLAME],g_hasExtra[EX_FLAME]?0:2);
    SbAdd("WEAPONS","Grenade launcher fire","each grenade","assets/grenade_launch.mp3",&g_weapons[0].fireSnd,&g_weapons[0].hasSnd,0);
    SbAdd("WEAPONS","Grenade explosion","grenade bursts","assets/explosion.wav",&g_grenSnd,&g_hasGrenSnd,0);
    SbAdd("WEAPONS","Grenade bounce","a grenade glances off a wall or the floor","assets/grenade_bounce.mp3",&g_grenBounceSnd,&g_hasGrenBounceSnd,0);
    SbAdd("WEAPONS","Grenade launcher: drum open","a reload starts","assets/gl_open.wav (cut)",&g_glSnd[0],&g_hasGlSnd[0],0);
    SbAdd("WEAPONS","Grenade launcher: grenade in","each grenade loaded","assets/gl_insert.wav (cut)",&g_glSnd[1],&g_hasGlSnd[1],0);
    SbAdd("WEAPONS","Grenade launcher: drum shut","the reload ends, or is cut short by firing","assets/gl_shut.wav (cut)",&g_glSnd[2],&g_hasGlSnd[2],0);
    SbAdd("WEAPONS","Remington blast","each shot","assets/shotgun_fire.mp3",&g_weapons[1].fireSnd,&g_weapons[1].hasSnd,0);
    SbAdd("WEAPONS","Remington rack","pump after a shot, and closing a reload","assets/shotgun_rack.wav (cut)",&g_weapons[1].auxSnd,&g_weapons[1].hasAuxSnd,0);
    SbAdd("WEAPONS","Remington shell in","each shell pushed into the tube","assets/shotgun_shell.wav (cut)",&g_shellSnd,&g_hasShellSnd,0);
    SbAdd("WEAPONS","Dry fire click","trigger on an empty gun","assets/dry_fire.mp3",&g_drySnd,&g_hasDrySnd,0);
    SbAdd("WEAPONS","Weapon switch / draw","keys 1-4",g_hasExtra[EX_SWITCH]?"assets/extra/ (applied)":"-",&g_extraSnd[EX_SWITCH],&g_hasExtra[EX_SWITCH],g_hasExtra[EX_SWITCH]?0:2);
    SbAdd("WEAPONS","Bullet hits wall","round strikes masonry","assets/bullet_wall.wav (cut)",&g_wallSnd,&g_hasWallSnd,0);
    SbAdd("WEAPONS","Bullet hits flesh","round strikes a body","assets/bullet_flesh.mp3",&g_fleshSnd,&g_hasFleshSnd,0);
    SbAdd("WEAPONS","Shell casing on the floor","casing's first bounce","synthesized",&g_sy[SY_CLINK],&g_hasSy,0);
    // ---- PLAYER
    SbAdd("PLAYER","Footstep","each step while walking or sprinting","synthesized",&g_sy[SY_STEP],&g_hasSy,0);
    SbAdd("PLAYER","Jump","leaving the floor","synthesized",&g_sy[SY_JUMP],&g_hasSy,0);
    SbAdd("PLAYER","Landing","coming down, louder for a longer fall","synthesized",&g_sy[SY_LAND],&g_hasSy,0);
    SbAdd("PLAYER","Hurt thud","any real hit","assets/player_hurt.wav",&g_hurtSnd,&g_hasHurtSnd,0);
    SbAdd("PLAYER","Pain yell","a lot of damage in a short while (1 of 2)","assets/pain_yell.mp3",&g_painSnd[0],&g_nPainSnd,0);
    SbAdd("PLAYER","Pain scream","a lot of damage in a short while (2 of 2)","assets/pain_scream.mp3",&g_painSnd[1],&g_nPainSnd,0);
    SbAdd("PLAYER","Surrounded","4 or more hostiles within 5.5 m, in front and behind","assets/surrounded.mp3",&g_surrSnd,&g_hasSurrSnd,0);
    SbAdd("PLAYER","Heartbeat","health under 30","assets/heartbeat.wav",&g_heartSnd,&g_hasHeartSnd,0);
    SbAdd("PLAYER","Death scream","the player dies","assets/player_death.mp3",&g_dieSnd,&g_hasDieSnd,0);
    SbAdd("PLAYER","Pickup","any pickup collected","assets/pickup.wav",&g_pkSnd,&g_hasPkSnd,0);
    SbAdd("PLAYER","Pickup: ammo","an ammo box collected","assets/pickup_ammo.mp3",&g_pkAmmoSnd,&g_hasPkAmmoSnd,0);
    SbAdd("PLAYER","Pickup: health / mega / quad / speed","a sound of its own per pickup kind",g_hasExtra[EX_PICKUP]?"assets/extra/ (applied)":"-",&g_extraSnd[EX_PICKUP],&g_hasExtra[EX_PICKUP],g_hasExtra[EX_PICKUP]?0:2);
    // ---- ENEMIES
    SbAdd("ENEMIES","Attack roar  (REMOVED from the game: it read as a dinosaur)","a melee enemy with no cry of his own lunges","assets/enemy_attack.mp3",&g_attackSnd,&g_hasAttackSnd,0);
    for (int k=0;k<g_nDeathSnd && k<8;k++){ static char nm[8][32], fl[8][40];
        snprintf(nm[k],sizeof nm[k],"Death cry %d",k+1); snprintf(fl[k],sizeof fl[k],"assets/enemy_death%d.mp3",k+1);
        SbAdd("ENEMIES",nm[k],"an enemy dies (one at random)",fl[k],&g_deathSnd[k],&g_sbOne,0); }
    SbAdd("ENEMIES","Death scream (shotgun)","an enemy killed by the shotgun, about half the time","assets/death_shotgun.mp3",&g_sgDeathSnd,&g_hasSgDeathSnd,0);
    SbAdd("ENEMIES","Rifleman / gunner shot","an enemy fires","assets/ak74_fire_single.mp3",&g_eShotSnd,&g_hasEShot,0);
    SbAdd("ENEMIES","Mutant bolt","a mutant throws a radiation bolt",FileExists("assets/mutant_bolt.mp3")?"assets/mutant_bolt.mp3":"synthesized",&g_eBoltSnd,&g_hasEBolt,0);
    { int own=FileExists("assets/priest_raise.mp3"); SbAdd("ENEMIES","Priest raises the dead","a corpse gets up",own?"assets/priest_raise.mp3":"assets/wave_start.wav (the wave sting)",&g_eRaiseSnd,&g_hasERaise,own?0:1); }
    SbAdd("ENEMIES","Melee weapon hits the player","cleaver, knife, shovel, axe, cross landing",g_hasExtra[EX_MELEE]?"assets/extra/ (applied)":"-",&g_extraSnd[EX_MELEE],&g_hasExtra[EX_MELEE],g_hasExtra[EX_MELEE]?0:2);
    SbAdd("ENEMIES","Running feet (loop)","a hostile running at the player within 15 m","assets/enemy_run.mp3",&g_runSnd,&g_hasRunSnd,0);
    SbAdd("ENEMIES","Walking footsteps","enemies walking nearby",g_hasExtra[EX_STEPS]?"assets/extra/ (applied)":"-",&g_extraSnd[EX_STEPS],&g_hasExtra[EX_STEPS],g_hasExtra[EX_STEPS]?0:2);
    SbAdd("ENEMIES","Rising from the floor","an enemy spawns",g_hasExtra[EX_RISE]?"assets/extra/ (applied)":"-",&g_extraSnd[EX_RISE],&g_hasExtra[EX_RISE],g_hasExtra[EX_RISE]?0:2);
    SbAdd("ENEMIES","Knocked down","an enemy is flattened by a blast or point-blank buckshot","assets/enemy_knockdown.mp3",&g_knockSnd,&g_hasKnockSnd,0);
    SbAdd("ENEMIES","Body hits the floor","an enemy falls dead",g_hasExtra[EX_BODY]?"assets/extra/ (applied)":"-",&g_extraSnd[EX_BODY],&g_hasExtra[EX_BODY],g_hasExtra[EX_BODY]?0:2);
    SbAdd("ENEMIES","Gib burst","an enemy is blown apart","assets/gib_burst.mp3",&g_gibSnd,&g_hasGibSnd,0);
    SbAdd("ENEMIES","Head chef (boss) dies","the boss goes down","assets/boss_dead.mp3",&g_bossDeadSnd,&g_hasBossDeadSnd,0);
    SbAdd("ENEMIES","Head chef (boss) entrance","the boss arrives between waves","assets/boss_entrance.mp3",&g_bossSnd,&g_hasBossSnd,0);
    // ---- ENEMY VOICES: one group per enemy, a row per cue. OK = his own file; BORROWED = his
    // family's file or the shared sound; MISSING = silent. The path shown is where his own file goes.
    { static char grp[EN_NTYPES][32], nm[EN_NTYPES][EC_N][24], src[EN_NTYPES][EC_N][96];
      static const char *when[EC_N]={"closes to 14 m of the player","starts a swing / lunge","takes a hit and lives","dies"};
      for (int t=0;t<EN_NTYPES;t++){
          snprintf(grp[t],sizeof grp[t],"VOICE: %s",kEnemy[t].name); for (char *c=grp[t];*c;c++) *c=(char)toupper((unsigned char)*c);
          for (int c=0;c<EC_N;c++){
              int o=EnemyVoiceOwner(t,c);
              snprintf(nm[t][c],sizeof nm[t][c],"%c%s",toupper((unsigned char)kECue[c][0]),kECue[c]+1);
              if (o==t){                                      // his own: one row per take
                  for (int v=0;v<g_hasECue[t][c] && v<EC_TAKES;v++){ static char tn[EN_NTYPES][EC_N][EC_TAKES][24], ts[EN_NTYPES][EC_N][EC_TAKES][72];
                      if (g_hasECue[t][c]>1) snprintf(tn[t][c][v],24,"%s  (take %d of %d)",nm[t][c],v+1,g_hasECue[t][c]); else snprintf(tn[t][c][v],24,"%s",nm[t][c]);
                      if (v==0) snprintf(ts[t][c][v],72,"%s",g_eCuePath[t][c]); else snprintf(ts[t][c][v],72,"assets/voice/%s_%s%d",kEnemy[t].name,kECue[c],v+1);
                      SbAdd(grp[t],tn[t][c][v],when[c],ts[t][c][v],&g_eCue[t][c][v],&g_hasECue[t][c],0); } }
              else if (o>=0){ snprintf(src[t][c],sizeof src[t][c],"the %s's   (own: %s)",kEnemy[o].name,g_eCuePath[t][c]); SbAdd(grp[t],nm[t][c],when[c],src[t][c],&g_eCue[o][c][0],&g_hasECue[o][c],1); }
              else if (c==EC_ATTACK && kEnemy[t].kind==AK_MELEE){ snprintf(src[t][c],sizeof src[t][c],"shared roar   (own: %s)",g_eCuePath[t][c]); SbAdd(grp[t],nm[t][c],when[c],src[t][c],&g_attackSnd,&g_hasAttackSnd,1); }
              else if (c==EC_DEATH && g_nDeathSnd>0){ snprintf(src[t][c],sizeof src[t][c],"shared death cries   (own: %s)",g_eCuePath[t][c]); SbAdd(grp[t],nm[t][c],when[c],src[t][c],&g_deathSnd[0],&g_sbOne,1); }
              else { snprintf(src[t][c],sizeof src[t][c],"%s",g_eCuePath[t][c]); SbAdd(grp[t],nm[t][c],when[c],src[t][c],NULL,NULL,2); }
          } } }
    // ---- EVENTS
    SbAdd("EVENTS","Hostiles in sight","none on screen, then some","assets/enemy_appear.mp3",&g_appearSnd,&g_hasAppearSnd,0);
    SbAdd("EVENTS","Charging pack","5 or more hostiles within 16 m","assets/angry_crowd.mp3",&g_mobSnd,&g_hasMobSnd,0);
    SbAdd("EVENTS","Multi-kill","one grenade kills 3 or more","assets/crowd_scream.mp3",&g_crowdSnd,&g_hasCrowdSnd,0);
    SbAdd("EVENTS","Wave start","a wave begins","assets/wave_start.wav",&g_waveSnd,&g_hasWaveSnd,0);
    SbAdd("EVENTS","Wave clear","the field is empty","assets/wave_clear.wav",&g_clearSnd,&g_hasClearSnd,0);
    // ---- FEEDBACK
    SbAdd("FEEDBACK","Hit tick","a round lands on an enemy (higher on a headshot)","synthesized",&g_sy[SY_HIT],&g_hasSy,0);
    SbAdd("FEEDBACK","Kill thud","an enemy dies","synthesized",&g_sy[SY_KILL],&g_hasSy,0);
    SbAdd("FEEDBACK","Low ammo warning","last rounds in the magazine",g_hasExtra[EX_LOWAMMO]?"assets/extra/ (applied)":"-",&g_extraSnd[EX_LOWAMMO],&g_hasExtra[EX_LOWAMMO],g_hasExtra[EX_LOWAMMO]?0:2);
    SbAdd("FEEDBACK","Menu rollover","the pointer moves onto a row of the ESC menu","assets/menu_hover.mp3",&g_menuSnd,&g_hasMenuSnd,0);
    SbAdd("FEEDBACK","Menu select","a row of the ESC menu is clicked","assets/menu_select.mp3",&g_menuSelSnd,&g_hasMenuSelSnd,0);
    SbAdd("FEEDBACK","Leaderboard rank","a score is posted",g_hasExtra[EX_RANK]?"assets/extra/ (applied)":"-",&g_extraSnd[EX_RANK],&g_hasExtra[EX_RANK],g_hasExtra[EX_RANK]?0:2);
    // ---- VOICED MOMENTS, BOSSES, PLACES (recordings added 13 Nov)
    SbAdd("MOMENTS","Wave 1 voice","wave 1 starts","assets/wave1_dialogue.mp3",&g_wave1Snd,&g_hasWave1Snd,0);
    SbAdd("MOMENTS","Wave 2 voice","wave 2 starts","assets/wave2_voice.mp3",&g_wave2Snd,&g_hasWave2Snd,0);
    SbAdd("MOMENTS","First sighting","an enemy appears after 8 s of none (sometimes)","assets/cue_sight.mp3",&g_cueSight,&g_hasCueSight,0);
    SbAdd("MOMENTS","Running from a pack","moving, 3 or more close and all behind (sometimes)","assets/cue_flee.mp3",&g_cueFlee,&g_hasCueFlee,0);
    SbAdd("MOMENTS","Quiet tension","on the move, nothing seen or fought for 8 s (sometimes)","assets/cue_tension.mp3",&g_cueTense,&g_hasCueTense,0);
    SbAdd("MOMENTS","Pack horn","5 or more running at the player inside 14 m (sometimes)","assets/mob_horn.mp3",&g_hornSnd,&g_hasHornSnd,0);
    SbAdd("MOMENTS","Low health heartbeat","under 30 hp","assets/heartbeat_low.mp3",&g_heartSnd,&g_hasHeartSnd,0);
    SbAdd("MOMENTS","Boss close drone","a boss within 4 m","assets/boss_close.mp3",&g_bossCloseSnd,&g_hasBossClose,0);
    for (int k=0;k<g_nBossTheme && k<4;k++){ static const char *nm[4]={"Boss 1 theme","Boss 2 theme","Boss 3 theme","Boss 4 theme"}; SbAdd("MOMENTS",nm[k],"that wave's boss has been seen, until he dies","assets/bossN_theme.mp3",&g_bossTheme[k],&g_sbOne,0); }
    for (int k=0;k<g_nRadSnd;k++) SbAdd("PLACES",TextFormat("Radiation %d",k+1)[0]?strdup(TextFormat("Radiation %d",k+1)):"Radiation","near a breach in the floor","assets/radiation_N.mp3",&g_radSnd[k],&g_sbOne,0);
    for (int k=0;k<g_nPanelSnd;k++) SbAdd("PLACES",strdup(TextFormat("Control panel %d",k+1)),"near a control station or a red fire point","assets/panel_N.mp3",&g_panelSnd[k],&g_sbOne,0);
    // ---- TRAPS (traps.h: synthesized)
    for (int k=0;k<TS_N;k++){ snprintf(g_slotSrc[k],sizeof g_slotSrc[k],"%s",g_trapSndFile[k][0]?TextFormat("APPLIED: %s",GetFileName(g_trapSndFile[k])):"synthesized");
        SbAdd("TRAPS  (a candidate can replace any of these: point at it, press the number)",TextFormat("%d   %s",(k+1)%10,kTrapSndName[k]),kTrapSndWhen[k],g_slotSrc[k],&g_trapSnd[k],&g_hasTrapSnd,0);
        if (g_sbN>0){ g_sb[g_sbN-1].slot=k+1; g_sb[g_sbN-1].name=strdup(g_sb[g_sbN-1].name); } }
    if (!g_candScanned){ g_candScanned=1; g_candN=0;                                   // what has been dropped in assets/candidates/
        FilePathList fl=LoadDirectoryFiles("assets/candidates");
        for (unsigned int i=0;i<fl.count && g_candN<SB_MAXCAND;i++){ const char *fp=fl.paths[i];
            if (!IsFileExtension(fp,".mp3") && !IsFileExtension(fp,".wav") && !IsFileExtension(fp,".ogg")) continue;
            Sound sd=LoadSound(fp); if (sd.frameCount<=0) continue;
            g_candSnd[g_candN]=sd; snprintf(g_candPath[g_candN],160,"%s",fp); snprintf(g_candName[g_candN],96,"%s",GetFileName(fp)); g_candWhen[g_candN][0]=0; g_candN++; }
        UnloadDirectoryFiles(fl);
        FILE *af=fopen("assets/candidates/applied.txt","r");                          // which of them are in the game already
        if (af){ char key[64], fn[96]; while (fscanf(af,"%63s %95s",key,fn)==2) for (int c=0;c<g_candN;c++) if (!strcmp(g_candName[c],fn)) snprintf(g_candWhen[c],64,"%s",key); fclose(af); } }
    for (int c=0;c<g_candN;c++){ int used=-1; for (int k=0;k<TS_N;k++) if (g_trapSndFile[k][0] && strstr(g_trapSndFile[k],kTrapSndKey[k]) && !strcmp(g_candWhen[c],kTrapSndKey[k])) used=k;
        SbAdd("CANDIDATES  (assets/candidates/ -- NOT in the game until applied)",g_candName[c],g_candWhen[c][0]?TextFormat("applied as: %s",g_candWhen[c]):TextFormat("ENTER: apply as %s",SbCandKey(c)),g_candPath[c],&g_candSnd[c],&g_sbOne,3);
        if (g_sbN>0){ g_sb[g_sbN-1].cand=c+1; g_sb[g_sbN-1].when=strdup(g_sb[g_sbN-1].when); } (void)used; }
    if (g_candN==0) SbAdd("CANDIDATES  (assets/candidates/ -- NOT in the game until applied)","(none yet)","drop .mp3 / .wav files into assets/candidates/ and reopen","-",NULL,NULL,2);
    // ---- AMBIENCE
    SbAdd("AMBIENCE","Drone, wind and Geiger counter (loop)","always, quieter in a fight","synthesized",NULL,&g_hasAmb,0); if (g_sbN>0) g_sb[g_sbN-1].mus=&g_amb[0];
    SbAdd("AMBIENCE","Tension pulse (loop)","swells with the hostiles on the field","synthesized",NULL,&g_hasAmb,0);          if (g_sbN>0) g_sb[g_sbN-1].mus=&g_amb[1];
    for (int k=0;k<3;k++){ static const char *nm[3]={"Wave 1 background (loop)","Wave 2 background (loop)","Wave 3 background (loop)"};
        static const char *wh[3]={"wave 1, then every third wave","wave 2, then every third wave","wave 3, then every third wave"};
        static const char *fl[3]={"assets/wave1_bg.mp3","assets/wave2_bg.mp3","assets/wave3_bg.mp3"};
        SbAdd("AMBIENCE",nm[k],wh[k],fl[k],NULL,&g_nWaveBg,0); if (g_sbN>0 && g_nWaveBg>k) g_sb[g_sbN-1].mus=&g_waveBg[k]; else if (g_sbN>0) g_sb[g_sbN-1].status=2; }
    SbAdd("AMBIENCE","Background for wave 4 onward",g_waveBg4?"wave 4 and every wave after":"later waves reuse the first three tracks",g_waveBg4?"assets/extra/ (applied)":"-",NULL,g_waveBg4?&g_nWaveBg:NULL,g_waveBg4?0:2); if (g_waveBg4 && g_sbN>0) g_sb[g_sbN-1].mus=&g_waveBg[3];
}
static int SbLoaded(const SbRow *r){ return r->status!=2 && r->has && *r->has>0 && (r->mus || (r->snd && r->snd->frameCount>0)); }
static void SbStopAll(void){
    for (int i=0;i<g_sbN;i++){ if (!SbLoaded(&g_sb[i])) continue;
        if (g_sb[i].mus) SetMusicVolume(*g_sb[i].mus,0.0f); else StopSound(*g_sb[i].snd); }
}
static int RunSoundboard(void){
    for (int i=0;i<g_sbN;i++) if (g_sbOff[i]) SbSetOff(i,0);                           // (rebuilt below: start from every sound as loaded)
    SbBuild(); memset(g_sbOff,0,sizeof g_sbOff); SbOffLoad(); for (int i=0;i<g_sbN;i++) for (int q=0;q<g_nOffNames;q++) if (!g_sbOff[i] && !strcmp(g_sb[i].name,g_offNames[q])) SbSetOff(i,1);
    EnableCursor();
    for (int i=0;i<2;i++) if (g_hasAmb) SetMusicVolume(g_amb[i],0.0f);
    int nOk=0,nBor=0,nMiss=0,nBad=0;
    for (int i=0;i<g_sbN;i++){ if (g_sb[i].status==2) nMiss++; else if (!SbLoaded(&g_sb[i])) nBad++; else if (g_sb[i].status==1) nBor++; else nOk++; }
    for (int i=0;i<g_sbN;i++) if (g_sb[i].status==1 || g_sb[i].status==2) DebugLog("sbgap","\"status\":%d,\"group\":\"%s\",\"name\":\"%s\",\"when\":\"%s\",\"src\":\"%s\",\"h\":%d",g_sb[i].status,g_sb[i].group,g_sb[i].name,g_sb[i].when,g_sb[i].src,g_sb[i].snd!=NULL);
    DebugLog("soundboard","\"rows\":%d,\"ok\":%d,\"borrowed\":%d,\"missing\":%d,\"failed\":%d", g_sbN,nOk,nBor,nMiss,nBad);
    float scroll=0; int onlyGaps=0, playing=-1, frame=0, sel=-1; const int RH=30, GH=46;
    { const char *ss=getenv("CHERNOBYL_SB_SEL"); if (ss) for (int i=0;i<g_sbN && sel<0;i++) if (g_sb[i].cand<=0 && strstr(g_sb[i].name,ss)) sel=i; }   // dev: open on this sound (stills)
    int selJump=(sel>=0);      // sel: the row whose candidates are shown under it
    while (!WindowShouldClose()){
        if (IsKeyPressed(KEY_ESCAPE)) break;                 // ESC leaves the dev mode (the game turns raylib's own exit key off)
        if (g_maxFrames>0 && ++frame>g_maxFrames) break;
        if (g_hasAmb){ UpdateMusicStream(g_amb[0]); UpdateMusicStream(g_amb[1]); }
        for (int k=0;k<g_nWaveBg;k++) if (IsMusicStreamPlaying(g_waveBg[k])) UpdateMusicStream(g_waveBg[k]);
        int W=GetScreenWidth(), H=GetScreenHeight(); Vector2 mp=GetMousePosition();
        if (IsKeyPressed(KEY_M)){ onlyGaps=!onlyGaps; scroll=0; }
        if (IsKeyPressed(KEY_SPACE)){ SbStopAll(); playing=-1; }
        int doApply=-1, doRevert=0; for (int k=0;k<10;k++) if (IsKeyPressed(KEY_ZERO+k)) doApply=(k+9)%10;      // 1..9, 0 -> trap sound 0..9
        if (IsKeyPressed(KEY_BACKSPACE)) doRevert=1;
        scroll-=GetMouseWheelMove()*48.0f; if (IsKeyDown(KEY_DOWN)) scroll+=14; if (IsKeyDown(KEY_UP)) scroll-=14;
        // layout pass: y of every visible row; group headers in between
        static int rowY[SB_MAX]; int y=0; const char *grp=NULL; int groupY[32], nG=0;
        static unsigned char nCand[SB_MAX]; char selKey[80]=""; if (sel>=0 && sel<g_sbN) snprintf(selKey,sizeof selKey,"%s",SbRowKey(&g_sb[sel]));
        for (int i=0;i<g_sbN;i++) rowY[i]=-1;
        for (int i=0;i<g_sbN;i++){ if (g_sb[i].cand>0) continue;                         // candidates are not rows of their own: they open under the sound they are for
            char rk[80]; snprintf(rk,sizeof rk,"%s",SbRowKey(&g_sb[i])); nCand[i]=0; if (rk[0]) for (int c=0;c<g_candN;c++) if (!strcmp(SbCandKey(c),rk)) nCand[i]++;
            if (onlyGaps && g_sb[i].status==0 && SbLoaded(&g_sb[i]) && !nCand[i]) continue;
            if (!grp || strcmp(grp,g_sb[i].group)){ grp=g_sb[i].group; if (nG<32) groupY[nG++]=y; y+=GH; }
            rowY[i]=y; y+=RH;
            if (i==sel && selKey[0]) for (int q=0;q<g_sbN;q++) if (g_sb[q].cand>0 && !strcmp(SbCandKey(g_sb[q].cand-1),selKey)){ rowY[q]=y; y+=RH; } }
        if (selJump && sel>=0 && rowY[sel]>=0){ scroll=(float)rowY[sel]-120.0f; selJump=0; }
        float maxScroll=fmaxf(0.0f,(float)y-(H-150)); if (scroll<0) scroll=0; if (scroll>maxScroll) scroll=maxScroll;
        if (IsKeyPressed(KEY_TAB) && nG>0){ int k=0; while (k<nG && groupY[k]<=scroll+1) k++; scroll=(k<nG)?(float)groupY[k]:0.0f; if (scroll>maxScroll) scroll=maxScroll; }
        int top=104, hover=-1;
        for (int i=0;i<g_sbN;i++) if (rowY[i]>=0){ float ry=top+rowY[i]-scroll; if (mp.y>=ry && mp.y<ry+RH && mp.y>top && mp.x>20 && mp.x<W-20) hover=i; }
        if (hover>=0 && (IsKeyPressed(KEY_E) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) && g_sb[hover].snd && !g_sb[hover].mus && g_sb[hover].status<2){   // on / off for the game
            SbStopAll(); playing=-1; SbSetOff(hover,!g_sbOff[hover]); SbOffSave(); DebugLog("sboff","\"name\":\"%s\",\"off\":%d",g_sb[hover].name,g_sbOff[hover]); }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hover>=0 && g_sb[hover].cand>0 && mp.x>W-190){     // its APPLY button
            SbStopAll(); playing=-1; int c=g_sb[hover].cand-1; if (g_candWhen[c][0]) SbUnapplyCandidate(c); else SbApplyCandidate(c,sel>=0?SbRowKey(&g_sb[sel]):NULL); SbBuild(); }   // (the same button takes an applied one back out)
        else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hover>=0 && g_sb[hover].cand<=0){ sel=(sel==hover && !SbLoaded(&g_sb[hover]))?-1:hover; }   // a click on a sound opens its candidates under it
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hover>=0 && !(g_sb[hover].cand>0 && mp.x>W-190) && SbLoaded(&g_sb[hover])){
            SbStopAll(); SbRow *r=&g_sb[hover]; Sound prev=g_sbSilence; if (g_sbOff[hover]){ prev=g_sbOrig[hover]; SetSoundPan(prev,0.0f); SetSoundPitch(prev,1.0f); SetSoundVolume(prev,1.0f); PlaySound(prev); playing=-1; }   // (an OFF sound can still be heard here)
            if (g_sbOff[hover]){} else
            if (r->mus){ if (!IsMusicStreamPlaying(*r->mus)) PlayMusicStream(*r->mus); SetMusicVolume(*r->mus,0.8f); } else { SetSoundPan(*r->snd,0.0f); SetSoundPitch(*r->snd,1.0f); SetSoundVolume(*r->snd,1.0f); PlaySound(*r->snd); }
            playing=hover; }
        if (hover>=0 && g_sb[hover].cand>0 && ((doApply>=0 && doApply<TS_N) || IsKeyPressed(KEY_ENTER))){   // APPLY: ENTER = as what its name says it is for; a number = as that trap sound
            SbStopAll(); playing=-1; SbApplyCandidate(g_sb[hover].cand-1,(doApply>=0)?kTrapSndKey[doApply]:NULL); SbBuild(); }
        if (hover>=0 && g_sb[hover].cand>0 && doRevert && g_candWhen[g_sb[hover].cand-1][0]){ SbStopAll(); playing=-1; SbUnapplyCandidate(g_sb[hover].cand-1); SbBuild(); }   // BACKSPACE on an applied candidate: out again
        if (g_sbMsgT>0) g_sbMsgT-=GetFrameTime();
        if (hover>=0 && g_sb[hover].slot>0 && doRevert){ int k=g_sb[hover].slot-1; SbStopAll(); playing=-1; TrapSoundSet(k,NULL); TrapSoundsSave();
            for (int q=0;q<g_candN;q++) if (!strcmp(g_candWhen[q],kTrapSndKey[k])) g_candWhen[q][0]=0; SbBuild(); }
        if (playing>=0 && playing<g_sbN && !g_sb[playing].mus && g_sb[playing].snd && !IsSoundPlaying(*g_sb[playing].snd)) playing=-1;
        BeginDrawing();
        ClearBackground((Color){14,14,17,255});
        grp=NULL;
        for (int i=0;i<g_sbN;i++){ if (rowY[i]<0) continue; SbRow *r=&g_sb[i]; int ry=(int)(top+rowY[i]-scroll);
            if (r->cand>0){                                                               // a candidate, shown under the sound it is for
                if (ry<top-RH || ry>H) continue; int ap=g_candWhen[r->cand-1][0]!=0;
                DrawRectangle(60,ry,W-80,RH-2,(i==playing)?(Color){60,40,20,235}:(i==hover)?(Color){30,40,58,235}:(Color){20,26,38,235});
                DrawText(ap?"APPLIED":"candidate",76,ry+7,16,ap?(Color){110,200,120,255}:(Color){90,170,255,255});
                { const char *nm=r->name, *u=strstr(nm,"__"); DrawText(u?u+2:nm,190,ry+5,20,(Color){215,225,240,255}); }
                if (sel>=0 && sel<g_sbN){ const SbRow *o=&g_sb[sel]; const char *g=o->group; char gs[40]; int gl=0; while (g[gl] && !(g[gl]==' ' && g[gl+1]==' ') && gl<39){ gs[gl]=g[gl]; gl++; } gs[gl]=0;      // what it would be the sound OF
                    const char *ft=TextFormat("for:  %s  -  %s   (%s)",gs,o->name,o->when); DrawText(ft,(int)(W*0.40f),ry+7,16,(Color){255,210,120,255}); }
                if (r->snd && r->snd->frameCount>0){ float sec=(float)r->snd->frameCount/(float)(r->snd->stream.sampleRate>0?r->snd->stream.sampleRate:44100); const char *d=TextFormat("%.1fs",sec); DrawText(d,W-190-MeasureText(d,16),ry+7,16,(Color){150,160,175,255}); }
                { Rectangle b={(float)(W-176),(float)(ry+3),140,(float)(RH-8)}; int hb=(i==hover && mp.x>W-190);
                  DrawRectangleRec(b,ap?(hb?(Color){170,60,50,255}:(Color){30,70,40,255}):hb?(Color){60,130,220,255}:(Color){36,74,130,255}); const char *bt=ap?(hb?"UNAPPLY":"APPLIED"):"APPLY"; DrawText(bt,(int)(b.x+70-MeasureText(bt,16)/2),ry+7,16,WHITE); }
                continue; }
            if (!grp || strcmp(grp,r->group)){ grp=r->group; int gy=ry-GH+14;
                if (gy>top-GH && gy<H){ DrawText(grp,28,gy,24,(Color){255,90,70,255}); DrawRectangle(28,gy+28,W-56,1,(Color){70,40,36,255}); } }
            if (ry<top-RH || ry>H) continue;
            int ok=SbLoaded(r);
            if (i==playing) DrawRectangle(20,ry,W-40,RH-2,(Color){60,40,20,235}); else if (i==hover) DrawRectangle(20,ry,W-40,RH-2,(Color){34,32,36,235});
            const char *tag = g_sbOff[i] ? "OFF" : r->status==2 ? "MISSING" : !ok ? "NOT LOADED" : r->status==1 ? "BORROWED" : (i==playing ? "PLAYING" : r->status==3 ? ((r->cand>0 && g_candWhen[r->cand-1][0])?"APPLIED":"NOT APPLIED") : "OK");
            Color tc = g_sbOff[i] ? (Color){150,150,150,255} : r->status==2 ? (Color){230,80,70,255} : !ok ? (Color){230,80,70,255} : r->status==1 ? (Color){240,180,60,255} : (r->status==3 && i!=playing) ? ((r->cand>0 && g_candWhen[r->cand-1][0])?(Color){110,200,120,255}:(Color){90,170,255,255}) : (i==playing?(Color){255,220,90,255}:(Color){110,200,120,255});
            Color nc = (r->status==2 || !ok || g_sbOff[i]) ? (Color){130,130,135,255} : (Color){235,235,238,255};
            DrawText(tag,34,ry+7,16,tc);
            DrawText(r->name,150,ry+5,20,nc);
            DrawText(r->when,(int)(W*0.36f),ry+7,16,(Color){170,170,178,255});
            DrawText(r->src,(int)(W*0.70f),ry+7,16,(Color){130,150,175,255});
            if (nCand[i]>0){ const char *cb=TextFormat(i==sel?"%d candidates shown below":"%d candidates  (click)",nCand[i]); int cw=MeasureText(cb,16), bx=W-110-cw;     // its own box, clear of the file name beside it
                DrawRectangle(bx-8,ry+2,cw+16,RH-6,(Color){18,34,58,255}); DrawRectangleLines(bx-8,ry+2,cw+16,RH-6,(Color){60,120,200,255}); DrawText(cb,bx,ry+7,16,(Color){120,190,255,255}); }
            if (i==sel) DrawRectangle(20,ry,4,RH-2,(Color){90,170,255,255});
            if (ok && !r->mus){ float sec=(float)r->snd->frameCount/(float)(r->snd->stream.sampleRate>0?r->snd->stream.sampleRate:44100);
                const char *d=TextFormat("%.2fs",sec); DrawText(d,W-34-MeasureText(d,16),ry+7,16,(Color){150,150,158,255}); }
        }
        DrawRectangle(0,0,W,top-6,(Color){14,14,17,255});
        DrawText("SOUND BOARD",28,18,40,(Color){255,90,70,255});
        DrawText(TextFormat("%d sounds:   %d ok    %d borrowed (stand-ins)    %d missing%s", g_sbN, nOk, nBor, nMiss, nBad?TextFormat("    %d failed to load",nBad):""),30,64,18,(Color){200,200,206,255});
        DrawText("from Chernobyl 1: none  (all 612 of its sound files compared by content, name and length)",W-30-MeasureText("from Chernobyl 1: none  (all 612 of its sound files compared by content, name and length)",16),64,16,(Color){150,150,158,255});
        DrawText(onlyGaps?"showing: MISSING and BORROWED only  (M: show all)":"showing: all  (M: only missing and borrowed)",W-30-MeasureText("showing: MISSING and BORROWED only  (M: show all)",16),30,16,(Color){150,150,158,255});
        DrawRectangle(0,H-34,W,34,(Color){20,20,24,255});
        if (g_sbMsgT>0) DrawText(g_sbMsg,28,H-26,16,(Color){255,220,110,255});
        else if (hover>=0 && g_sb[hover].cand>0) DrawText(g_candWhen[g_sb[hover].cand-1][0]?"click the name: hear it     click UNAPPLY (or BACKSPACE): take it back out of the game":"click the name: hear it     click APPLY (or ENTER): use it for this sound in the game",28,H-26,16,(Color){90,170,255,255});
        else if (hover>=0 && g_sb[hover].slot>0) DrawText(g_trapSndFile[g_sb[hover].slot-1][0]?"click: play     BACKSPACE: put the synthesized sound back":"click: play     (point at a candidate below and press this row's number to replace it)",28,H-26,16,(Color){150,200,160,255});
        else DrawText("click a sound: play it and show its candidates     E or right-click: switch it OFF / ON in the game     SPACE: stop     wheel / arrows: scroll     TAB: next group     M: gaps only     ESC: quit",28,H-26,16,(Color){150,150,158,255});
        EndDrawing();
        if (g_maxFrames>0 && frame==20) TakeScreenshot("build/soundboard.png");
    }
    SbStopAll();
    return 0;
}
