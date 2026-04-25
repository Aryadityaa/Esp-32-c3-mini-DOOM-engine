// ╔══════════════════════════════════════════════════════════════════╗
// ║   ESP32-C3 MINI  ·  DOOM-STYLE RAYCASTER  ·  SH1106 128×64       ║
// ║   Controls:                                                      ║
// ║     UP         = Move Forward                                    ║
// ║     DOWN       = Move Back                                       ║
// ║     LEFT       = Turn Left                                       ║
// ║     RIGHT      = Turn Right                                      ║
// ║     UP+LEFT    = Strafe Left                                     ║
// ║     UP+RIGHT   = Strafe Right                                    ║
// ║     LEFT+RIGHT = SHOOT                                           ║
// ║     UP+DOWN    = PAUSE                                           ║
// ╚══════════════════════════════════════════════════════════════════╝

#include <U8g2lib.h>
#include <Wire.h>
#include <math.h>

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 6, 5);

// ──────────────────────────────────────────────────────────────────
// PINS
// ──────────────────────────────────────────────────────────────────
#define BTN_UP    2
#define BTN_DOWN  3
#define BTN_LEFT  4
#define BTN_RIGHT 10

// ──────────────────────────────────────────────────────────────────
// DISPLAY / VIEW CONSTANTS
// ──────────────────────────────────────────────────────────────────
#define SCREEN_W    128
#define SCREEN_H     64
#define VIEW_W      112    // raycast viewport width (rest = minimap strip)
#define VIEW_H       52    // raycast viewport height (rest = HUD)
#define VIEW_HALF    26    // VIEW_H / 2
#define HUD_Y        52    // HUD bar top
#define MM_X        114    // minimap origin X
#define MM_Y          0    // minimap origin Y
#define MM_CELL       2    // pixels per minimap cell
#define MM_RADIUS     3    // cells shown around player

// ──────────────────────────────────────────────────────────────────
// GAME CONSTANTS
// ──────────────────────────────────────────────────────────────────
#define FOV_FACTOR   0.66f
#define MOVE_SPEED   0.055f
#define ROT_SPEED    0.055f
#define STRAFE_SPD   0.040f
#define SHOOT_COOLDOWN_MS 300

// ──────────────────────────────────────────────────────────────────
// MAP  (0=open  1-4=wall types)
// ──────────────────────────────────────────────────────────────────
#define MAP_W 16
#define MAP_H 16

