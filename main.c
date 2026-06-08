/*
 * Jazz Jackrabbit - Infinite Runner
 * Sega Master System / devkitSMS
 *
 * Button 1: Jump / Double Jump
 * Button 2: Shoot
 * Stomp enemies by landing on them.
 * RNG seeded by time spent on title screen -> unique level each play.
 */

#include "SMSlib.h"
#include "palettes.h"
#include "tiles.h"
#include "title_data.h"

/* ============================================================
   Constants
   ============================================================ */
#define SCREEN_W        32
#define SCREEN_H        24
#define SCREEN_PX_W     256
#define SCREEN_PX_H     192

#define TILE_SKY          0
#define TILE_GROUND_TOP  16
#define TILE_GROUND_FILL 32
#define TILE_CLOUD       48
#define DIGIT_TILE_BASE  64

#define SPR_TILE_BASE    80
#define SPR_TURTLE_BASE  180

#define JAZZ_SCREEN_X   32
#define JAZZ_PX_W       32
#define JAZZ_PX_H       40
#define JAZZ_TILES_W    4
#define JAZZ_TILES_H    5

#define GRAVITY         1
#define JUMP_VELOCITY   (-16)
#define JUMP2_VELOCITY  (-13)

#define FLOOR_BASE_ROW  18
#define FLOOR_MIN_ROW   14
#define FLOOR_MAX_ROW   20
#define FLOOR_PIT       99

#define INITIAL_SPEED   2
#define MAX_SPEED       6

#define MAX_ENEMIES     1
#define MAX_BULLETS     3

#define ETYPE_TURTLE    0
#define ETYPE_SPARK     1
#define ETYPE_BEE       2
#define ETYPE_HORNET    3

/* Game states */
#define STATE_TITLE     0
#define STATE_PLAYING   1

/* ============================================================
   RNG (16-bit Galois LFSR)
   ============================================================ */
static unsigned int rng_state;

static unsigned int rng_next(void) {
    /* Galois LFSR taps at bits 15,13,12,10 */
    unsigned int lsb = rng_state & 1;
    rng_state >>= 1;
    if (lsb) rng_state ^= 0xB400u;
    return rng_state;
}

/* ============================================================
   Data types
   ============================================================ */
typedef struct {
    int x, y, active, type, frame, anim_timer;
} Enemy;

typedef struct {
    int x, y, active;
} Bullet;

/* ============================================================
   Global state
   ============================================================ */
static int game_state;

static int jazz_y, jazz_vy, on_ground, jumps_left;
static int anim_frame, anim_timer;
static int scroll_x, scroll_speed, frame_count;
static unsigned int score;
static int score_changed, dead, dead_timer;

static Enemy  enemies[MAX_ENEMIES];
static Bullet bullets[MAX_BULLETS];
static int    enemy_spawn_timer, enemy_spawn_interval, shoot_cooldown;

/* Floor system */
/* Floor pattern is now driven by RNG — no fixed array needed */
static int floor_col_height[SCREEN_W];
static int floor_pattern_pos, floor_cur_height;
static int pixels_since_last_col, next_floor_col;
static int floor_flat_remaining;  /* how many more flat cols before next event */
static int floor_pit_remaining;   /* how many more pit cols to emit */

/* ============================================================
   RNG-driven floor generation
   ============================================================ */
static signed char next_floor_delta(void) {
    unsigned int r;

    /* Emit remaining pit columns first */
    if (floor_pit_remaining > 0) {
        floor_pit_remaining--;
        return 9;
    }

    if (floor_flat_remaining > 0) {
        floor_flat_remaining--;
        return 0;
    }

    r = rng_next();
    switch (r & 7) {
        case 0: floor_flat_remaining = 3; return  1;   /* step down, then flat */
        case 1: floor_flat_remaining = 3; return -1;   /* step up, then flat */
        case 2: floor_flat_remaining = 2; return  1;   /* big step down (2 cols) */
        case 3: floor_flat_remaining = 2; return -1;   /* big step up (2 cols) */
        case 4:
        case 5: floor_flat_remaining = ((r >> 3) & 7) + 4; return 0; /* long flat run */
        case 6:
            /* pit: 3-5 cols wide, then flat recovery */
            floor_pit_remaining = ((r >> 3) & 3) + 2;  /* 2-5 extra pit cols */
            floor_flat_remaining = 4;                   /* flat run after pit */
            return 9;                                    /* first pit col */
        default: floor_flat_remaining = 6; return 0;   /* long flat */
    }
}

