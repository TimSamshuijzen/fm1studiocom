
#include "fm1.h"

/* STAR CAVE - original FM-1 arcade game. */
#define W 120
#define H 120
#define TOP 12
#define BOTTOM 110
#define SX 22
#define TICK (FM1_AUDIO_RATE / 25)
#define UP   (1u << 8)
#define DOWN (1u << 9)
#define FIRE (1u << 18)
#define PLAY (1u << FM1_BTN_PLAY)
#define HOME (1u << FM1_BTN_HOME)
#define NS 8
#define NR 8
#define NB 6

enum { TITLE, RUN, PAUSE, OVER };
enum { VOID, ROCK, SHADE, EDGE, PANEL, DIM, BLUE, CYAN,
       WHITE, GOLD, ORANGE, PINK, PURPLE, GREEN };

static const uint16_t palette[] = {
    0x0844,0x10AB,0x21B2,0x3E9F,0x0867,0x430F,0x03CF,
    0x67FF,0xFFFF,0xFFE0,0xFD20,0xF8D2,0x8A5E,0x47F0
};

static uint8_t back[H][W], front[H][W];
static uint16_t pixels[240 * 16];
static uint8_t ceiling[W], floor_y[W];

typedef struct { int x,y,on; } Object;

static Object stars[NS], rocks[NR], bullets[NB];
static int mode, sy, lives, score, best, collected;
static int immune, cooldown, star_timer, rock_timer;
static int center, halfgap, target_c, target_h, gen_count;
static uint32_t rng=0xA82719D3u, travel, last_tick, pending, old_leds;

/* Only audio writes clock_samples; only loop writes the mailboxes. */
static volatile uint32_t clock_samples, shot_event, star_event, hit_event;
static volatile uint32_t music_level=5;

static int clampi(int n,int lo,int hi) {
    return n<lo?lo:(n>hi?hi:n);
}

static int absi(int n) { return n<0?-n:n; }

static int randn(int n) {
    rng ^= rng<<13;
    rng ^= rng>>17;
    rng ^= rng<<5;
    return (int)(rng % (uint32_t)n);
}

/* ---- Small 3 x 5 bitmap font; digits followed by A-Z. ---- */

static const uint8_t font[36][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},
    {5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},{7,1,2,2,2},
    {7,5,7,5,7},{7,5,7,1,7},
    {2,5,7,5,5},{6,5,6,5,6},{7,4,4,4,7},{6,5,5,5,6},
    {7,4,6,4,7},{7,4,6,4,4},{7,4,5,5,7},{5,5,7,5,5},
    {7,2,2,2,7},{1,1,1,5,7},{5,5,6,5,5},{4,4,4,4,7},
    {5,7,7,5,5},{5,7,7,7,5},{7,5,5,5,7},{7,5,7,4,4},
    {7,5,5,7,1},{6,5,6,5,5},{7,4,7,1,7},{7,2,2,2,2},
    {5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},
    {5,5,2,2,2},{7,1,2,4,7}
};

static void dot(int x,int y,int c) {
    if ((unsigned)x<W && (unsigned)y<H)
        back[y][x]=(uint8_t)c;
}

static void rect(int x,int y,int w,int h,int c) {
    int x0=clampi(x,0,W), x1=clampi(x+w,0,W);
    int y0=clampi(y,0,H), y1=clampi(y+h,0,H);

    for(int j=y0;j<y1;j++)
        for(int i=x0;i<x1;i++)
            back[j][i]=(uint8_t)c;
}

static void text(int x,int y,const char *s,int size,int c) {
    for(;*s;s++,x+=4*size) {
        int id=(*s>='0'&&*s<='9')?*s-'0':
               (*s>='A'&&*s<='Z')?10+*s-'A':-1;

        for(int j=0;j<5;j++) {
            int bits=id>=0?font[id][j]:0;

            if(*s=='/') bits=(j<2)?1:(j==2?2:4);
            if(*s=='-') bits=j==2?7:0;

            for(int i=0;i<3;i++)
                if(bits&(4>>i))
                    rect(x+i*size,y+j*size,size,size,c);
        }
    }
}