const uint8_t WORLD[MAP_H][MAP_W] PROGMEM = {
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1},
  {1,0,0,2,2,0,0,0,0,0,2,2,0,0,0,1},
  {1,0,0,2,0,0,0,0,0,0,0,2,0,0,0,1},
  {1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1},
  {1,1,0,1,1,1,1,1,1,1,1,1,0,1,1,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,3,3,0,0,0,0,0,0,0,0,3,3,0,1},
  {1,0,3,0,0,0,0,0,0,0,0,0,0,3,0,1},
  {1,0,0,0,0,4,4,0,0,4,4,0,0,0,0,1},
  {1,0,0,0,0,4,0,0,0,0,4,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

inline uint8_t mapAt(int x, int y) {
  if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return 1;
  return pgm_read_byte(&WORLD[y][x]);
}

// ──────────────────────────────────────────────────────────────────
// ENEMY BITMAPS  — 8×8 pixel-art demon sprites, stored in PROGMEM
// Frame 0: stand/walk-A   Frame 1: walk-B   Frame 2: dying
// Each row = 1 byte, MSB = leftmost pixel
// ──────────────────────────────────────────────────────────────────
// Frame 0: demon facing forward, arms out
//  .#.##.#.
//  .#.##.#.   <- horns
//  ##.##.##   <- head wide
//  .##..##.   <- face
//  ##.##.##   <- shoulders/arms
//  .######.   <- body
//  .##..##.   <- legs upper
//  ##....##   <- feet
const uint8_t ENEMY_F0[8] PROGMEM = {
  0b01011010,  // .#.##.#.  horns
  0b01011010,  // .#.##.#.
  0b11011011,  // ##.##.##  head
  0b01100110,  // .##..##.  face / eyes dark
  0b11111111,  // ########  arms wide
  0b01111110,  // .######.  body
  0b01100110,  // .##..##.  legs
  0b11000011,  // ##....##  feet
};

// Frame 1: demon, arms raised higher (walking alt)
const uint8_t ENEMY_F1[8] PROGMEM = {
  0b01011010,  // horns
  0b01011010,
  0b11011011,  // head
  0b01100110,  // face
  0b11011011,  // arms higher (inward)
  0b01111110,  // body
  0b01100110,  // legs
  0b11000011,  // feet
};

// Frame 2: dying — collapsed heap
const uint8_t ENEMY_DIE[8] PROGMEM = {
  0b00000000,
  0b00000000,
  0b00000000,
  0b00111100,  // .####.
  0b01111110,  // .######
  0b11111111,  // ########
  0b11111111,  // ########  splat on ground
  0b01111110,
};

// ──────────────────────────────────────────────────────────────────
// Z-BUFFER  (declared here so drawEnemySprite can use it)
// ──────────────────────────────────────────────────────────────────
float zBuffer[VIEW_W];

// ──────────────────────────────────────────────────────────────────
// ENEMY SPRITE RENDERER
// Scales the 8×8 bitmap to the on-screen size, then:
//   1. Erases background rectangle (black) so it pops on any wall
//   2. Draws the scaled bitmap pixels
// ──────────────────────────────────────────────────────────────────
void drawEnemySprite(int screenX, int spriteH, float dist,
                     int state, int frame) {
  if (spriteH < 2) return;

  // Cap sprite size
  if (spriteH > VIEW_H) spriteH = VIEW_H;

  // Bounding box on screen
  int halfW  = spriteH / 2;
  int startX = screenX - halfW;
  int startY = VIEW_HALF  - spriteH / 2;
  int endX   = startX + spriteH;   // width == height (square sprite)
  int endY   = startY + spriteH;

  // Full off-screen check
  if (endX <= 0 || startX >= VIEW_W) return;
  if (endY <= 0 || startY >= VIEW_H) return;

  // Pick bitmap
  const uint8_t* bmp;
  if      (state == 3)         bmp = ENEMY_DIE;
  else if (frame == 0)         bmp = ENEMY_F0;
  else                         bmp = ENEMY_F1;

  // Dithering threshold for far enemies
  // dist 0-3 = solid, 3-6 = 50%, 6+ = 25%
  int ditherMask = 0; // 0=solid, 1=every other, 3=every 4th
  if (dist > 6.0f) ditherMask = 3;
  else if (dist > 3.0f) ditherMask = 1;

  for (int sx = max(0, startX); sx < min(VIEW_W, endX); sx++) {
    // Z-buffer occlusion: skip column if wall is in front
    if (dist >= zBuffer[sx]) continue;  // zBuffer declared below — forward ref OK

    // Map screen column → bitmap column (0-7)
    int bmpX = (sx - startX) * 8 / spriteH;
    if (bmpX < 0) bmpX = 0;
    if (bmpX > 7) bmpX = 7;

    for (int sy = max(0, startY); sy < min(VIEW_H, endY); sy++) {
      // Map screen row → bitmap row (0-7)
      int bmpY = (sy - startY) * 8 / spriteH;
      if (bmpY < 0) bmpY = 0;
      if (bmpY > 7) bmpY = 7;

      // Read bitmap pixel (MSB = col 0)
      uint8_t row = pgm_read_byte(&bmp[bmpY]);
      bool bmpPixel = (row >> (7 - bmpX)) & 1;

      if (bmpPixel) {
        // Solid pixel: apply dither for distant enemies
        if (ditherMask == 0) {
          u8g2.drawPixel(sx, sy);
        } else if (ditherMask == 1) {
          if ((sx + sy) & 1) u8g2.drawPixel(sx, sy);
        } else {
          if (((sx + sy) & 3) == 0) u8g2.drawPixel(sx, sy);
        }
      } else {
        // Background pixel: clear it so sprite pops against wall/floor
        u8g2.setDrawColor(0);
        u8g2.drawPixel(sx, sy);
        u8g2.setDrawColor(1);
      }
    }
  }
}

// ──────────────────────────────────────────────────────────────────
// BUTTONS
// ──────────────────────────────────────────────────────────────────
struct Btn { int pin; bool last, pressed, held; };
Btn btns[4];

void initButtons() {
  int pins[] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT};
  for (int i = 0; i < 4; i++) {
    btns[i] = {pins[i], false, false, false};
    pinMode(pins[i], INPUT_PULLUP);
  }
}

void updateButtons() {
  for (int i = 0; i < 4; i++) {
    bool c = (digitalRead(btns[i].pin) == LOW);
    btns[i].pressed = c && !btns[i].last;
    btns[i].held    = c;
    btns[i].last    = c;
  }
}

#define UP_P    btns[0].pressed
#define DOWN_P  btns[1].pressed
#define LEFT_P  btns[2].pressed
#define RIGHT_P btns[3].pressed
#define UP_H    btns[0].held
#define DOWN_H  btns[1].held
#define LEFT_H  btns[2].held
#define RIGHT_H btns[3].held
#define ANY_P   (UP_P||DOWN_P||LEFT_P||RIGHT_P)
#define ANY_H   (UP_H||DOWN_H||LEFT_H||RIGHT_H)