/* ============================================================
   Floor helpers
   ============================================================ */
static void write_floor_column(int tcol, int ground_row) {
    unsigned int tile;
    int y, sub_row, sub_col;
    sub_col = tcol & 3;
    for (y = 2; y < SCREEN_H; y++) {
        sub_row = y & 3;
        if (ground_row == FLOOR_PIT)   tile = TILE_SKY         + sub_row*4 + sub_col;
        else if (y > ground_row)       tile = TILE_GROUND_FILL + sub_row*4 + sub_col;
        else if (y == ground_row)      tile = TILE_GROUND_TOP  + sub_row*4 + sub_col;
        else                           tile = TILE_SKY         + sub_row*4 + sub_col;
        SMS_loadTileMap(tcol, y, &tile, 2);
    }
    floor_col_height[tcol] = ground_row;
}

static int get_ground_y_at(int screen_px_x) {
    int tcol = ((screen_px_x + scroll_x) >> 3) & 31;
    int gr   = floor_col_height[tcol];
    return (gr == FLOOR_PIT) ? SCREEN_PX_H + 32 : gr * 8;
}

static void init_floor(void) {
    int i;
    floor_cur_height = FLOOR_BASE_ROW;
    floor_pattern_pos = pixels_since_last_col = next_floor_col = 0;
    floor_flat_remaining = 8;
    floor_pit_remaining  = 0;
    for (i = 0; i < SCREEN_W; i++) floor_col_height[i] = FLOOR_BASE_ROW;
}

/* ============================================================
   Digit tiles
   ============================================================ */
static const unsigned char digit_bits[10][8] = {
    {0x7E,0x42,0x42,0x42,0x42,0x42,0x7E,0x00},
    {0x08,0x18,0x08,0x08,0x08,0x08,0x1C,0x00},
    {0x7E,0x02,0x02,0x7E,0x40,0x40,0x7E,0x00},
    {0x7E,0x02,0x02,0x3E,0x02,0x02,0x7E,0x00},
    {0x42,0x42,0x42,0x7E,0x02,0x02,0x02,0x00},
    {0x7E,0x40,0x40,0x7E,0x02,0x02,0x7E,0x00},
    {0x7E,0x40,0x40,0x7E,0x42,0x42,0x7E,0x00},
    {0x7E,0x02,0x02,0x02,0x02,0x02,0x02,0x00},
    {0x7E,0x42,0x42,0x7E,0x42,0x42,0x7E,0x00},
    {0x7E,0x42,0x42,0x7E,0x02,0x02,0x7E,0x00},
};

static void load_digit_tiles(void) {
    unsigned char tile_data[32];
    int d, row;
    for (d = 0; d < 10; d++) {
        for (row = 0; row < 8; row++) {
            unsigned char bits = digit_bits[d][row];
            tile_data[row*4+0] = 0; tile_data[row*4+1] = bits;
            tile_data[row*4+2] = 0; tile_data[row*4+3] = 0;
        }
        SMS_loadTiles(tile_data, DIGIT_TILE_BASE + d, 32);
    }
}