static void centered(int y,const char *s,int size,int c) {
    int n=0;
    while(s[n]) n++;
    text((W-(4*n-1)*size)/2,y,s,size,c);
}

static void number(int x,int y,int n,int digits,int c) {
    char s[7];
    s[digits]=0;

    for(int i=digits-1;i>=0;i--) {
        s[i]=(char)('0'+n%10);
        n/=10;
    }

    text(x,y,s,1,c);
}

/* ---- Cave generation and gameplay. ---- */

static void new_column(int x) {
    if(gen_count%32==0) {
        target_c=45+randn(33);
        target_h=26+randn(12);
    }

    if(gen_count%3==0)
        center+=(target_c>center)-(target_c<center);

    if(gen_count%5==0)
        halfgap+=(target_h>halfgap)-(target_h<halfgap);

    center=clampi(center,TOP+4+halfgap,BOTTOM-4-halfgap);

    ceiling[x]=(uint8_t)(center-halfgap);
    floor_y[x]=(uint8_t)(center+halfgap);

    gen_count++;
    if(gen_count==960) gen_count=0;
}

static void spawn(Object *a,int count,int x,int middle) {
    int lo=ceiling[x]+8, hi=floor_y[x]-8;

    for(int i=0;i<count;i++) {
        if(!a[i].on) {
            a[i]=(Object){
                x,
                middle?(lo+hi)/2:lo+randn(hi-lo+1),
                1
            };
            return;
        }
    }
}

static void award(int points) {
    score=clampi(score+points,0,99999);
    if(score>best) best=score;
}

static void reset_game(void) {
    center=target_c=61;
    halfgap=target_h=35;
    gen_count=0;
    travel=0;

    for(int x=0;x<W;x++) new_column(x);

    sy=(ceiling[SX]+floor_y[SX])/2;

    lives=3;
    score=0;
    collected=0;
    immune=0;
    cooldown=0;
    star_timer=36;
    rock_timer=48;

    for(int i=0;i<NS;i++) stars[i].on=0;
    for(int i=0;i<NR;i++) rocks[i].on=0;
    for(int i=0;i<NB;i++) bullets[i].on=0;

    spawn(stars,NS,52,1);
    spawn(stars,NS,84,0);
    spawn(rocks,NR,110,0);

    pending=0;
}

static void damage(void) {
    if(immune) return;

    --lives;

    hit_event=((hit_event+4u)&~3u)|(lives?2u:3u);

    sy=(ceiling[SX]+floor_y[SX])/2;
    immune=50;

    if(lives==0) mode=OVER;
}