void waitRelease() {
  delay(60);
  while (true) { updateButtons(); if (!ANY_H) break; delay(8); }
}

// ──────────────────────────────────────────────────────────────────
// PLAYER
// ──────────────────────────────────────────────────────────────────
struct Player {
  float x, y;    // world position
  float dx, dy;  // direction unit vector
  float cx, cy;  // camera plane (perpendicular, length = FOV_FACTOR)
  int   hp;
  int   ammo;
  int   kills;
  int   score;
};
Player player;

void playerInit() {
  player.x  = 1.5f; player.y  = 1.5f;
  player.dx = 1.0f; player.dy = 0.0f;
  player.cx = 0.0f; player.cy = FOV_FACTOR;
  player.hp = 100; player.ammo = 50;
  player.kills = 0; player.score = 0;
}

void playerRotate(float a) {
  float ca = cosf(a), sa = sinf(a);
  float ndx = player.dx*ca - player.dy*sa;
  float ndy = player.dx*sa + player.dy*ca;
  float ncx = player.cx*ca - player.cy*sa;
  float ncy = player.cx*sa + player.cy*ca;
  player.dx=ndx; player.dy=ndy; player.cx=ncx; player.cy=ncy;
}

void playerMove(float fwd, float strafe) {
  // Separate X/Y collision so player slides along walls
  float nx = player.x + player.dx * fwd - player.cy * strafe;
  float ny = player.y + player.dy * fwd + player.cx * strafe;
  float margin = 0.3f;
  if (mapAt((int)(nx + (nx > player.x ? margin : -margin)), (int)player.y) == 0) player.x = nx;
  if (mapAt((int)player.x, (int)(ny + (ny > player.y ? margin : -margin))) == 0) player.y = ny;
}

// ──────────────────────────────────────────────────────────────────
// ENEMIES
// ──────────────────────────────────────────────────────────────────
#define MAX_ENEMIES 6

struct Enemy {
  float x, y;
  int   hp;
  int   state;      // 0=idle 1=chase 2=attack 3=dying 4=dead
  int   frame;
  unsigned long lastAct;
};
Enemy enemies[MAX_ENEMIES];

void enemiesInit() {
  const float sx[] = {3.5f,12.5f, 7.5f, 4.5f,11.5f, 8.5f};
  const float sy[] = {9.5f, 9.5f, 5.5f,13.5f,13.5f, 2.5f};
  for (int i = 0; i < MAX_ENEMIES; i++) {
    enemies[i] = {sx[i], sy[i], 3, 0, 0, millis() + (unsigned long)i * 350};
  }
}

void updateEnemies() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy& e = enemies[i];
    if (e.state == 4) continue;

    if (e.state == 3) {          // dying
      if (now - e.lastAct > 700) { e.state = 4; }
      continue;
    }

    if (now - e.lastAct < 380) continue;
    e.lastAct = now;

    float ddx = player.x - e.x;
    float ddy = player.y - e.y;
    float dist = sqrtf(ddx*ddx + ddy*ddy);

    // Wake up
    if (e.state == 0 && dist < 8.0f) e.state = 1;

    if (e.state == 1) {    // chase
      e.frame ^= 1;
      if (dist < 0.9f) {
        e.state = 2;
      } else {
        float spd = 0.09f;
        float nx = e.x + (ddx/dist)*spd;
        float ny = e.y + (ddy/dist)*spd;
        if (mapAt((int)nx, (int)e.y) == 0) e.x = nx;
        if (mapAt((int)e.x, (int)ny) == 0) e.y = ny;
      }
    }

    if (e.state == 2) {    // attack
      player.hp -= 10;
      if (player.hp < 0) player.hp = 0;
      e.state = 1;
    }
  }
}

// ──────────────────────────────────────────────────────────────────
// SHOOTING
// ──────────────────────────────────────────────────────────────────
bool          shooting      = false;
int           shootFrame    = 0;
unsigned long shootTime     = 0;
unsigned long lastShot      = 0;
#define SHOOT_ANIM_MS 250

