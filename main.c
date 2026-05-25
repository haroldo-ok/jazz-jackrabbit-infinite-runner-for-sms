/*
 * Jazz Jackrabbit - Infinite Runner
 * Sega Master System / devkitSMS
 *
 * Single-button (Button 1 / Start) to jump.
 * Auto-runs forever. Score increases with distance.
 * Avoid Turtle Goons!
 */

#include "SMSlib.h"
#include "palettes.h"
#include "tiles.h"

/* ============================================================
   Constants
   ============================================================ */
#define SCREEN_W        32   /* tiles */
#define SCREEN_H        24   /* tiles */
#define SCREEN_PX_W     256
#define SCREEN_PX_H     192

/* BG Tile VRAM indices — each 32x32 source = 4x4 = 16 SMS 8x8 tiles */
#define TILE_SKY          0   /* tiles 0-15  */
#define TILE_GROUND_TOP  16   /* tiles 16-31 */
#define TILE_GROUND_FILL 32   /* tiles 32-47 */
#define TILE_CLOUD       48   /* tiles 48-63 */
#define DIGIT_TILE_BASE  64   /* tiles 64-73 (digits 0-9) */

/* Sprite tile indices */
/* Jazz: 5 frames x 20 tiles (4x5) = tiles 80-179 */
/* Turtle: 4 frames x 12 tiles (4x3) = tiles 180-227 */
#define SPR_TILE_BASE    80
#define SPR_TURTLE_BASE  180

/* Jazz sprite position (fixed X on screen) */
#define JAZZ_SCREEN_X   32
#define JAZZ_PX_W       32   /* 4 tiles wide */
#define JAZZ_PX_H       40   /* 5 tiles tall */
#define JAZZ_TILES_W    4
#define JAZZ_TILES_H    5
#define GROUND_Y        144  /* pixel Y of ground surface */
#define JAZZ_GROUND_Y   (GROUND_Y - 30)  /* sprite content is 30px tall (tiles are 40px padded) */

/* Physics */
#define GRAVITY         1
#define JUMP_VELOCITY   (-16)

/* Ground layout (tiles) */
#define GROUND_TILE_ROW  18  /* tile row where ground top is */

/* Scroll speed (subpixel: actual speed = scroll_speed/4) */
#define INITIAL_SPEED    2
#define MAX_SPEED        6

/* Enemy limits */
#define MAX_ENEMIES      1

/* Score display (tile row 0) */
#define SCORE_ROW        0

/* ============================================================
   Data types
   ============================================================ */
typedef struct {
    int  x;          /* pixel X (world) - we use screen X directly since scrolling is BG */
    int  y;          /* pixel Y */
    int  active;
} Enemy;

/* ============================================================
   Global state
   ============================================================ */
static int jazz_y;          /* Jazz Y pixel position */
static int jazz_vy;         /* Jazz vertical velocity */
static int on_ground;       /* 1 = grounded */

static int anim_frame;      /* 0-3 = running, 4 = jump */
static int anim_timer;

static int turtle_frame;
static int turtle_anim_timer;

static int scroll_x;        /* background horizontal scroll (0-255) */
static int scroll_speed;    /* pixels per frame */
static int frame_count;

static unsigned int score;
static int score_changed;
static int dead;
static int dead_timer;

static Enemy enemies[MAX_ENEMIES];
static int enemy_spawn_timer;
static int enemy_spawn_interval;

/* Obstacle heights (platform gaps) - a simple flat runner for now */

/* ============================================================
   Number tiles: we'll use the SMS built-in font trick.
   Actually we need to write our own digits since SMS has no built-in font.
   We'll write score using BG tile writes.
   For simplicity, we encode digits as BG tiles 8-17 (10 digit tiles).
   ============================================================ */
/* 5x7 font for digits 0-9 stored as 8x8 tiles */
/* Each digit tile: index 0 = black bg, palette entries 10=white for fg */

