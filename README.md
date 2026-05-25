Here's a full summary of what we built together:

**Project: Jazz Jackrabbit Infinite Runner for Sega Master System**

A single-button endless runner ROM built from scratch using devkitSMS (SDCC C compiler + SMSlib), compiled to a valid 32KB `.sms` file with a proper `TMR SEGA` header and checksum --- bootable on real hardware and emulators.

* * * * *

**Toolchain**

-   Installed `sdcc` (Small Device C Compiler with Z80 backend)
-   Cloned `devkitSMS` (sverx's SMS/GG C library) from GitHub
-   Used `ihx2sms` to convert the SDCC linker output to a valid SMS ROM

* * * * *

**Asset extraction pipeline** (Python/Pillow)

All graphics were extracted directly from the uploaded Jazz Jackrabbit (1994, DOS) sprite sheets and tileset:

-   **Jazz sprites**: Scanned the sprite sheet with density analysis to find actual pixel-gap separators (no fixed grid), identified the correct animation rows visually, and extracted tight per-sprite bounds. Row 2 (y=97--126) is the running animation (Jazz leaning forward with motion-blur ear streaks). Row 4 col 0 is the jump frame. Sprites are 4×5 SMS tiles (32×40px), padded right/bottom only to avoid bleeding from adjacent cells.

-   **Turtle Goon sprites**: Similarly scanned the enemies sheet using both background colors (the sheet has two --- dark and light blue-grey). Found 4 walk frames in columns x=36--100, 107--172, 178--242, 250--305, all at y=36--79. Extended right edge to x=100 to capture the beak. Flipped horizontally so the turtle faces left toward Jazz. Scaled proportionally to 32×24px (4×3 SMS tiles).

-   **Diamondus tileset**: Identified 4 key 32×32 source tiles --- night sky with stars (row 13 col 0), green neon-pillar ground top (row 1 col 4), dark purple speckled ground fill (row 5 col 0), and cloud (row 0 col 1). Each 32×32 tile was split into a 4×4 grid of 8×8 SMS sub-tiles. The BG palette (16 colors) was derived from the 23 unique colors actually present in those tiles.

-   **Color quantization**: A combined 16-color sprite palette covers both Jazz (greens, red beret, tan skin, blue gun) and the turtle (purple shell, yellow trim, grey pads). Separate BG palette for tileset graphics.

* * * * *

**SMS hardware constraints navigated**

-   **VRAM tile layout**: BG tiles 0--63, digit tiles 64--73, Jazz tiles 80--179 (5 frames × 20 tiles), turtle tiles 180--227 (4 frames × 12 tiles). All within the 0--255 first-half range.
-   **Sprite half selection**: `SMS_useFirstHalfTilesforSprites(1)` --- sprites must use the same VRAM half where tiles are loaded.
-   **Hardware sprite limit**: 64 sprites per frame, 8 per scanline. Jazz uses 20 (4×5 metasprite), turtle uses 12 (4×3 metasprite) --- 32 total, comfortably under the limit.
-   **VDP feature groups**: `VDPFEATURE_LOCKHSCROLL` (group 0) and `VDPFEATURE_FRAMEIRQ` (group 1) must be set with separate calls --- OR-ing them corrupts both writes.
-   **Fixed top rows**: `VDPFEATURE_LOCKHSCROLL` locks the top 16px (2 tile rows) from horizontal scrolling, keeping the score display stationary.

* * * * *

**Game mechanics**

-   Infinite horizontal BG scroll with `SMS_setBGScrollX`, speed ramping every 300 frames up to a maximum
-   Jazz runs automatically; **Button 1/2/Up** to jump (variable gravity physics)
-   4-frame running animation (6 ticks/frame), separate jump pose
-   Turtle Goon enemy with 4-frame walk animation (8 ticks/frame), spawns from the right at increasing frequency
-   AABB collision detection
-   Score counter (increments every 10 frames), displayed as 5 digits in the fixed top-row HUD
-   Death + restart flow