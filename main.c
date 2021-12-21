#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <conio.h> // _kbhit, _getch
#include <time.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
#define IS_WIN
#include <windows.h>
#endif

/*
 * types
 */

typedef int8_t i8;
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

typedef __uint128_t u128;
#define U128_CONST(high, low) (((u128)(high) << 64) | (low)) // as there are no 128-bit integer constants, we must use binary operations to create one from two 64-bit integers (high, low)

/*
 * defines
 */

#define BOARD_WIDTH  10
#define BOARD_HEIGHT 12
#define BOARD_BITLINE(ln)      (U128_CONST(0xFFC0000000000000, 0) >> ((ln) * BOARD_WIDTH)) // full line as an 128-bit integer
#define BOARD_FULL              U128_CONST(0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF)
#define BOARD_FULL_LINE         0x3FF
#define BOARD_FULL_TO_LINE(ln) (~(BOARD_FULL >> (((ln)+1) * BOARD_WIDTH))) // mask of lines of 1's that got all the way up to the given line number 'ln'

#define BLOCK(width, height)      ((height) | ((width) << 3)) // as the board is (10*12)120 bits large, we have 8 bits to spare
#define BLOCK_HEIGHT(block)       (((u64)(block) & 0b000111) >> 0) // lowest three bits is height
#define BLOCK_WIDTH(block)        (((u64)(block) & 0b111000) >> 3) // next three is width
#define BLOCK_MOVE(block, x, y)   ((block) >> ((x) + (y) * BOARD_WIDTH))

#define REFRESH_DELAY 0.5 // in seconds

/*
 * tetris
 */

static u128 board = 0;
static u8 blockid = 0; // index in blocks[]
static const u128* block;
static u8 x = 0;
static u8 y = 0;

static const u128 blocks[] =
{
    U128_CONST(0x4038000000000000, BLOCK(3,2)), // T: 0
    U128_CONST(0x8030080000000000, BLOCK(2,3)),
    U128_CONST(0xE010000000000000, BLOCK(3,2)),
    U128_CONST(0x4030040000000000, BLOCK(2,3)),
    U128_CONST(0x8038000000000000, BLOCK(3,2)), // J: 4
    U128_CONST(0xC020080000000000, BLOCK(2,3)),
    U128_CONST(0xE008000000000000, BLOCK(3,2)),
    U128_CONST(0x40100C0000000000, BLOCK(2,3)),
    U128_CONST(0x2038000000000000, BLOCK(3,2)), // L: 8
    U128_CONST(0x80200C0000000000, BLOCK(2,3)),
    U128_CONST(0xE020000000000000, BLOCK(3,2)),
    U128_CONST(0xC010040000000000, BLOCK(2,3)),
    U128_CONST(0xC030000000000000, BLOCK(3,2)), // O: 12
    U128_CONST(0xC018000000000000, BLOCK(3,2)), // Z: 13
    U128_CONST(0x4030080000000000, BLOCK(2,3)),
    U128_CONST(0x6030000000000000, BLOCK(3,2)), // S: 15
    U128_CONST(0x8030040000000000, BLOCK(2,3)),
    U128_CONST(0xF000000000000000, BLOCK(4,1)), // I: 17
    U128_CONST(0x8020080200000000, BLOCK(1,4))
};

static void setblock (u8 idx)
{
    block = &blocks[blockid = idx];
}

static void print (u128 _board)
{
#ifdef IS_WIN
    system("cls"); // yeah stinks but my eyes hurt
#endif

    printf("\r/==========\\\n");
    u128 mp = (u128)1 << 127; // mask pointer
    for (u8 y = 0; y < BOARD_HEIGHT; y++)
    {
        putchar('|');
        for (u8 x = 0; x < BOARD_WIDTH; x++)
        {
            putchar((_board & mp) ? '#' : ' ');
            mp >>= 1;
        }
        puts("|");
    }
    printf("\\==========/\n");
}

static u8 movex (i8 by, i8 dir) // check collision for (x+by) and move by 'dir' (x+=dir)
{
    if((u8)(x + by) >= BOARD_WIDTH) return 1; // as coordinates cannot be negative
    if(board & BLOCK_MOVE(*block, x+by, y)) return 1; // collision detection for (x+by)
    x += dir;
    return 0;
}