static void load_gameplay_tiles(void) {
    SMS_loadTiles(bg_tiles,     0,               sizeof(bg_tiles));
    load_digit_tiles();
    SMS_loadTiles(jazz_tiles,   SPR_TILE_BASE,   sizeof(jazz_tiles));
    SMS_loadTiles(turtle_tiles, SPR_TURTLE_BASE, sizeof(turtle_tiles));
    SMS_loadTiles(bee_tiles,    SPR_BEE_BASE,    sizeof(bee_tiles));
    SMS_loadTiles(spark_tiles,  SPR_SPARK_BASE,  sizeof(spark_tiles));
    SMS_loadTiles(hornet_tiles, SPR_HORNET_BASE, sizeof(hornet_tiles));
    SMS_loadTiles(bullet_tile,  SPR_BULLET_BASE, sizeof(bullet_tile));
}

/* ============================================================
   Title screen
   ============================================================ */
static void show_title_screen(void) {
    int i;
    unsigned int tile;

    /* Load title palette into BG palette */
    SMS_loadBGPalette(title_palette);

    /* Load deduplicated title tiles into VRAM 0..TITLE_UNIQUE_TILES-1 */
    SMS_loadTiles(title_tiles, 0, sizeof(title_tiles));

    /* Write tilemap from precomputed index table */
    for (i = 0; i < 768; i++) {
        tile = title_tilemap[i];
        SMS_loadTileMap(i & 31, i >> 5, &tile, 2);
    }
}

/* ============================================================
   Score
   ============================================================ */
static void draw_score(void) {
    unsigned int s = score, d[5], tile;
    int i;
    for (i=4;i>=0;i--) { d[i]=s%10; s/=10; }
    for (i=0;i<5;i++) { tile=DIGIT_TILE_BASE+d[i]; SMS_loadTileMap(22+i,0,&tile,2); }
    score_changed=0;
}

static void draw_score_label(void) {
    unsigned int tile; int i;
    for (i=16;i<22;i++) { tile=TILE_SKY+(i&3); SMS_loadTileMap(i,0,&tile,2); }
}

static void draw_background(void) {
    int x; unsigned int tile;
    for (x=0;x<SCREEN_W;x++) {
        tile=TILE_SKY+(x&3);     SMS_loadTileMap(x,0,&tile,2);
        tile=TILE_SKY+4+(x&3);   SMS_loadTileMap(x,1,&tile,2);
        write_floor_column(x, FLOOR_BASE_ROW);
    }
}

/* ============================================================
   Enemy management
   ============================================================ */
static void init_enemies(void) {
    int i;
    for (i=0;i<MAX_ENEMIES;i++) enemies[i].active=0;
    enemy_spawn_timer=90; enemy_spawn_interval=120;
}

static void init_bullets(void) {
    int i;
    for (i=0;i<MAX_BULLETS;i++) bullets[i].active=0;
    shoot_cooldown=0;
}

static void spawn_enemy(void) {
    int i, type, ground_py;
    unsigned int r = rng_next();
    if      (score < 20)  type = ETYPE_TURTLE;
    else if (score < 50)  type = (r&1) ? ETYPE_SPARK : ETYPE_TURTLE;
    else if (score < 100) type = (r%3==0) ? ETYPE_BEE : (r&1) ? ETYPE_SPARK : ETYPE_TURTLE;
    else { int t=r&3; type=(t==0)?ETYPE_HORNET:(t==1)?ETYPE_BEE:(t==2)?ETYPE_SPARK:ETYPE_TURTLE; }

    for (i=0;i<MAX_ENEMIES;i++) {
        if (!enemies[i].active) {
            enemies[i].active=1; enemies[i].type=type;
            enemies[i].x=SCREEN_PX_W+8; enemies[i].frame=0; enemies[i].anim_timer=0;
            ground_py = get_ground_y_at(SCREEN_PX_W);
            switch (type) {
                case ETYPE_TURTLE: enemies[i].y=(ground_py>SCREEN_PX_H)?FLOOR_BASE_ROW*8-24:ground_py-24; break;
                case ETYPE_SPARK:  enemies[i].y=(ground_py>SCREEN_PX_H)?FLOOR_BASE_ROW*8-16:ground_py-16; break;
                case ETYPE_BEE:    enemies[i].y=FLOOR_BASE_ROW*8-60; break;
                case ETYPE_HORNET: enemies[i].y=FLOOR_BASE_ROW*8-90; break;
            }
            return;
        }
    }
}