static void step(uint32_t keys) {
    if(mode!=RUN) return;

    if(immune) immune--;
    if(cooldown) cooldown--;

    travel++;

    /* Scroll the cave one logical pixel left. */
    for(int x=0;x<W-1;x++) {
        ceiling[x]=ceiling[x+1];
        floor_y[x]=floor_y[x+1];
    }

    new_column(W-1);

    /* Move existing objects with the cave. */
    for(int i=0;i<NS;i++)
        if(stars[i].on && --stars[i].x < -4)
            stars[i].on=0;

    for(int i=0;i<NR;i++)
        if(rocks[i].on && --rocks[i].x < -5)
            rocks[i].on=0;

    /* Generate new stars and crystals. */
    if(--star_timer<=0) {
        spawn(stars,NS,W-1,0);
        star_timer=32+randn(13);
    }

    if(--rock_timer<=0) {
        spawn(rocks,NR,W-1,0);
        rock_timer=44+randn(14);
    }

    /* Move the player's ship. */
    sy=clampi(
        sy+2*((keys&DOWN?1:0)-(keys&UP?1:0)),
        TOP+4,BOTTOM-5
    );

    /* Fire, with a short cooldown for automatic repeat. */
    if((keys&FIRE)&&!cooldown) {
        for(int i=0;i<NB;i++) {
            if(!bullets[i].on) {
                bullets[i]=(Object){SX+6,sy,1};
                cooldown=6;
                shot_event++;
                break;
            }
        }
    }

    /* Move bullets and check collisions along their paths. */
    for(int i=0;i<NB;i++) {
        if(!bullets[i].on) continue;

        Object *b=&bullets[i];

        for(int k=0;k<4 && b->on;k++) {
            int x=++b->x;

            if(x>=W || b->y<=ceiling[x] ||
               b->y>=floor_y[x]) {
                b->on=0;
                break;
            }

            for(int j=0;j<NR;j++) {
                if(rocks[j].on &&
                   absi(x-rocks[j].x)<=4 &&
                   absi(b->y-rocks[j].y)<=4) {

                    rocks[j].on=0;
                    b->on=0;
                    award(5);

                    hit_event=((hit_event+4u)&~3u)|1u;
                    break;
                }
            }
        }
    }

    /* Collect stars. Each one advances the musical scale. */
    for(int i=0;i<NS;i++) {
        if(stars[i].on &&
           absi(stars[i].x-SX)<=7 &&
           absi(stars[i].y-sy)<=6) {

            stars[i].on=0;
            award(10);

            star_event=((star_event+8u)&~7u)|
                       (uint32_t)(collected%6);

            collected++;
        }
    }

    /* Check the cave walls across the width of the ship. */
    int lo=TOP+4, hi=BOTTOM-5;

    for(int x=SX-5;x<=SX+5;x++) {
        if(ceiling[x]+4>lo) lo=ceiling[x]+4;
        if(floor_y[x]-4<hi) hi=floor_y[x]-4;
    }

    if(sy<lo || sy>hi) {
        if(!immune) damage();
        sy=clampi(sy,lo,hi);
    }

    /* Check ship/crystal collisions. */
    for(int i=0;i<NR;i++) {
        if(rocks[i].on && !immune &&
           absi(rocks[i].x-SX)<=8 &&
           absi(rocks[i].y-sy)<=7) {

            rocks[i].on=0;
            damage();
        }
    }
}

/* ---- Pixel sprites and dirty-tile display. ---- */

/* Dots are transparent; other characters select palette colours. */

static const char *const ship_sprite[]={
    "..7........",
    "..778......",
    ".7777888...",
    "67777788887",
    ".7777888...",
    "..778......",
    "..7........"
};

static const char *const star_sprite[]={
    "...9...",
    "...9...",
    "..989..",
    "9988899",
    "..989..",
    "...9...",
    "...9..."
};

static const char *const rock_sprite[]={
    "...C.....",
    "..CBC....",
    ".CBBBC...",
    "CBB8BBC..",
    ".CBBBBBC.",
    "..CBBBC..",
    "...CBC...",
    "....C....",
    "........."
};

static void sprite(int x,int y,const char *const *rows,int w,int h) {
    for(int j=0;j<h;j++) {
        for(int i=0;i<w;i++) {
            char c=rows[j][i];

            if(c!='.')
                dot(x+i,y+j,c>='A'?c-'A'+10:c-'0');
        }
    }
}