/* Simple digit patterns: 8x8, white pixels = 1, rest = 0 */
static const unsigned char digit_bits[10][8] = {
    {0x7E,0x42,0x42,0x42,0x42,0x42,0x7E,0x00}, /* 0 */
    {0x08,0x18,0x08,0x08,0x08,0x08,0x1C,0x00}, /* 1 */
    {0x7E,0x02,0x02,0x7E,0x40,0x40,0x7E,0x00}, /* 2 */
    {0x7E,0x02,0x02,0x3E,0x02,0x02,0x7E,0x00}, /* 3 */
    {0x42,0x42,0x42,0x7E,0x02,0x02,0x02,0x00}, /* 4 */
    {0x7E,0x40,0x40,0x7E,0x02,0x02,0x7E,0x00}, /* 5 */
    {0x7E,0x40,0x40,0x7E,0x42,0x42,0x7E,0x00}, /* 6 */
    {0x7E,0x02,0x02,0x02,0x02,0x02,0x02,0x00}, /* 7 */
    {0x7E,0x42,0x42,0x7E,0x42,0x42,0x7E,0x00}, /* 8 */
    {0x7E,0x42,0x42,0x7E,0x02,0x02,0x7E,0x00}, /* 9 */
};

/* ============================================================
   Load digit tiles into VRAM (indices 64-73)
   color 0 = bg index 0 (sky), color 2 = white (index 2 in BG palette)
   ============================================================ */
static void load_digit_tiles(void) {
    /* Digit tiles: VRAM 64-73 */
    unsigned char tile_data[32];
    int d, row;
    for (d = 0; d < 10; d++) {
        for (row = 0; row < 8; row++) {
            unsigned char bits = digit_bits[d][row];
            /* white = BG palette index 2 = 0b0010: plane0=0, plane1=1, plane2=0, plane3=0 */
            unsigned char bp0 = 0;
            unsigned char bp1 = bits;   /* bit 1 of index = 1 for white */
            unsigned char bp2 = 0;
            unsigned char bp3 = 0;
            tile_data[row*4+0] = bp0;
            tile_data[row*4+1] = bp1;
            tile_data[row*4+2] = bp2;
            tile_data[row*4+3] = bp3;
        }
        SMS_loadTiles(tile_data, DIGIT_TILE_BASE + d, 32);
    }
}

/* ============================================================
   Load all tiles into VRAM
   ============================================================ */
static void load_all_tiles(void) {
    /* BG tiles: 0-63 (4 source tiles x 16 SMS tiles each) */
    SMS_loadTiles(bg_tiles, 0, sizeof(bg_tiles));

    /* Digit tiles: 64-73 */
    load_digit_tiles();

    /* Jazz sprite tiles: VRAM tiles 80-179 (5 frames x 20 tiles) */
    SMS_loadTiles(jazz_tiles, SPR_TILE_BASE, sizeof(jazz_tiles));

    /* Turtle sprite tiles: VRAM tiles 180-227 (4 frames x 12 tiles) */
    SMS_loadTiles(turtle_tiles, SPR_TURTLE_BASE, sizeof(turtle_tiles));
}

/* ============================================================
   Draw the background tilemap.
   Each original 32x32 tile = 4x4 block of SMS 8x8 tiles.
   Sub-tile index within a source tile: tile_base + (sub_row*4 + sub_col)
   Screen is 32 cols x 24 rows of 8x8 tiles = 256x192px.
   Ground surface at tile row 18.
   ============================================================ */
static void draw_background(void) {
    int x, y;
    unsigned int tile;
    int sub_row, sub_col;

    for (y = 0; y < SCREEN_H; y++) {
        sub_row = y & 3;  /* which row within the 4x4 block (0-3) */
        for (x = 0; x < SCREEN_W; x++) {
            sub_col = x & 3;  /* which col within the 4x4 block (0-3) */

            if (y >= GROUND_TILE_ROW + 1) {
                tile = TILE_GROUND_FILL + sub_row * 4 + sub_col;
            } else if (y == GROUND_TILE_ROW) {
                tile = TILE_GROUND_TOP + sub_row * 4 + sub_col;
            } else if (y == 2 && (x >= 4 && x <= 7)) {
                /* Cloud block at cols 4-7, row 2 */
                tile = TILE_CLOUD + sub_row * 4 + sub_col;
            } else if (y == 3 && (x >= 4 && x <= 7)) {
                tile = TILE_CLOUD + sub_row * 4 + sub_col;
            } else if (y == 4 && (x >= 20 && x <= 23)) {
                tile = TILE_CLOUD + sub_row * 4 + sub_col;
            } else if (y == 5 && (x >= 20 && x <= 23)) {
                tile = TILE_CLOUD + sub_row * 4 + sub_col;
            } else {
                tile = TILE_SKY + sub_row * 4 + sub_col;
            }
            SMS_loadTileMap(x, y, &tile, 2);
        }
    }
}