void shoot() {
  unsigned long now = millis();
  if (player.ammo <= 0)            return;
  if (now - lastShot < SHOOT_COOLDOWN_MS) return;

  player.ammo--;
  shooting   = true;
  shootFrame = 0;
  shootTime  = now;
  lastShot   = now;

  // Find closest enemy visible at screen center
  float bestDist  = 999.0f;
  int   bestIdx   = -1;
  float invDet = 1.0f / (player.cx*player.dy - player.dx*player.cy);

  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (enemies[i].hp <= 0 || enemies[i].state >= 3) continue;
    float ex = enemies[i].x - player.x;
    float ey = enemies[i].y - player.y;
    float tX  = invDet * ( player.dy*ex - player.dx*ey);
    float tY  = invDet * (-player.cy*ex + player.cx*ey);
    if (tY < 0.2f) continue;
    float sX  = (VIEW_W/2.0f) * (1.0f + tX/tY);
    // Tolerance scales with enemy screen size: bigger enemy = easier to hit
    int   sprH   = (int)(VIEW_H / tY);
    int   tol    = max(8, sprH / 2);
    if (sX < VIEW_W/2 - tol || sX > VIEW_W/2 + tol) continue;
    int   col = constrain((int)sX, 0, VIEW_W-1);
    if (tY > zBuffer[col]) continue;   // wall in the way
    if (tY < bestDist) { bestDist = tY; bestIdx = i; }
  }

  if (bestIdx >= 0) {
    Enemy& e = enemies[bestIdx];
    e.hp--;
    player.score += 50;
    if (e.hp <= 0) {
      e.state   = 3;
      e.frame   = 0;
      e.lastAct = millis();
      player.kills++;
      player.score += 200;
    } else {
      e.state = 1; // start chasing
    }
  }
}

// ──────────────────────────────────────────────────────────────────
// DRAW HELPERS
// ──────────────────────────────────────────────────────────────────
void drawCentered(const char* s, int y) {
  u8g2.drawStr((SCREEN_W - u8g2.getStrWidth(s)) / 2, y, s);
}

// ──────────────────────────────────────────────────────────────────
// WALL COLUMN RENDERER  — distance-based dithering shading
// wallType 1=solid  2=brick  3=grid  4=stripe
// side 0=NS(bright)  1=EW(darker)
// ──────────────────────────────────────────────────────────────────
void drawWallColumn(int col, int y0, int y1, float dist, int wt, int side) {
  float shade = dist * (side ? 1.5f : 1.0f);

  for (int y = y0; y <= y1; y++) {
    bool px = true;

    // Distance shading (4 levels)
    if      (shade > 7.0f)  px = ((col + y) % 4 == 0);
    else if (shade > 4.5f)  px = (((col ^ y) & 1) == 0);
    else if (shade > 2.5f)  px = !((col & 1) && (y & 1));
    else if (shade > 1.5f)  px = !((col % 4 == 0) && (y % 4 == 0));

    // Wall-type texture overlay
    if (px) {
      if      (wt == 2 && (y % 5 == 0))                    px = false; // brick mortar
      else if (wt == 3 && ((y%5==0) || (col%7==0)))         px = (shade < 3.0f);
      else if (wt == 4 && ((col + y) % 6 == 0))            px = false; // stripe
    }

    if (px) u8g2.drawPixel(col, y);
  }
}

// ──────────────────────────────────────────────────────────────────
// FLOOR / CEILING
// ──────────────────────────────────────────────────────────────────
void drawFloorCeiling() {
  // Ceiling — very sparse (dark)
  for (int y = 0; y < VIEW_HALF; y++) {
    int nearHorizon = VIEW_HALF - y;
    if      (nearHorizon <= 3) { for (int x=0;x<VIEW_W;x+=2) if((x+y)&1) u8g2.drawPixel(x,y); }
    else if (nearHorizon <= 8) { for (int x=(y&1);x<VIEW_W;x+=4) u8g2.drawPixel(x,y); }
    else                       { for (int x=0;x<VIEW_W;x+=8)  if(x%8==0) u8g2.drawPixel(x,y); }
  }
  // Floor — slightly denser (gray ground)
  for (int y = VIEW_HALF; y < VIEW_H; y++) {
    int nearHorizon = y - VIEW_HALF;
    if      (nearHorizon <= 3) { for (int x=(y&1);x<VIEW_W;x+=2) u8g2.drawPixel(x,y); }
    else if (nearHorizon <= 8) { for (int x=(y&1);x<VIEW_W;x+=3) u8g2.drawPixel(x,y); }
    else                       { for (int x=(y&1);x<VIEW_W;x+=5) u8g2.drawPixel(x,y); }
  }
}