static u8 down ()
{
    if(y + BLOCK_HEIGHT(*block) >= BOARD_HEIGHT) return 1; // collision detection for the bottom of the board
    if(board & BLOCK_MOVE(*block, x, y+1)) return 1; // collision detection for (y+1)
    y++;
    return 0;
}

static void rotate ()
{
#define ROT_CASE(id, idofs, dx, dy) case (id): setblock(blockid+(idofs)); x+=(dx); y+=(dy); break
    switch(blockid)
    {
        ROT_CASE( 0,  1,  1,  0); // T: 0
        ROT_CASE( 1,  1, -1,  1);
        ROT_CASE( 2,  1,  0, -1);
        ROT_CASE( 3, -3,  0,  0);
        ROT_CASE( 4,  1,  0,  1); // J: 4
        ROT_CASE( 5,  1, -2,  0);
        ROT_CASE( 6,  1,  1, -1);
        ROT_CASE( 7, -3,  1,  1);
        ROT_CASE( 8,  1,  1, -1); // L: 8
        ROT_CASE( 9,  1,  2,  0);
        ROT_CASE(10,  1,  0,  0);
        ROT_CASE(11, -3, -1, -1);
        ROT_CASE(13,  1,  1,  0); // Z: 13
        ROT_CASE(14, -1, -1,  0);
        ROT_CASE(15,  1,  0,  0); // S: 15
        ROT_CASE(16, -1,  0,  0);
        ROT_CASE(17,  1,  0,  0); // I: 17
        ROT_CASE(18, -1,  0,  0);
    }
#undef ROT_CASE
}

static void drop (u8 atline)
{
    board = (board & ~BOARD_FULL_TO_LINE(atline)) | (((board & BOARD_FULL_TO_LINE(atline)) >> BOARD_WIDTH) & BOARD_FULL_TO_LINE(atline));
}

int main ()
{
    srand(time(NULL));
    time_t time_now;
    time_t time_last = time(NULL);
    
    setblock(blockid);

    for(;;)
    {
        time_now = time(NULL);

        if(_kbhit())
        {
            switch(_getch())
            {
            case 'a': case 'A': movex(-1, -1); break;
            case 'd': case 'D': movex(BLOCK_WIDTH(*block), 1); break;
            case 's': case 'S': down(); break;
            case 'w': case 'W': {
                u8 ox=x, oy=y, obid=blockid; // saving the current state
                rotate();
                if(
                    (board & BLOCK_MOVE(*block,x,y)) ||
                    (x >= BOARD_WIDTH) ||
                    (y >= BOARD_HEIGHT) ||
                    ((x + BLOCK_WIDTH(*block)) >= BOARD_WIDTH) ||
                    ((y + BLOCK_HEIGHT(*block)) >= BOARD_HEIGHT)
                ) // rotation failed, new rotation has collided
                    x=ox, y=oy, setblock(obid); // retrieving the state
                break;
            }}
            print(board | BLOCK_MOVE(*block,x,y)); // print on any keyboard input
        }

        if(time_now - time_last < REFRESH_DELAY)
            continue;
        time_last = time_now;

        print(board | BLOCK_MOVE(*block,x,y));

        if(down())
        { // has collided
            board |= BLOCK_MOVE(*block,x,y);

            switch(rand() % 6) // get the next block
            {
            case 0: setblock(0);  break;
            case 1: setblock(4);  break;
            case 2: setblock(8);  break;
            case 3: setblock(13); break;
            case 4: setblock(15); break;
            case 5: setblock(17); break;
            }

            if ((board & BOARD_BITLINE(y+0)) == BOARD_BITLINE(y+0)) drop(y+0), y--;
            if ((board & BOARD_BITLINE(y+1)) == BOARD_BITLINE(y+1)) drop(y+1), y--;
            if ((board & BOARD_BITLINE(y+2)) == BOARD_BITLINE(y+2)) drop(y+2), y--;
            if ((board & BOARD_BITLINE(y+3)) == BOARD_BITLINE(y+3)) drop(y+3), y--;

            x = 0;
            y = 0;
        }
    }

    return EXIT_SUCCESS;
}