/* ============================================================
   Score display: write 5-digit score on row 0, cols 22-26
   ============================================================ */
static void draw_score(void) {
    unsigned int s = score;
    int i;
    unsigned int digits[5];
    unsigned int tile;

    for (i = 4; i >= 0; i--) {
        digits[i] = s % 10;
        s /= 10;
    }

    for (i = 0; i < 5; i++) {
        tile = DIGIT_TILE_BASE + digits[i];
        SMS_loadTileMap(22 + i, 0, &tile, 2);
    }
    score_changed = 0;
}

/* ============================================================
   Draw "SCORE" label using tile indices for S,C,O,R,E
   We'll reuse digit tiles creatively or just skip the label.
   ============================================================ */
static void draw_score_label(void) {
    unsigned int tile;
    int i;
    for (i = 16; i < 22; i++) {
        tile = TILE_SKY + (0 * 4) + (i & 3);  /* sky sub-tile for this column */
        SMS_loadTileMap(i, 0, &tile, 2);
    }
}

/* ============================================================
   Enemy management
   ============================================================ */
static void init_enemies(void) {
    int i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].active = 0;
    }
    enemy_spawn_timer = 120;
    enemy_spawn_interval = 120;
}

static void spawn_enemy(void) {
    int i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            enemies[i].x = SCREEN_PX_W + 8;   /* just off right edge */
            enemies[i].y = GROUND_Y - 24;   /* turtle is 24px tall */
            enemies[i].active = 1;
            return;
        }
    }
}

static void update_enemies(void) {
    int i;
    enemy_spawn_timer--;
    if (enemy_spawn_timer <= 0) {
        spawn_enemy();
        enemy_spawn_timer = enemy_spawn_interval;
        /* Speed up spawning over time */
        if (enemy_spawn_interval > 60) {
            enemy_spawn_interval -= 5;
        }
    }

    for (i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            enemies[i].x -= scroll_speed;
            if (enemies[i].x < -16) {
                enemies[i].active = 0;
            }
        }
    }
}

/* ============================================================
   Collision detection (AABB, simplified)
   Jazz box: (JAZZ_SCREEN_X, jazz_y) to (+14, +14)
   ============================================================ */
static int check_collision(void) {
    int i;
    /* Jazz hitbox: inner 24x32 region (leaving 8px margin on sides/top) */
    int jx1 = JAZZ_SCREEN_X + 8;
    int jy1 = jazz_y + 16;
    int jx2 = JAZZ_SCREEN_X + 32;
    int jy2 = jazz_y + 46;

    for (i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            int ex1 = enemies[i].x + 2;
            int ey1 = enemies[i].y + 2;
            int ex2 = enemies[i].x + 13;
            int ey2 = enemies[i].y + 13;
            if (jx1 < ex2 && jx2 > ex1 && jy1 < ey2 && jy2 > ey1) {
                return 1;
            }
        }
    }
    return 0;
}

/* ============================================================
   Draw sprites (Jazz + enemies) using SMS sprite SAT
   ============================================================ */
static void draw_sprites(void) {
    int i, tx, ty;
    int base_tile;

    SMS_initSprites();

    /* Jazz: 4x5 metasprite (20 hardware sprites) */
    if (!dead) {
        if (on_ground) {
            base_tile = SPR_TILE_BASE + (anim_frame * 20);
        } else {
            base_tile = SPR_TILE_BASE + (4 * 20); /* jump frame */
        }
        for (ty = 0; ty < JAZZ_TILES_H; ty++) {
            for (tx = 0; tx < JAZZ_TILES_W; tx++) {
                SMS_addSprite(
                    JAZZ_SCREEN_X + tx * 8,
                    jazz_y       + ty * 8,
                    base_tile + ty * JAZZ_TILES_W + tx
                );
            }
        }
    }

    /* Turtle enemies: 4x3 metasprite (12 hardware sprites) */
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && enemies[i].x > -32 && enemies[i].x < SCREEN_PX_W + 8) {
            int t_base = SPR_TURTLE_BASE + turtle_frame * 12;
            for (ty = 0; ty < 3; ty++) {
                for (tx = 0; tx < 4; tx++) {
                    SMS_addSprite(
                        enemies[i].x + tx * 8,
                        enemies[i].y + ty * 8,
                        t_base + ty * 4 + tx
                    );
                }
            }
        }
    }

    SMS_copySpritestoSAT();
}

/* ============================================================
   Game reset
   ============================================================ */