// ──────────────────────────────────────────────────────────────────
// WEAPON (gun sprite, bottom center of screen)
// ──────────────────────────────────────────────────────────────────
void drawWeapon() {
  int wx = VIEW_W / 2;
  int wy = VIEW_H - 1;
  int bob = (int)(sinf(millis() * 0.006f) * 2.0f);

  if (shooting && shootFrame < 3) {
    // Muzzle flash — ring + dot
    u8g2.drawCircle(wx, wy - 16 + bob, 4);
    u8g2.drawDisc  (wx, wy - 16 + bob, 2);
    u8g2.setDrawColor(0);
    u8g2.drawPixel (wx, wy - 16 + bob);
    u8g2.setDrawColor(1);
  }

  // Barrel (3px wide, 7px tall)
  u8g2.drawBox(wx-1, wy-14+bob, 3, 7);
  // Sight notch on barrel top
  u8g2.setDrawColor(0);
  u8g2.drawPixel(wx, wy-14+bob);
  u8g2.setDrawColor(1);
  // Slide / body
  u8g2.drawBox(wx-3, wy-8+bob, 7, 5);
  // Trigger guard
  u8g2.drawFrame(wx-2, wy-4+bob, 5, 4);
  // Grip
  u8g2.drawBox(wx-1, wy-4+bob, 4, 4);
  // Detail cuts on slide
  u8g2.setDrawColor(0);
  u8g2.drawPixel(wx-2, wy-6+bob);
  u8g2.drawPixel(wx+2, wy-6+bob);
  u8g2.setDrawColor(1);
}

// ──────────────────────────────────────────────────────────────────
// HUD BAR
// ──────────────────────────────────────────────────────────────────
int   damageFlash = 0;
int   prevHP      = 100;

void drawHUD() {
  // Solid black bar at bottom
  u8g2.drawBox(0, HUD_Y, SCREEN_W, SCREEN_H - HUD_Y);
  u8g2.drawHLine(0, HUD_Y, SCREEN_W);
  u8g2.setDrawColor(0);

  u8g2.setFont(u8g2_font_4x6_tr);

  // ── Health ──
  u8g2.drawStr(1, HUD_Y + 6, "HP");
  int hpBarW = constrain((int)(player.hp * 28 / 100), 0, 28);
  u8g2.drawFrame(1, HUD_Y + 7, 30, 5);
  u8g2.drawBox  (2, HUD_Y + 8, hpBarW, 3);
  char buf[12];
  sprintf(buf, "%d", player.hp);
  u8g2.drawStr(33, HUD_Y + 12, buf);

  // ── Ammo ──
  u8g2.drawStr(50, HUD_Y + 6, "AMO");
  sprintf(buf, "%d", player.ammo);
  u8g2.drawStr(50, HUD_Y + 13, buf);

  // ── Score ──
  u8g2.drawStr(72, HUD_Y + 6, "SCR");
  sprintf(buf, "%d", player.score);
  u8g2.drawStr(72, HUD_Y + 13, buf);

  // ── Kills ──
  u8g2.drawStr(106, HUD_Y + 6, "KIL");
  sprintf(buf, "%d/%d", player.kills, MAX_ENEMIES);
  u8g2.drawStr(104, HUD_Y + 13, buf);

  u8g2.setDrawColor(1);

  // ── Crosshair ──
  int cx = VIEW_W/2, cy = VIEW_HALF;
  u8g2.drawPixel(cx,   cy);
  u8g2.drawPixel(cx-4, cy); u8g2.drawPixel(cx+4, cy);
  u8g2.drawPixel(cx,   cy-3); u8g2.drawPixel(cx, cy+3);
}

// ──────────────────────────────────────────────────────────────────
// MINIMAP  (top-right corner, 7×7 cells around player)
// ──────────────────────────────────────────────────────────────────
void drawMinimap() {
  int px = (int)player.x, py = (int)player.y;
  int cells = MM_RADIUS*2 + 1;
  int mw = cells * MM_CELL, mh = cells * MM_CELL;

  // Background fill (white = visible area)
  u8g2.drawBox(MM_X, MM_Y, mw+2, mh+2);
  u8g2.setDrawColor(0);

  // Draw visible cells
  for (int cy = -MM_RADIUS; cy <= MM_RADIUS; cy++) {
    for (int cx2 = -MM_RADIUS; cx2 <= MM_RADIUS; cx2++) {
      int wx = px + cx2, wy = py + cy;
      int sx = MM_X + 1 + (cx2 + MM_RADIUS) * MM_CELL;
      int sy = MM_Y + 1 + (cy  + MM_RADIUS) * MM_CELL;
      if (mapAt(wx, wy) != 0)
        u8g2.drawBox(sx, sy, MM_CELL, MM_CELL);  // wall = black
    }
  }

  // Enemy dots
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (enemies[i].state == 4) continue;
    int ex = (int)enemies[i].x - px;
    int ey = (int)enemies[i].y - py;
    if (abs(ex) > MM_RADIUS || abs(ey) > MM_RADIUS) continue;
    int sx = MM_X + 1 + (ex + MM_RADIUS)*MM_CELL + MM_CELL/2;
    int sy = MM_Y + 1 + (ey + MM_RADIUS)*MM_CELL + MM_CELL/2;
    u8g2.drawDisc(sx, sy, 1);
  }

  // Player arrow (center)
  int pdx = MM_X + 1 + MM_RADIUS*MM_CELL + MM_CELL/2;
  int pdy = MM_Y + 1 + MM_RADIUS*MM_CELL + MM_CELL/2;
  u8g2.drawDisc(pdx, pdy, 1);
  // Direction line
  int dex = pdx + (int)(player.dx * 3.5f);
  int dey = pdy + (int)(player.dy * 3.5f);
  dex = constrain(dex, MM_X+1, MM_X+mw);
  dey = constrain(dey, MM_Y+1, MM_Y+mh);
  u8g2.drawLine(pdx, pdy, dex, dey);

  u8g2.setDrawColor(1);
  u8g2.drawFrame(MM_X, MM_Y, mw+2, mh+2);
}

