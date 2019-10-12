#include <stdlib.h>
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 160
#define SCREEN_OFFSET 32

#define REG_DISPLAY (*((volatile uint16 *)0x04000000))
#define DISPLAY_MODE1 0x1000
#define DISPLAY_ENABLE_OBJECTS 0x0040

#define TILE_MEM ((volatile tile_block *)0x06000000)
#define PALETTE_MEM ((volatile palette *)(0x05000000 + 0x200))  // ignore bg mem
#define OAM_MEM ((volatile object *)0x07000000)

#define REG_VCOUNT (*(volatile uint16*) 0x04000006)

#define OAM_MEM  ((volatile object *)0x07000000)
#define Y_MASK 0x0FF
#define X_MASK 0x1FF
#define OAM_HIDE_MASK 0x300
#define OAM_HIDE 0x200

#define REG_KEY_INPUT (*((volatile uint16 *)0x04000130))
#define KEY_ANY  0x03FF
#define KEY_LEFT (1 << 4)
#define KEY_RIGHT (1 << 5)
#define VELOCITY 2

typedef unsigned char uint8;
typedef unsigned short uint16;      // controle bits voor OAM, RGB
typedef unsigned int uint32;        // 1 tile bit in de GBA
typedef uint32 tile_4bpp[8];        // 8 rijen, elk 1 bit
typedef tile_4bpp tile_block[512];  // tile block = 8 screen blocks, 512 tiles
typedef uint16 palette[16];         // 16 palettes beschikbaar

typedef struct object {
    uint16 attr0; // ypos
    uint16 attr1; // xpos
    uint16 attr2; // size?
    uint16 unused;
} __attribute__((packed, aligned(4))) object;

typedef struct sprite {
    int x;  // position
    int y;
    int dx; // velocity
    int dy;
    uint8 w;  // dimensions (simple hitbox detection)
    uint8 h;
    volatile object *obj;
} sprite;

int isHidden(sprite *s)
{
    volatile object *obj = s->obj;
    return obj->attr0 & OAM_HIDE;
}

void hide(sprite *s)
{
    volatile object *obj = s->obj;
    obj->attr0 = (obj->attr0 & ~OAM_HIDE_MASK) | OAM_HIDE;
}

void unhide(sprite *s)
{
    volatile object *obj = s->obj;
    obj->attr0 = (obj->attr0 & ~OAM_HIDE_MASK);
}

void position(sprite *s)
{
    volatile object *obj = s->obj;
    int x = s->x;
    int y = s->y;
    obj->attr0 = (obj->attr0 &  ~Y_MASK) | (y & Y_MASK);
    obj->attr1 = (obj->attr1 & ~X_MASK) | (x & X_MASK);
}

void vsync()
{
    while (REG_VCOUNT >= 160);
    while (REG_VCOUNT < 160);
}

uint16 get_color(uint16 r, uint16 g, uint16 b)
{
    uint16 c = (b & 0x1f) << 10;
    c |= (g & 0x1f) << 5;
    c |= (r & 0x1f);
    return c;
}

int next_oam_mem = 0;
int next_tile_mem = 1;

sprite* create_sprite(volatile object* obj, int initialx, int initialy, uint8 w, uint8 h)
{
    sprite* s = (sprite*) malloc(sizeof(sprite));
    s->obj = obj;
    s->x = initialx;
    s->y = initialy;
    s->w = w;
    s->h = h;
    position(s);
    return s;
}

void velocity(sprite *s)
{
  s->x += s->dx;
  s->y += s->dy;

  //Moving logic;
  s->x = s->x < 0 ? 0 : s->x;
  s->x = s->x > SCREEN_WIDTH - s->w ? SCREEN_WIDTH - s->w  : s->x;
  s->y = s->y < 0 ? 0 : s->y;
  s->y = s->y > SCREEN_HEIGHT - s->h ? SCREEN_HEIGHT - s->h  : s->y;
}

int collides(sprite *s, sprite *o)
{
    uint8 x1 = s->x;
    uint8 x2 = o->x;
    uint8 y1 = s->y;
    uint8 y2 = o->y;
    uint8 w1 = s->w;
    uint8 w2 = o->w;
    uint8 h1 = s->h;
    uint8 h2 = o->h;
    // http://image.diku.dk/projects/media/kirk.06.pdf#page=37
    if (x1 < x2 + w2 & x2 < x1 + w1 & y1 < y2 + h2 & y2 < y1 + h1)
    {
      return 1;
    }
    return 0;
}

volatile object* create_paddle()
{
    // 1. kleur
    PALETTE_MEM[0][2] = get_color(31, 0, 0);

    // 2. tile - vanaf hieronder alles bezet tot TILE_MEM[4][6]!
    volatile uint16 *paddle_tile = (uint16*) TILE_MEM[4][2];  // begin vanaf 2
    // vul de tile met de palet index 2 - dit is per rij, vandaar 0x2222
    for(int i = 0; i < 4 * sizeof(tile_4bpp) / 2; i++) {
        paddle_tile[i] = 0x2222;
    }

    // 3. object
    volatile object *paddle_sprite = &OAM_MEM[next_oam_mem++];
    paddle_sprite->attr0 = 0x4000; // 4bpp, wide
    paddle_sprite->attr1 = 0x4000; // 32x8 met wide shape
    paddle_sprite->attr2 = 2; // vanaf de 2de tile, palet 0

    return paddle_sprite;
}