static void update_enemies(void) {
    int i;
    enemy_spawn_timer--;
    if (enemy_spawn_timer<=0) {
        spawn_enemy(); enemy_spawn_timer=enemy_spawn_interval;
        if (enemy_spawn_interval>50) enemy_spawn_interval-=3;
    }
    for (i=0;i<MAX_ENEMIES;i++) {
        if (!enemies[i].active) continue;
        enemies[i].x-=scroll_speed;
        if (++enemies[i].anim_timer>=8) { enemies[i].anim_timer=0; enemies[i].frame^=1; }
        if (enemies[i].type==ETYPE_TURTLE||enemies[i].type==ETYPE_SPARK) {
            int gpy=get_ground_y_at(enemies[i].x+8);
            if (gpy>SCREEN_PX_H) { enemies[i].active=0; continue; }
            enemies[i].y=gpy-(enemies[i].type==ETYPE_TURTLE?24:16);
        }
        if (enemies[i].x<-32) enemies[i].active=0;
    }
}

/* ============================================================
   Bullet management
   ============================================================ */
static void shoot(void) {
    int i;
    if (shoot_cooldown>0) return;
    for (i=0;i<MAX_BULLETS;i++) {
        if (!bullets[i].active) {
            bullets[i].active=1; bullets[i].x=JAZZ_SCREEN_X+JAZZ_PX_W;
            bullets[i].y=jazz_y+10; shoot_cooldown=12; return;
        }
    }
}

static void update_bullets(void) {
    int i;
    if (shoot_cooldown>0) shoot_cooldown--;
    for (i=0;i<MAX_BULLETS;i++) {
        if (!bullets[i].active) continue;
        bullets[i].x+=8;
        if (bullets[i].x>SCREEN_PX_W+8) bullets[i].active=0;
    }
}

/* ============================================================
   Collision
   ============================================================ */
static int check_collisions(void) {
    int i,b;
    int jx1=JAZZ_SCREEN_X+6, jy1=jazz_y+8, jx2=JAZZ_SCREEN_X+26, jy2=jazz_y+28;
    for (i=0;i<MAX_ENEMIES;i++) {
        int ex1,ey1,ex2,ey2,ew,eh;
        if (!enemies[i].active) continue;
        switch(enemies[i].type){case ETYPE_TURTLE:ew=28;eh=20;break;default:ew=12;eh=12;break;}
        ex1=enemies[i].x+2; ey1=enemies[i].y+2; ex2=enemies[i].x+2+ew; ey2=enemies[i].y+2+eh;
        for (b=0;b<MAX_BULLETS;b++) {
            if (!bullets[b].active) continue;
            if (bullets[b].x+8>ex1&&bullets[b].x<ex2&&bullets[b].y+8>ey1&&bullets[b].y<ey2) {
                bullets[b].active=0; enemies[i].active=0; score+=5; score_changed=1; goto nxt;
            }
        }
        if (jazz_vy>0&&(enemies[i].type==ETYPE_TURTLE||enemies[i].type==ETYPE_SPARK)&&
            jx1<ex2&&jx2>ex1&&jy2>=ey1-4&&jy2<=ey2) {
            enemies[i].active=0; jazz_vy=JUMP_VELOCITY/2; on_ground=0; jumps_left=1;
            score+=10; score_changed=1; goto nxt;
        }
        if (jx1<ex2&&jx2>ex1&&jy1<ey2&&jy2>ey1) return 1;
        nxt:;
    }
    return 0;
}

/* ============================================================
   Draw sprites
   ============================================================ */