static void compose(void) {
    rect(0,0,W,H,VOID);

    /* Draw the rock walls and illuminated edges. */
    for(int x=0;x<W;x++) {
        int a=ceiling[x], b=floor_y[x];

        for(int y=TOP;y<=a;y++)
            back[y][x]=(uint8_t)(
                y==a?EDGE:(y>=a-2?SHADE:ROCK)
            );

        for(int y=b;y<BOTTOM;y++)
            back[y][x]=(uint8_t)(
                y==b?EDGE:(y<=b+2?SHADE:ROCK)
            );
    }

    /* Slowly scrolling background particles. */
    for(int i=0;i<22;i++) {
        int x=(int)((uint32_t)(i*37+117)+
                   (120u-travel/3u%120u))%120;

        int y=TOP+4+(i*23)%90;

        if(y>ceiling[x]+2 && y<floor_y[x]-2)
            dot(x,y,DIM);
    }

    /* Collectibles and obstacles. */
    for(int i=0;i<NS;i++)
        if(stars[i].on)
            sprite(stars[i].x-3,stars[i].y-3,
                   star_sprite,7,7);

    for(int i=0;i<NR;i++)
        if(rocks[i].on)
            sprite(rocks[i].x-4,rocks[i].y-4,
                   rock_sprite,9,9);

    /* Laser bolts. */
    for(int i=0;i<NB;i++) {
        if(bullets[i].on) {
            rect(bullets[i].x-2,bullets[i].y,3,1,GOLD);
            dot(bullets[i].x,bullets[i].y,WHITE);
        }
    }

    /* Ship blinks during temporary invulnerability. */
    if(!immune || ((immune/3)%2)==0) {
        dot(SX-6,sy,(travel&1u)?ORANGE:GOLD);
        sprite(SX-5,sy-3,ship_sprite,11,7);
    }

    /* Top status bar. */
    rect(0,0,W,TOP,PANEL);
    rect(0,TOP-1,W,1,EDGE);

    text(3,3,"SCORE",1,CYAN);
    number(26,3,score,5,WHITE);

    text(87,3,"LIVES",1,CYAN);
    number(111,3,lives,1,GOLD);

    /* Bottom control and music-volume bar. */
    rect(0,BOTTOM,W,H-BOTTOM,PANEL);
    rect(0,BOTTOM,W,1,EDGE);

    text(3,113,"W/S MOVE J FIRE",1,CYAN);
    text(89,113,"MUSIC",1,DIM);
    number(113,113,(int)music_level,1,WHITE);

    /* Title, pause and game-over panels. */
    if(mode!=RUN) {
        rect(8,33,104,55,EDGE);
        rect(10,35,100,51,PANEL);

        if(mode==TITLE) {
            centered(40,"STAR CAVE",2,GOLD);
            centered(56,"COLLECT THE STARS",1,WHITE);
            centered(65,"J OR PLAY TO START",1,CYAN);
            centered(74,"SHOOT THE CRYSTALS",1,DIM);

        } else if(mode==PAUSE) {
            centered(43,"PAUSED",2,CYAN);
            centered(63,"PLAY TO RESUME",1,WHITE);

        } else {
            centered(40,"GAME OVER",2,PINK);
            text(35,56,"BEST",1,CYAN);
            number(59,56,best,5,GOLD);
            centered(70,"J OR PLAY TO RETRY",1,WHITE);
        }
    }
}

/* Test whether an 8 x 8 logical tile has changed. */

static int changed(int tx,int ty) {
    for(int y=ty*8;y<ty*8+8;y++)
        for(int x=tx*8;x<tx*8+8;x++)
            if(back[y][x]!=front[y][x])
                return 1;

    return 0;
}

/* Combine adjacent dirty tiles into one LCD transaction.
 * Each logical pixel becomes a 2 x 2 physical pixel.
 */

static void flush_run(int first,int end,int ty) {
    int x0=first*8, y0=ty*8;
    int width=(end-first)*8;
    int stride=width*2;

    for(int j=0;j<8;j++) {
        for(int i=0;i<width;i++) {
            uint16_t c=palette[back[y0+j][x0+i]];
            int p=j*2*stride+i*2;

            pixels[p]=c;
            pixels[p+1]=c;
            pixels[p+stride]=c;
            pixels[p+stride+1]=c;
        }
    }

    if(fm1_lcd_blit(
        (uint32_t)(x0*2),
        (uint32_t)(y0*2),
        (uint32_t)stride,
        16,
        pixels,
        (uint32_t)(stride*16)
    )==0) {
        for(int j=0;j<8;j++)
            for(int i=0;i<width;i++)
                front[y0+j][x0+i]=back[y0+j][x0+i];
    }
}