volatile object* create_blok(int index)
{
    // 1. kleur
    PALETTE_MEM[0][3] = get_color(0, 31, 0);

    // 2. tile - vanaf hieronder alles bezet tot TILE_MEM[4][6]!
    volatile uint16 *blok_tile = (uint16*) TILE_MEM[4][6];  // begin vanaf 2
    // vul de tile met de palet index 2 - dit is per rij, vandaar 0x2222
    for(int i = 0; i < 4 * sizeof(tile_4bpp) / 2; i++) {
        blok_tile[i] = 0x3333;
    }

    // 3. object
    volatile object *blok_sprite = &OAM_MEM[index];
    blok_sprite->attr0 = 0x4000; // 4bpp, wide
    blok_sprite->attr1 = 0x4000; // 32x8 met wide shape
    blok_sprite->attr2 = 6; // vanaf de 2de tile, palet 0

    return blok_sprite;
}

volatile object* create_ball()
{
    // 1. kleur
    PALETTE_MEM[0][1] = get_color(31, 31, 31); // wit - zie labo 3

    // 2. tile
    volatile uint16 *ball_tile = (uint16*) TILE_MEM[4][1];  // 1 block
    // vul de tile met de palet index 1 - dit is per rij, vandaar 0x1111
    for(int i = 0; i < sizeof(tile_4bpp) / 2; i++) {
        ball_tile[i] = 0x1111;
    }

    // 3. object
    volatile object *ball_sprite = &OAM_MEM[next_oam_mem++];
    ball_sprite->attr0 = 0; // 4bpp, square
    ball_sprite->attr1 = 0; // grootte 8x8 met square
    ball_sprite->attr2 = 1; // eerste tile, palet 0

    return ball_sprite;
}

void resetGame(sprite *ball, sprite *paddle, int outer, int inner, sprite *blokken[outer][inner])
{
  paddle->x = (SCREEN_WIDTH / 2) - (32 / 2);
  paddle->y = SCREEN_HEIGHT - 10;
  ball->x = 50;
  ball->y = 50;
  ball->dx = 2;
  ball->dy = 1;
  position(paddle);
  position(ball);

  for (int j = 0; j < 5; j++)
  {
    for (int i = 0; i < 5 ; i++)
    {
      blokken[j][i]->x = 25 + 40 * i;
      blokken[j][i]->y = 10 * j;
      unhide(blokken[j][i]);
      position(blokken[j][i]);
    }
  }



}

void initScreen()
{
    REG_DISPLAY = DISPLAY_MODE1 | DISPLAY_ENABLE_OBJECTS;
}

uint16 readKeys()
{
    return ~REG_KEY_INPUT & KEY_ANY;
}

int main()
{
    uint16 keys;
    int paddle_x = (SCREEN_WIDTH / 2) - (32 / 2);
    int paddle_y = SCREEN_HEIGHT - 10;
    int ball_x = 50;
    int ball_y = 50;
    int ball_dx = 2;
    int ball_dy = 1;

    // ! belangrijke volgorde (ball, paddle, blokken)!
    sprite *ball = create_sprite(create_ball(), ball_x, ball_y, 8, 8);
    ball->dx = ball_dx;
    ball->dy = ball_dy;
    sprite *paddle = create_sprite(create_paddle(), paddle_x, paddle_y, 32, 8);

    sprite *blokken[5][5];

    for (int j = 0; j < 5; j++)
    {
      for (int i = 0; i < 5 ; i++)
      {
        uint8 memLoc = (j * 5) + i + 2;
        blokken[j][i] = create_sprite(create_blok(memLoc), 25 + 40 * i , 10 * j, 32, 8);
      }
    }

    initScreen();
    while(1)
    {
        vsync(); // vermijdt flikkeren
        keys = readKeys();
        if(keys & KEY_LEFT)
        {
          paddle->dx = 2;
          velocity(paddle);
        }
        if(keys & KEY_RIGHT)
        {
          paddle->dx = -2;
          velocity(paddle);
        }
        velocity(ball);
        //x boundaries
        if(ball->x <= 0 || ball->x >= (SCREEN_WIDTH - ball->w))
        {
            ball->dx = -ball->dx;
        }
        //y boundaries
        if(ball->y <= 0 || collides(ball, paddle))
        {
            ball->dy = -ball->dy;
        }
        //missed paddle
        if(ball->y >= (SCREEN_HEIGHT - ball->h))
        {
          resetGame(ball, paddle, 5,5, blokken);
        }

        //hit block
        for(int xRow = 0; xRow < 5; xRow++)
        {
          for(int yRow = 0; yRow < 5; yRow++)
          {
            sprite *obj = blokken[yRow][xRow];
            if(collides(ball, obj) && !isHidden(obj))
            {
                hide(obj);
                ball->dy = -ball->dy;
            }
          }
        }
        position(ball);
        position(paddle);
    }
}