static void draw_sprites(void) {
    int i,tx,ty,base_tile;
    SMS_initSprites();
    if (!dead) {
        base_tile=on_ground?SPR_TILE_BASE+anim_frame*20:SPR_TILE_BASE+4*20;
        for(ty=0;ty<JAZZ_TILES_H;ty++) for(tx=0;tx<JAZZ_TILES_W;tx++)
            SMS_addSprite(JAZZ_SCREEN_X+tx*8,jazz_y+ty*8,base_tile+ty*JAZZ_TILES_W+tx);
    }
    for(i=0;i<MAX_BULLETS;i++)
        if(bullets[i].active) SMS_addSprite(bullets[i].x,bullets[i].y,SPR_BULLET_BASE);
    for(i=0;i<MAX_ENEMIES;i++) {
        int ex,ey,f,tb;
        if(!enemies[i].active||enemies[i].x<-32||enemies[i].x>SCREEN_PX_W+8) continue;
        ex=enemies[i].x; ey=enemies[i].y; f=enemies[i].frame;
        switch(enemies[i].type) {
            case ETYPE_TURTLE: tb=SPR_TURTLE_BASE+f*12;
                for(ty=0;ty<3;ty++) for(tx=0;tx<4;tx++) SMS_addSprite(ex+tx*8,ey+ty*8,tb+ty*4+tx); break;
            case ETYPE_SPARK:  tb=SPR_SPARK_BASE+f*4;
                SMS_addSprite(ex,ey,tb); SMS_addSprite(ex+8,ey,tb+1);
                SMS_addSprite(ex,ey+8,tb+2); SMS_addSprite(ex+8,ey+8,tb+3); break;
            case ETYPE_BEE:    tb=SPR_BEE_BASE+f*4;
                SMS_addSprite(ex,ey,tb); SMS_addSprite(ex+8,ey,tb+1);
                SMS_addSprite(ex,ey+8,tb+2); SMS_addSprite(ex+8,ey+8,tb+3); break;
            case ETYPE_HORNET: tb=SPR_HORNET_BASE+f*4;
                SMS_addSprite(ex,ey,tb); SMS_addSprite(ex+8,ey,tb+1);
                SMS_addSprite(ex,ey+8,tb+2); SMS_addSprite(ex+8,ey+8,tb+3); break;
        }
    }
    SMS_copySpritestoSAT();
}

/* ============================================================
   Start gameplay (called from title and after death)
   ============================================================ */
static void start_game(void) {
    game_state = STATE_PLAYING;
    /* Restore gameplay tiles and palette */
    SMS_loadBGPalette(bg_palette);
    SMS_loadSpritePalette(sprite_palette);
    load_gameplay_tiles();
    SMS_VDPturnOnFeature(VDPFEATURE_LOCKHSCROLL);

    init_floor();
    jazz_y=FLOOR_BASE_ROW*8-30; jazz_vy=0; on_ground=1; jumps_left=2;
    anim_frame=0; anim_timer=0;
    scroll_x=0; scroll_speed=INITIAL_SPEED; frame_count=0;
    score=0; score_changed=1; dead=0; dead_timer=0;
    init_enemies(); init_bullets();
    draw_background();
    draw_score();
    draw_score_label();
}

/* ============================================================
   Main
   ============================================================ */
