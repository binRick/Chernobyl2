// Sound board (dev): `--sounds` / ./dev-sounds.sh. Every sound the game can make, grouped by what
// it is for, each row showing its trigger, where it comes from (file, a cut of a file, or
// synthesized in code) and its length -- click a row to hear it. Rows marked MISSING are cues
// the game has no sound for yet (or is borrowing another sound for): the shopping list.
//   click a row: play (again: restart)   SPACE: stop everything   wheel / up / down: scroll
//   TAB: next group   M: only the MISSING and BORROWED rows   ESC: quit
typedef struct { const char *group, *name, *when, *src; Sound *snd; int *has; Music *mus; int status; } SbRow;   // status: 0 ok, 1 borrowed (stand-in), 2 missing
#define SB_MAX 220
static SbRow g_sb[SB_MAX]; static int g_sbN=0;
static int g_sbOne=1;                                  // "has" for sounds with no flag of their own
static void SbAdd(const char *group, const char *name, const char *when, const char *src, Sound *snd, int *has, int status){
    if (g_sbN<SB_MAX) g_sb[g_sbN++]=(SbRow){group,name,when,src,snd,has,NULL,status};
}
static void SbBuild(void){
    g_sbN=0;
    // ---- WEAPONS
    SbAdd("WEAPONS","Minigun fire (loop)","trigger held, spun up","assets/minigun_fire.mp3",&g_weapons[2].fireSnd,&g_weapons[2].hasSnd,0);
    SbAdd("WEAPONS","Minigun spin-down","trigger released","assets/minigun_cooldown.mp3",&g_weapons[2].auxSnd,&g_weapons[2].hasAuxSnd,0);
    SbAdd("WEAPONS","Minigun spin-up","trigger pressed, barrels winding up","assets/minigun_spinup.wav (cut)",&g_spinUpSnd,&g_hasSpinUpSnd,0);
    SbAdd("WEAPONS","Flamethrower roar (loop)","trigger held","assets/flame_fire.mp3",&g_weapons[5].fireSnd,&g_weapons[5].hasSnd,0);
    SbAdd("WEAPONS","Flamethrower ignite / shut off","start and end of a burst","-",NULL,NULL,2);
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
    SbAdd("WEAPONS","Weapon switch / draw","keys 1-4","-",NULL,NULL,2);
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
    SbAdd("PLAYER","Pickup: health / mega / quad / speed","a sound of its own per pickup kind","-",NULL,NULL,2);
    // ---- ENEMIES
    SbAdd("ENEMIES","Attack roar","a melee enemy lunges","assets/enemy_attack.mp3",&g_attackSnd,&g_hasAttackSnd,0);
    for (int k=0;k<g_nDeathSnd && k<8;k++){ static char nm[8][32], fl[8][40];
        snprintf(nm[k],sizeof nm[k],"Death cry %d",k+1); snprintf(fl[k],sizeof fl[k],"assets/enemy_death%d.mp3",k+1);
        SbAdd("ENEMIES",nm[k],"an enemy dies (one at random)",fl[k],&g_deathSnd[k],&g_sbOne,0); }
    SbAdd("ENEMIES","Death scream (shotgun)","an enemy killed by the shotgun, about half the time","assets/death_shotgun.mp3",&g_sgDeathSnd,&g_hasSgDeathSnd,0);
    SbAdd("ENEMIES","Rifleman / gunner shot","an enemy fires","assets/ak74_fire_single.mp3",&g_eShotSnd,&g_hasEShot,0);
    SbAdd("ENEMIES","Mutant bolt","a mutant throws a radiation bolt","assets/flame_fire.mp3 (the flamethrower's)",&g_eBoltSnd,&g_hasEBolt,1);
    SbAdd("ENEMIES","Priest raises the dead","a corpse gets up","assets/wave_start.wav (the wave sting)",&g_eRaiseSnd,&g_hasERaise,1);
    SbAdd("ENEMIES","Melee weapon hits the player","cleaver, knife, shovel, axe, cross landing","-",NULL,NULL,2);
    SbAdd("ENEMIES","Running feet (loop)","a hostile running at the player within 15 m","assets/enemy_run.mp3",&g_runSnd,&g_hasRunSnd,0);
    SbAdd("ENEMIES","Walking footsteps","enemies walking nearby","-",NULL,NULL,2);
    SbAdd("ENEMIES","Rising from the floor","an enemy spawns","-",NULL,NULL,2);
    SbAdd("ENEMIES","Knocked down","an enemy is flattened by a blast or point-blank buckshot","assets/enemy_knockdown.mp3",&g_knockSnd,&g_hasKnockSnd,0);
    SbAdd("ENEMIES","Body hits the floor","an enemy falls dead","-",NULL,NULL,2);
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
    SbAdd("FEEDBACK","Low ammo warning","last rounds in the magazine","-",NULL,NULL,2);
    SbAdd("FEEDBACK","Menu rollover","the pointer moves onto a row of the ESC menu","assets/menu_hover.mp3",&g_menuSnd,&g_hasMenuSnd,0);
    SbAdd("FEEDBACK","Menu select","a row of the ESC menu is clicked","assets/menu_select.mp3",&g_menuSelSnd,&g_hasMenuSelSnd,0);
    SbAdd("FEEDBACK","Leaderboard rank","a score is posted","-",NULL,NULL,2);
    // ---- AMBIENCE
    SbAdd("AMBIENCE","Drone, wind and Geiger counter (loop)","always, quieter in a fight","synthesized",NULL,&g_hasAmb,0); if (g_sbN>0) g_sb[g_sbN-1].mus=&g_amb[0];
    SbAdd("AMBIENCE","Tension pulse (loop)","swells with the hostiles on the field","synthesized",NULL,&g_hasAmb,0);          if (g_sbN>0) g_sb[g_sbN-1].mus=&g_amb[1];
    for (int k=0;k<3;k++){ static const char *nm[3]={"Wave 1 background (loop)","Wave 2 background (loop)","Wave 3 background (loop)"};
        static const char *wh[3]={"wave 1, then every third wave","wave 2, then every third wave","wave 3, then every third wave"};
        static const char *fl[3]={"assets/wave1_bg.mp3","assets/wave2_bg.mp3","assets/wave3_bg.mp3"};
        SbAdd("AMBIENCE",nm[k],wh[k],fl[k],NULL,&g_nWaveBg,0); if (g_sbN>0 && g_nWaveBg>k) g_sb[g_sbN-1].mus=&g_waveBg[k]; else if (g_sbN>0) g_sb[g_sbN-1].status=2; }
    SbAdd("AMBIENCE","Background for wave 4 onward","later waves reuse the first three tracks","-",NULL,NULL,2);
}
static int SbLoaded(const SbRow *r){ return r->status!=2 && r->has && *r->has>0 && (r->mus || (r->snd && r->snd->frameCount>0)); }
static void SbStopAll(void){
    for (int i=0;i<g_sbN;i++){ if (!SbLoaded(&g_sb[i])) continue;
        if (g_sb[i].mus) SetMusicVolume(*g_sb[i].mus,0.0f); else StopSound(*g_sb[i].snd); }
}
static int RunSoundboard(void){
    SbBuild(); EnableCursor();
    for (int i=0;i<2;i++) if (g_hasAmb) SetMusicVolume(g_amb[i],0.0f);
    int nOk=0,nBor=0,nMiss=0,nBad=0;
    for (int i=0;i<g_sbN;i++){ if (g_sb[i].status==2) nMiss++; else if (!SbLoaded(&g_sb[i])) nBad++; else if (g_sb[i].status==1) nBor++; else nOk++; }
    DebugLog("soundboard","\"rows\":%d,\"ok\":%d,\"borrowed\":%d,\"missing\":%d,\"failed\":%d", g_sbN,nOk,nBor,nMiss,nBad);
    float scroll=0; int onlyGaps=0, playing=-1, frame=0; const int RH=30, GH=46;
    while (!WindowShouldClose()){
        if (g_maxFrames>0 && ++frame>g_maxFrames) break;
        if (g_hasAmb){ UpdateMusicStream(g_amb[0]); UpdateMusicStream(g_amb[1]); }
        for (int k=0;k<g_nWaveBg;k++) if (IsMusicStreamPlaying(g_waveBg[k])) UpdateMusicStream(g_waveBg[k]);
        int W=GetScreenWidth(), H=GetScreenHeight(); Vector2 mp=GetMousePosition();
        if (IsKeyPressed(KEY_M)){ onlyGaps=!onlyGaps; scroll=0; }
        if (IsKeyPressed(KEY_SPACE)){ SbStopAll(); playing=-1; }
        scroll-=GetMouseWheelMove()*48.0f; if (IsKeyDown(KEY_DOWN)) scroll+=14; if (IsKeyDown(KEY_UP)) scroll-=14;
        // layout pass: y of every visible row; group headers in between
        static int rowY[SB_MAX]; int y=0; const char *grp=NULL; int groupY[32], nG=0;
        for (int i=0;i<g_sbN;i++){ rowY[i]=-1; if (onlyGaps && g_sb[i].status==0 && SbLoaded(&g_sb[i])) continue;
            if (!grp || strcmp(grp,g_sb[i].group)){ grp=g_sb[i].group; if (nG<32) groupY[nG++]=y; y+=GH; }
            rowY[i]=y; y+=RH; }
        float maxScroll=fmaxf(0.0f,(float)y-(H-150)); if (scroll<0) scroll=0; if (scroll>maxScroll) scroll=maxScroll;
        if (IsKeyPressed(KEY_TAB) && nG>0){ int k=0; while (k<nG && groupY[k]<=scroll+1) k++; scroll=(k<nG)?(float)groupY[k]:0.0f; if (scroll>maxScroll) scroll=maxScroll; }
        int top=104, hover=-1;
        for (int i=0;i<g_sbN;i++) if (rowY[i]>=0){ float ry=top+rowY[i]-scroll; if (mp.y>=ry && mp.y<ry+RH && mp.y>top && mp.x>20 && mp.x<W-20) hover=i; }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hover>=0 && SbLoaded(&g_sb[hover])){
            SbStopAll(); SbRow *r=&g_sb[hover];
            if (r->mus){ if (!IsMusicStreamPlaying(*r->mus)) PlayMusicStream(*r->mus); SetMusicVolume(*r->mus,0.8f); } else { SetSoundPan(*r->snd,0.0f); SetSoundPitch(*r->snd,1.0f); SetSoundVolume(*r->snd,1.0f); PlaySound(*r->snd); }
            playing=hover; }
        if (playing>=0 && !g_sb[playing].mus && !IsSoundPlaying(*g_sb[playing].snd)) playing=-1;
        BeginDrawing();
        ClearBackground((Color){14,14,17,255});
        grp=NULL;
        for (int i=0;i<g_sbN;i++){ if (rowY[i]<0) continue; SbRow *r=&g_sb[i]; int ry=(int)(top+rowY[i]-scroll);
            if (!grp || strcmp(grp,r->group)){ grp=r->group; int gy=ry-GH+14;
                if (gy>top-GH && gy<H){ DrawText(grp,28,gy,24,(Color){255,90,70,255}); DrawRectangle(28,gy+28,W-56,1,(Color){70,40,36,255}); } }
            if (ry<top-RH || ry>H) continue;
            int ok=SbLoaded(r);
            if (i==playing) DrawRectangle(20,ry,W-40,RH-2,(Color){60,40,20,235}); else if (i==hover) DrawRectangle(20,ry,W-40,RH-2,(Color){34,32,36,235});
            const char *tag = r->status==2 ? "MISSING" : !ok ? "NOT LOADED" : r->status==1 ? "BORROWED" : (i==playing ? "PLAYING" : "OK");
            Color tc = r->status==2 ? (Color){230,80,70,255} : !ok ? (Color){230,80,70,255} : r->status==1 ? (Color){240,180,60,255} : (i==playing?(Color){255,220,90,255}:(Color){110,200,120,255});
            Color nc = (r->status==2 || !ok) ? (Color){150,150,155,255} : (Color){235,235,238,255};
            DrawText(tag,34,ry+7,16,tc);
            DrawText(r->name,150,ry+5,20,nc);
            DrawText(r->when,(int)(W*0.36f),ry+7,16,(Color){170,170,178,255});
            DrawText(r->src,(int)(W*0.70f),ry+7,16,(Color){130,150,175,255});
            if (ok && !r->mus){ float sec=(float)r->snd->frameCount/(float)(r->snd->stream.sampleRate>0?r->snd->stream.sampleRate:44100);
                const char *d=TextFormat("%.2fs",sec); DrawText(d,W-34-MeasureText(d,16),ry+7,16,(Color){150,150,158,255}); }
        }
        DrawRectangle(0,0,W,top-6,(Color){14,14,17,255});
        DrawText("SOUND BOARD",28,18,40,(Color){255,90,70,255});
        DrawText(TextFormat("%d sounds:   %d ok    %d borrowed (stand-ins)    %d missing%s", g_sbN, nOk, nBor, nMiss, nBad?TextFormat("    %d failed to load",nBad):""),30,64,18,(Color){200,200,206,255});
        DrawText(onlyGaps?"showing: MISSING and BORROWED only  (M: show all)":"showing: all  (M: only missing and borrowed)",W-30-MeasureText("showing: MISSING and BORROWED only  (M: show all)",16),30,16,(Color){150,150,158,255});
        DrawRectangle(0,H-34,W,34,(Color){20,20,24,255});
        DrawText("click a row: play     SPACE: stop     wheel / arrows: scroll     TAB: next group     M: gaps only     ESC: quit",28,H-26,16,(Color){150,150,158,255});
        EndDrawing();
        if (g_maxFrames>0 && frame==20) TakeScreenshot("build/soundboard.png");
    }
    SbStopAll();
    return 0;
}