// ──────────────────────────────────────────────────────────────────
// SORT ENEMIES BY DISTANCE (furthest first, for painter's algorithm)
// ──────────────────────────────────────────────────────────────────
void sortByDist(int idx[], float d[], int n) {
  for (int i = 0; i < n-1; i++)
    for (int j = 0; j < n-1-i; j++)
      if (d[j] < d[j+1]) {
        float td=d[j]; d[j]=d[j+1]; d[j+1]=td;
        int   ti=idx[j]; idx[j]=idx[j+1]; idx[j+1]=ti;
      }
}

// ──────────────────────────────────────────────────────────────────
// MAIN RENDER FRAME  — DDA raycaster + sprites + HUD
// ──────────────────────────────────────────────────────────────────
void renderFrame() {
  u8g2.clearBuffer();

  // 1. Floor + ceiling
  drawFloorCeiling();

  // 2. Walls — DDA per column
  for (int col = 0; col < VIEW_W; col++) {
    float camX  = 2.0f * col / (float)VIEW_W - 1.0f;
    float rdx   = player.dx + player.cx * camX;
    float rdy   = player.dy + player.cy * camX;

    int   mx = (int)player.x, my = (int)player.y;
    float ddx = (rdx == 0.0f) ? 1e30f : fabsf(1.0f/rdx);
    float ddy = (rdy == 0.0f) ? 1e30f : fabsf(1.0f/rdy);

    int   stepX, stepY;
    float sdx, sdy;
    if (rdx < 0) { stepX=-1; sdx=(player.x-mx)*ddx; }
    else         { stepX= 1; sdx=(mx+1.0f-player.x)*ddx; }
    if (rdy < 0) { stepY=-1; sdy=(player.y-my)*ddy; }
    else         { stepY= 1; sdy=(my+1.0f-player.y)*ddy; }

    int  side=0, wt=0, safe=0;
    bool hit=false;
    while (!hit && safe++<32) {
      if (sdx < sdy) { sdx+=ddx; mx+=stepX; side=0; }
      else           { sdy+=ddy; my+=stepY; side=1; }
      wt = mapAt(mx,my);
      if (wt) hit=true;
    }

    float pwd = hit ? (side==0 ? sdx-ddx : sdy-ddy) : 32.0f;
    if (pwd < 0.05f) pwd = 0.05f;
    zBuffer[col] = pwd;

    int lh = (int)(VIEW_H / pwd);
    int y0 = VIEW_HALF - lh/2; if (y0<0) y0=0;
    int y1 = VIEW_HALF + lh/2; if (y1>=VIEW_H) y1=VIEW_H-1;

    if (hit) drawWallColumn(col, y0, y1, pwd, wt, side);
  }

  // 3. Enemy sprites (sorted far→near)
  {
    int   idx[MAX_ENEMIES];
    float dst[MAX_ENEMIES];
    int   n = 0;
    float invDet = 1.0f / (player.cx*player.dy - player.dx*player.cy);

    for (int i = 0; i < MAX_ENEMIES; i++) {
      if (enemies[i].state == 4) continue;
      float ex = enemies[i].x - player.x;
      float ey = enemies[i].y - player.y;
      idx[n] = i;
      dst[n] = ex*ex + ey*ey;
      n++;
    }
    sortByDist(idx, dst, n);

    for (int k = 0; k < n; k++) {
      int    i  = idx[k];
      Enemy& e  = enemies[i];
      float  ex = e.x - player.x;
      float  ey = e.y - player.y;
      float  tX = invDet*( player.dy*ex - player.dx*ey);
      float  tY = invDet*(-player.cy*ex + player.cx*ey);
      if (tY < 0.15f) continue;

      int sX = (int)((VIEW_W/2.0f)*(1.0f + tX/tY));
      int sH = abs((int)(VIEW_H / tY));

      drawEnemySprite(sX, sH, tY, e.state, e.frame);
    }
  }

  // 4. Weapon
  drawWeapon();

  // 5. HUD
  drawHUD();

  // 6. Minimap
  drawMinimap();

  // 7. Damage flash — invert screen border
  if (damageFlash > 0) {
    for (int x=0;x<VIEW_W;x++) { u8g2.drawPixel(x,0); u8g2.drawPixel(x,VIEW_H-1); }
    for (int y=0;y<VIEW_H;y++) { u8g2.drawPixel(0,y); u8g2.drawPixel(VIEW_W-1,y); }
    // Second border line for more impact
    for (int x=2;x<VIEW_W-2;x++) { u8g2.drawPixel(x,2); u8g2.drawPixel(x,VIEW_H-3); }
    for (int y=2;y<VIEW_H-2;y++) { u8g2.drawPixel(2,y); u8g2.drawPixel(VIEW_W-3,y); }
    damageFlash--;
  }

  u8g2.sendBuffer();
}