static void render(void) {
    compose();

    for(int ty=0;ty<H/8;ty++) {
        int tx=0;

        while(tx<W/8) {
            if(!changed(tx,ty)) {
                tx++;
                continue;
            }

            int first=tx++;

            while(tx<W/8 && changed(tx,ty))
                tx++;

            flush_run(first,tx,ty);
        }
    }
}

/* ---- Audio: lightweight triangle-wave synthesizer. ---- */

typedef struct {
    float phase,inc,env,decay,sweep;
} Voice;

static Voice lead,bass,chime,laser,impact;

/* C major pentatonic, including the upper tonic. */
static const float scale[6]={
    523.251f,587.330f,659.255f,
    783.991f,880.0f,1046.502f
};

/* Original 32-step melody. Zero represents a rest. */
static const float tune[32]={
    261.626f,329.628f,391.995f,329.628f,
    293.665f,329.628f,440.0f,391.995f,

    261.626f,329.628f,523.251f,440.0f,
    391.995f,329.628f,293.665f,0.0f,

    329.628f,391.995f,440.0f,523.251f,
    440.0f,391.995f,329.628f,293.665f,

    261.626f,293.665f,329.628f,391.995f,
    440.0f,391.995f,261.626f,0.0f
};

static const float bassline[8]={
    130.813f,146.832f,110.0f,97.999f,
    110.0f,130.813f,97.999f,130.813f
};

static int beat, samples_left;
static uint32_t seen_shot,seen_star,seen_hit;
static float volume_smooth;

/* Trigger an oscillator with a decaying envelope
 * and optional frequency sweep.
 */

static void voice_start(
    Voice *v,float hz,float duration,float end_hz
) {
    float n=duration*(float)FM1_AUDIO_RATE;

    v->phase=0.25f;
    v->inc=hz/(float)FM1_AUDIO_RATE;
    v->env=1.0f;
    v->decay=1.0f/n;
    v->sweep=(end_hz-hz)/((float)FM1_AUDIO_RATE*n);
}

/* No trigonometry, allocation or division per oscillator sample. */

static inline float voice_next(Voice *v) {
    if(v->env<=0.0f) return 0.0f;

    float p=v->phase;
    float wave=p<0.5f?4.0f*p-1.0f:3.0f-4.0f*p;
    float result=wave*v->env*v->env;

    v->phase+=v->inc;
    if(v->phase>=1.0f) v->phase-=1.0f;

    v->inc+=v->sweep;
    v->env-=v->decay;

    return result;
}

void audio(int32_t *out,int frames) {
    /* Read sound-event mailboxes once per block. */
    uint32_t e=shot_event;

    if(e!=seen_shot) {
        seen_shot=e;
        voice_start(&laser,950.0f,0.085f,230.0f);
    }

    e=star_event;

    if(e!=seen_star) {
        seen_star=e;
        float f=scale[e&7u];
        voice_start(&chime,f,0.38f,f);
    }

    e=hit_event;

    if(e!=seen_hit) {
        seen_hit=e;

        if((e&3u)==1u)
            voice_start(&impact,180.0f,0.15f,70.0f);
        else
            voice_start(
                &impact,240.0f,
                (e&3u)==3u?0.65f:0.30f,
                55.0f
            );
    }

    float vol=(float)fm1_master()/(float)FM1_MASTER_MAX;
    float music=(float)music_level*0.125f;

    for(int i=0;i<frames;i++) {
        /* Sample-accurate music sequencer. */
        if(samples_left<=0) {
            float f=tune[beat];

            if(f>0.0f)
                voice_start(&lead,f,0.19f,f);
            else
                lead.env=0.0f;

            if((beat&3)==0) {
                f=bassline[beat/4];
                voice_start(&bass,f,0.76f,f);
            }

            beat=(beat+1)&31;
            samples_left=8820;
        }

        samples_left--;

        /* Mix five independent synthesizer voices. */
        float m=voice_next(&lead)*music*0.19f;
        float b=voice_next(&bass)*music*0.15f;
        float s=voice_next(&chime)*0.30f;
        float z=voice_next(&laser)*0.10f;
        float h=voice_next(&impact)*0.20f;

        /* Smooth the MASTER volume. */
        volume_smooth+=(vol-volume_smooth)*0.002f;

        float gain=volume_smooth*(float)FM1_AUDIO_MAX;

        /* Stereo mix with conservative headroom. */
        out[2*i]=(int32_t)(
            (m+b+0.60f*s+z+h)*gain
        );

        out[2*i+1]=(int32_t)(
            (0.65f*m+b+s+0.65f*z+h)*gain
        );
    }

    /* Audio provides the game's timebase. */
    clock_samples+=(uint32_t)frames;
}