void main(void) {
    unsigned int keys, keys_pressed, prev_keys = 0;

    SMS_init();
    SMS_displayOff();
    SMS_useFirstHalfTilesforSprites(1);
    SMS_setSpriteMode(SPRITEMODE_NORMAL);
    SMS_setBackdropColor(0);
    SMS_VDPturnOnFeature(VDPFEATURE_FRAMEIRQ);

    /* Seed RNG with a non-zero value */
    rng_state = 0xACE1u;

    /* Show title screen */
    game_state = STATE_TITLE;
    show_title_screen();
    SMS_initSprites();
    SMS_copySpritestoSAT();
    SMS_displayOn();

    while (1) {
        SMS_waitForVBlank();

        /* Always advance RNG so time spent on title seeds the level */
        rng_next();

        keys         = SMS_getKeysStatus();
        keys_pressed = keys & ~prev_keys;
        prev_keys    = keys;

        /* ---- Title screen ---- */
        if (game_state == STATE_TITLE) {
            if (keys_pressed & (PORT_A_KEY_1 | PORT_A_KEY_2 | PORT_A_KEY_UP)) {
                start_game();
            }
            continue;
        }

        /* ---- Dead ---- */
        if (dead) {
            dead_timer++;
            draw_sprites();
            if (dead_timer > 90 &&
                (keys_pressed & (PORT_A_KEY_1|PORT_A_KEY_2|PORT_A_KEY_UP))) {
                /* Return to title — new seed from current rng_state */
                game_state = STATE_TITLE;
                SMS_VDPturnOffFeature(VDPFEATURE_LOCKHSCROLL);
                show_title_screen();
                SMS_initSprites(); SMS_copySpritestoSAT();
            }
            continue;
        }

        /* ---- Input ---- */
        if (keys_pressed & (PORT_A_KEY_1 | PORT_A_KEY_UP)) {
            if (on_ground) { jazz_vy=JUMP_VELOCITY; on_ground=0; jumps_left=1; }
            else if (jumps_left>0) { jazz_vy=JUMP2_VELOCITY; jumps_left=0; }
        }
        if (keys_pressed & PORT_A_KEY_2) shoot();

        /* ---- Scroll ---- */
        scroll_x=(scroll_x+scroll_speed)&0xFF;
        SMS_setBGScrollX(256-scroll_x);

        /* ---- Floor columns ---- */
        pixels_since_last_col+=scroll_speed;
        while (pixels_since_last_col>=8) {
            int delta, new_h;
            pixels_since_last_col-=8;
            delta = next_floor_delta();
            if (delta==9) {
                write_floor_column(next_floor_col&31, FLOOR_PIT);
            } else {
                new_h=floor_cur_height+delta;
                if (new_h<FLOOR_MIN_ROW) new_h=FLOOR_MIN_ROW;
                if (new_h>FLOOR_MAX_ROW) new_h=FLOOR_MAX_ROW;
                floor_cur_height=new_h;
                write_floor_column(next_floor_col&31, floor_cur_height);
            }
            next_floor_col=(next_floor_col+1)&31;
        }

        /* ---- Jazz physics ---- */
        {
            int gpy=get_ground_y_at(JAZZ_SCREEN_X+16)-30;
            jazz_vy+=GRAVITY; jazz_y+=jazz_vy;
            if (jazz_y>=gpy&&jazz_vy>=0) { jazz_y=gpy; jazz_vy=0; on_ground=1; jumps_left=2; }
            else if (jazz_y<gpy) on_ground=0;
            if (jazz_y>SCREEN_PX_H) { dead=1; dead_timer=0; }
        }

        /* ---- Animation ---- */
        if (on_ground) { if(++anim_timer>=6){anim_timer=0;anim_frame=(anim_frame+1)&3;} }

        /* ---- Speed ramp ---- */
        if (++frame_count%300==0&&scroll_speed<MAX_SPEED) scroll_speed++;

        /* ---- Score ---- */
        if (frame_count%10==0) { score++; score_changed=1; }
        if (score_changed) draw_score();

        /* ---- Bullets & Enemies ---- */
        update_bullets();
        update_enemies();

        /* ---- Collision ---- */
        if (check_collisions()) { dead=1; dead_timer=0; }

        draw_sprites();
    }
}

void SMS_SEGASMS_pause(void) __critical __interrupt {}

SMS_EMBED_SEGA_ROM_HEADER(9999, 0);
SMS_EMBED_SDSC_HEADER_AUTO_DATE(1, 0,
    "EpicGames/Ported",
    "Jazz Jackrabbit Runner",
    "Jump, shoot, stomp! Unique level every play.");