// ──────────────────────────────────────────────────────────────────
// SPLASH SCREEN
// ──────────────────────────────────────────────────────────────────
void showSplash() {
  // Scan-line wipe in
  for (int y = 0; y <= 64; y += 4) {
    u8g2.clearBuffer();
    for (int i=0;i<y&&i<64;i++) u8g2.drawHLine(0,i,128);
    u8g2.sendBuffer();
    delay(10);
  }

  u8g2.clearBuffer();
  u8g2.drawBox(0, 0, 128, 64);
  u8g2.setDrawColor(0);

  u8g2.setFont(u8g2_font_logisoso16_tr);
  int tw = u8g2.getStrWidth("DOOM");
  u8g2.drawStr((128-tw)/2, 22, "DOOM");
  u8g2.drawHLine(10, 25, 108);

  u8g2.setFont(u8g2_font_5x7_tr);
  drawCentered("ESP32-C3 EDITION", 35);

  u8g2.setFont(u8g2_font_4x6_tr);
  drawCentered("KILL THEM ALL, MARINE!", 47);

  u8g2.setFont(u8g2_font_4x6_tr);
  drawCentered("L+R=SHOOT  UP+DOWN=PAUSE", 56);
  drawCentered("ANY KEY TO START", 63);

  // Corner pips
  for (int i=0;i<4;i++) {
    u8g2.drawPixel(i,i); u8g2.drawPixel(127-i,i);
    u8g2.drawPixel(i,63-i); u8g2.drawPixel(127-i,63-i);
  }
  u8g2.setDrawColor(1);
  u8g2.sendBuffer();

  waitRelease();
  while (!ANY_P) updateButtons();
  waitRelease();

  // Wipe transition out
  for (int y=0;y<=64;y+=2) {
    u8g2.clearBuffer();
    u8g2.drawBox(0, 0, 128, y);
    u8g2.sendBuffer();
    delay(6);
  }
}

// ──────────────────────────────────────────────────────────────────
// GAME OVER SCREEN
// ──────────────────────────────────────────────────────────────────
void showDead() {
  for (int f=0;f<6;f++) {
    u8g2.clearBuffer();
    if (f%2==0) { u8g2.drawBox(0,0,128,64); u8g2.setDrawColor(0); }
    u8g2.setFont(u8g2_font_logisoso16_tr);
    drawCentered("YOU  DIED", 32);
    u8g2.setDrawColor(1);
    u8g2.sendBuffer(); delay(160);
  }

  u8g2.clearBuffer();
  u8g2.drawBox(0,0,128,64);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_7x13B_tr);
  drawCentered("YOU  DIED", 14);
  u8g2.drawHLine(4,17,120);
  u8g2.setFont(u8g2_font_5x7_tr);
  char buf[32];
  sprintf(buf,"SCORE : %d", player.score); u8g2.drawStr(14,28,buf);
  sprintf(buf,"KILLS : %d/%d", player.kills, MAX_ENEMIES); u8g2.drawStr(14,38,buf);
  sprintf(buf,"AMMO  : %d", player.ammo);  u8g2.drawStr(14,48,buf);
  u8g2.setFont(u8g2_font_4x6_tr);
  drawCentered("ANY BUTTON TO RESTART", 60);
  u8g2.setDrawColor(1);
  u8g2.sendBuffer();
  delay(800); waitRelease();
  while (!ANY_P) updateButtons();
  waitRelease();
}