/* ---- FM-1 entry points. ---- */

void setup(void) {
    reset_game();
    mode=TITLE;

    /* Invalidate the display cache for the first draw. */
    for(int y=0;y<H;y++)
        for(int x=0;x<W;x++)
            front[y][x]=255;

    fm1_led_key(FM1_KEY_NOTE0+8,0);
    fm1_led_key(FM1_KEY_NOTE0+9,0);
    fm1_led_key(FM1_KEY_NOTE0+18,0);
    fm1_led_key(FM1_BTN_PLAY,0);

    render();

    last_tick=clock_samples;
}

void loop(void) {
    uint32_t buttons=fm1_input_edges(0);
    uint32_t edges=fm1_input_note_edges();
    uint32_t held=fm1_in.notes;
    uint32_t now=clock_samples;

    int redraw=0;

    /* SELECT encoder controls music level, from 0 to 8. */
    int32_t clicks=fm1_enc_take(FM1_ENC_SELECT);

    if(clicks) {
        int delta=clicks>8?8:(clicks< -8?-8:(int)clicks);

        music_level=(uint32_t)clampi(
            (int)music_level+delta,0,8
        );

        redraw=1;
    }

    /* Menu and game-state controls. */
    if(buttons&HOME) {
        reset_game();
        mode=TITLE;
        redraw=1;

    } else if(
        (mode==TITLE || mode==OVER) &&
        ((buttons&PLAY)||(edges&FIRE))
    ) {
        reset_game();
        mode=RUN;
        redraw=1;
        last_tick=now;
        edges=0;

    } else if(buttons&PLAY) {
        mode=mode==RUN?PAUSE:RUN;
        redraw=1;
        last_tick=now;
        pending=0;
        edges=0;
    }

    /* Update LEDs only when their state changes. */
    uint32_t leds=held&(UP|DOWN|FIRE);

    if(mode==RUN) leds|=1u;

    if((leds^old_leds)&UP)
        fm1_led_key(FM1_KEY_NOTE0+8,(leds&UP)!=0);

    if((leds^old_leds)&DOWN)
        fm1_led_key(FM1_KEY_NOTE0+9,(leds&DOWN)!=0);

    if((leds^old_leds)&FIRE)
        fm1_led_key(FM1_KEY_NOTE0+18,(leds&FIRE)!=0);

    if((leds^old_leds)&1u)
        fm1_led_key(FM1_BTN_PLAY,(leds&1u)!=0);

    old_leds=leds;

    /* Fixed-rate game updates, independent of loop speed. */
    if(mode==RUN) {
        /* Remember short key presses between game ticks. */
        pending|=edges&(UP|DOWN|FIRE);

        /* Limit catch-up work if the display is slow. */
        if(now-last_tick>=4u*TICK)
            last_tick=now-3u*TICK;

        for(int i=0;
            i<3 && now-last_tick>=TICK && mode==RUN;
            i++
        ) {
            last_tick+=TICK;

            step(held|pending);
            pending=0;
            redraw=1;
        }

    } else {
        last_tick=now;
        pending=0;
    }

    /* Draw from loop(), never from the audio interrupt. */
    if(redraw) render();
}