static void reset_game(void) {
    jazz_y   = JAZZ_GROUND_Y;
    jazz_vy  = 0;
    on_ground = 1;
    anim_frame = 0;
    anim_timer = 0;
    turtle_frame = 0;
    turtle_anim_timer = 0;
    scroll_x = 0;
    scroll_speed = INITIAL_SPEED;
    frame_count = 0;
    score = 0;
    score_changed = 1;
    dead = 0;
    dead_timer = 0;
    init_enemies();
    draw_background();
    draw_score();
    draw_score_label();
}

/* ============================================================
   Main game loop
   ============================================================ */
void main(void) {
    unsigned int keys, keys_pressed;
    unsigned int prev_keys = 0;

    SMS_init();
    SMS_displayOff();
    SMS_useFirstHalfTilesforSprites(1);  /* sprite tiles are in first VRAM half (tiles 0-255) */
    SMS_setSpriteMode(SPRITEMODE_NORMAL);

    /* Load palettes */
    SMS_loadBGPalette(bg_palette);
    SMS_loadSpritePalette(sprite_palette);

    /* Set backdrop = sky color (palette entry 0) */
    SMS_setBackdropColor(0);

    /* Load tiles */
    load_all_tiles();

    /* Initial game state */
    reset_game();

    SMS_VDPturnOnFeature(VDPFEATURE_FRAMEIRQ);
    SMS_VDPturnOnFeature(VDPFEATURE_LOCKHSCROLL);
    SMS_displayOn();

    while (1) {
        SMS_waitForVBlank();

        keys = SMS_getKeysStatus();
        keys_pressed = keys & ~prev_keys;
        prev_keys = keys;

        if (dead) {
            dead_timer++;
            /* Flash effect - draw sprites anyway */
            draw_sprites();

            if (dead_timer > 120) {
                if (keys_pressed & (PORT_A_KEY_1 | PORT_A_KEY_2 | PORT_A_KEY_UP)) {
                    reset_game();
                }
            }
            continue;
        }

        /* ---- Input ---- */
        int jump_pressed = keys_pressed & (PORT_A_KEY_1 | PORT_A_KEY_2 | PORT_A_KEY_UP);

        /* ---- Jazz physics ---- */
        if (jump_pressed && on_ground) {
            jazz_vy = JUMP_VELOCITY;
            on_ground = 0;
        }

        if (!on_ground) {
            jazz_vy += GRAVITY;
            jazz_y  += jazz_vy;
            if (jazz_y >= JAZZ_GROUND_Y) {
                jazz_y  = JAZZ_GROUND_Y;
                jazz_vy = 0;
                on_ground = 1;
            }
        }

        /* ---- Animation ---- */
        if (on_ground) {
            anim_timer++;
            if (anim_timer >= 6) {
                anim_timer = 0;
                anim_frame = (anim_frame + 1) & 3; /* 0-3 running cycle */
            }
        }

        /* ---- Scrolling ---- */
        scroll_x = (scroll_x + scroll_speed) & 0xFF;
        SMS_setBGScrollX(256 - scroll_x);  /* Scroll right to left */

        /* ---- Speed ramp ---- */
        frame_count++;
        if (frame_count % 300 == 0 && scroll_speed < MAX_SPEED) {
            scroll_speed++;
        }

        /* ---- Score ---- */
        if (frame_count % 10 == 0) {
            score++;
            score_changed = 1;
        }
        if (score_changed) {
            draw_score();
        }

        /* ---- Enemies ---- */
        update_enemies();

        /* ---- Turtle animation ---- */
        turtle_anim_timer++;
        if (turtle_anim_timer >= 8) {
            turtle_anim_timer = 0;
            turtle_frame = (turtle_frame + 1) & 3;
        }

        /* ---- Collision ---- */
        if (check_collision()) {
            dead = 1;
            dead_timer = 0;
        }

        /* ---- Draw ---- */
        draw_sprites();
    }
}

/* Pause button handler (required by devkitSMS) */
void SMS_SEGASMS_pause(void) __critical __interrupt {
    /* Do nothing */
}

/* Embed SEGA header so ROM is bootable on real hardware and emulators */
SMS_EMBED_SEGA_ROM_HEADER(9999, 0);
SMS_EMBED_SDSC_HEADER_AUTO_DATE(1, 0,
    "EpicGames/Ported",
    "Jazz Jackrabbit Runner",
    "Single-button infinite runner based on Jazz Jackrabbit (1994)");