// ──────────────────────────────────────────────────────────────────
// VICTORY SCREEN
// ──────────────────────────────────────────────────────────────────
void showWin() {
  for (int f=0;f<6;f++) {
    u8g2.clearBuffer();
    if (f%2==0) { u8g2.drawBox(0,0,128,64); u8g2.setDrawColor(0); }
    u8g2.setFont(u8g2_font_logisoso16_tr);
    drawCentered("VICTORY!", 32);
    u8g2.setDrawColor(1);
    u8g2.sendBuffer(); delay(200);
  }

  u8g2.clearBuffer();
  u8g2.drawBox(0,0,128,64);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_7x13B_tr);
  drawCentered("VICTORY!", 14);
  u8g2.drawHLine(4,17,120);
  u8g2.setFont(u8g2_font_5x7_tr);
  char buf[32];
  sprintf(buf,"SCORE : %d", player.score); u8g2.drawStr(14,28,buf);
  sprintf(buf,"KILLS : %d/%d", player.kills, MAX_ENEMIES); u8g2.drawStr(14,38,buf);
  u8g2.setFont(u8g2_font_4x6_tr);
  drawCentered("ALL DEMONS DESTROYED!", 50);
  drawCentered("ANY BUTTON TO RESTART", 60);
  u8g2.setDrawColor(1);
  u8g2.sendBuffer();
  delay(800); waitRelease();
  while (!ANY_P) updateButtons();
  waitRelease();
}

// ──────────────────────────────────────────────────────────────────
// PAUSE SCREEN
// ──────────────────────────────────────────────────────────────────
void showPause() {
  u8g2.clearBuffer();
  u8g2.drawFrame(22,8,84,48);
  u8g2.drawFrame(24,10,80,44);
  u8g2.setFont(u8g2_font_7x13B_tr);
  drawCentered("PAUSED",28);
  u8g2.drawHLine(26,30,76);
  u8g2.setFont(u8g2_font_4x6_tr);
  drawCentered("UP/DN   = FWD/BACK", 38);
  drawCentered("L/R     = TURN",     46);
  drawCentered("UP+L/R  = STRAFE",   54);
  drawCentered("L+R     = SHOOT",    62);
  u8g2.sendBuffer();
  delay(300); waitRelease();
  while (!ANY_P) updateButtons();
  waitRelease();
}

// ──────────────────────────────────────────────────────────────────
// GAME LOOP
// ──────────────────────────────────────────────────────────────────
void gameLoop() {
  playerInit();
  enemiesInit();
  damageFlash = 0;
  prevHP      = 100;
  shooting    = false;
  shootFrame  = 0;
  lastShot    = 0;

  while (true) {
    updateButtons();
    unsigned long now = millis();

    // ── Pause: UP+DOWN ──
    if (UP_H && DOWN_H) { showPause(); continue; }

    // ── Shoot: LEFT+RIGHT ──
    if (LEFT_H && RIGHT_H) {
      shoot();
    } else {
      // ── Movement ──
      if      (UP_H && LEFT_H)  playerMove(MOVE_SPEED, -STRAFE_SPD);  // strafe L
      else if (UP_H && RIGHT_H) playerMove(MOVE_SPEED,  STRAFE_SPD);  // strafe R
      else {
        if (UP_H)    playerMove(MOVE_SPEED, 0);   // forward
        if (DOWN_H)  playerMove(-MOVE_SPEED, 0);  // back
        if (LEFT_H)  playerRotate(-ROT_SPEED);    // turn L
        if (RIGHT_H) playerRotate( ROT_SPEED);    // turn R
      }
    }

    // ── Shoot animation ──
    if (shooting) {
      shootFrame = (int)((now - shootTime) / (SHOOT_ANIM_MS/4));
      if (now - shootTime > SHOOT_ANIM_MS) { shooting = false; shootFrame = 0; }
    }

    // ── Enemy AI ──
    updateEnemies();

    // ── Damage flash ──
    if (player.hp < prevHP) damageFlash = 4;
    prevHP = player.hp;

    // ── Render ──
    renderFrame();

    // ── End conditions ──
    if (player.hp <= 0) { showDead(); return; }

    bool allDead = true;
    for (int i=0;i<MAX_ENEMIES;i++) if (enemies[i].state!=4) { allDead=false; break; }
    if (allDead) { showWin(); return; }
  }
}

// ──────────────────────────────────────────────────────────────────
// SETUP & LOOP
// ──────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  initButtons();
  u8g2.begin();
  u8g2.setContrast(220);
  randomSeed(analogRead(A0) ^ analogRead(A1) ^ (unsigned long)millis());
  showSplash();
}

void loop() {
  gameLoop();
}