/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv2 license, which unfortunately won't be
 * written for another century.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Wumpus (ported by lurk101)
 * Stolen from PCC Vol 2 No 1
 *
 * This version has been updated to compile for Pico using
 * gcc and the Raspberry Pico SDK on the Raspberry Pi.
 * The cave generator from the original has been replaced.
 * A detector is added for the ever so rare dodecahedron cave.
 */

// Enable cheat commands to dump the cave map and best shot.
#if !defined(CHEAT)
#define CHEAT 0
#endif

#include "hardware/flash.h"
#include "pico/stdlib.h"
#include "stdio.h"
#include "stdlib.h"

#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

// Boundaries
#define N_BATS 3    // 3 bats
#define N_ROOMS 20  // must be even
#define N_TUNNELS 3 // must be 3
#define N_PITS 3    // 3 pits
#define N_ARROWS 5  // 5 shots

// Room flags
#define HAS_BAT ((uint8_t)(1 << 0))
#define HAS_PIT ((uint8_t)(1 << 1))
#define HAS_WUMPUS ((uint8_t)(1 << 2))
#define HAS_VISIT ((uint8_t)(1 << 3))

// Tunnel flag
#define UN_MAPPED ((uint8_t)-1)

static uint32_t cave, empty_cave;        // cave bitmaps
static uint32_t arrow, loc, wloc;        // arrow count, player and wumpus locations
static uint8_t flags[N_ROOMS];           // array of room  flags
static union {
    uint8_t rooms[N_ROOMS][N_TUNNELS]; // tunnel map
    uint8_t sector[FLASH_PAGE_SIZE];
} buf;
static bool new_cave = false;

// Instructions
// clang-format off
static const char* intro1 =
    "\n"
    "The Wumpus lives in a cave of %d rooms.\n"
    "Each room has %d tunnels leading to other rooms.\n\n"
    "Hazards:\n\n"
    "Bottomless Pits - %d rooms have Bottomless Pits in them.\n"
    " If you go there, you fall into the pit and lose!\n"
    "Super Bats - %d other rooms have super bats.\n"
    " If you go there, a bat will grab you and take you to\n"
    " somewhere else in the cave where you could\n"
    " fall into a pit or run into the . . .\n\n"
    "Wumpus:\n\n"
    "The Wumpus is not bothered by the hazards since\n"
    "he has sucker feet and is too big for a bat to lift.\n\n"
    "Usually he is asleep. Two things wake him up:\n"
    " your entering his room\n"
    " your shooting an arrow anywhere in the cave.\n"
    "If the wumpus wakes, he either decides to move one room or\n"
    "stay where he was. But if he ends up where you are,\n"
    "he eats you up and you lose!\n\n";
static const char* intro2 =
    "You:\n\n"
    "Each turn you may either move or shoot a crooked arrow.\n\n"
    "Moving - You can move to one of the adjoining rooms;\n"
    " that is, to one that has a tunnel connecting it with\n"
    " the room you are in.\n\n"
    "Shooting - You have %d arrows. You lose when you run out.\n"
    " Each arrow can go from 1 to %d rooms.\n"
    " You aim by telling the computer\n"
    " The arrow's path is a list of room numbers\n"
    " telling the arrow which room to go to next.\n"
    " The first room in the path must be connected to the\n"
    " room you are in. Each succeeding room must be\n"
    " connected to the previous room.\n"
    " If there is no tunnel between two of the rooms\n"
    " in the arrow's path, the arrow chooses one of the\n"
    " three tunnels from the room it's in and goes its\n"
    " own way.\n\n"
    " If the arrow hits the wumpus, you win!\n"
    " If the arrow hits you, you lose!\n\n";
static const char* intro3 =
    "Warnings:\n\n"
    "When you are one or two rooms away from the wumpus,\n"
    "the computer says:\n"
    "   'I smell a Wumpus'\n"
    "When you are one room away from some other hazard, it says:\n"
    "   Bat    - 'Bats nearby'\n"
    "   Pit    - 'I feel a draft'\n";

const char* banner =
    " _   _             _     _____ _\n"
    "| | | |           | |   |_   _| |\n"
    "| |_| |_   _ _ __ | |_    | | | |__   ___\n"
    "|  _  | | | | '_ \\| __|   | | | '_ \\ / _ \\\n"
    "| | | | |_| | | | | |_    | | | | | |  __/\n"
    "\\_| |_/\\__,_|_| |_|\\__|   \\_/ |_| |_|\\___|\n\n"
    " _    _\n"
    "| |  | |\n"
    "| |  | |_   _ _ __ ___  _ __  _   _ ___\n"
    "| |/\\| | | | | '_ ` _ \\| '_ \\| | | / __|\n"
    "\\  /\\  / |_| | | | | | | |_) | |_| \\__ \\\n"
    " \\/  \\/ \\__,_|_| |_| |_| .__/ \\__,_|___/\n"
    "                       | |\n"
    "                       |_|\n\n";
// clang-format on

// Random numbers

// Uniform distribution 0..n-1
static inline uint32_t random_number(uint32_t n) {
    const uint32_t bits_used = 24;
    const uint32_t max_r = (1 << bits_used);
    uint32_t d = max_r / n;
    uint32_t r;
    while (unlikely((r = (rand() & (max_r - 1)) / d) >= n))
        ;
    return r;
}

// Console output
static inline void put_str(char* s) {
    fputs(s, stdout);
    fflush(stdout);
}

static inline void put_newline(void) { putchar('\n'); }

// Console input
static uint32_t argc;
static char* argv[N_ARROWS + 1];
static char cmd_buffer[64];
static char cchr;

static void get_and_parse_cmd(void) {
    // read line into buffer
    char* cp = cmd_buffer;
    char* cp_end = cp + sizeof(cmd_buffer);
    do {
        cchr = getchar();
        if (unlikely(cchr == '\b')) {
            if (likely(cp != cmd_buffer)) {
                cp--;
                put_str(" \b");
            }
        } else if (likely(cp < cp_end))
            *cp++ = cchr;
    } while (likely((cchr != '\r') && (cchr != '\n')));
    // parse buffer
    cp = cmd_buffer;
    bool not_last = true;
    for (argc = 0; likely(not_last && (argc <= N_ARROWS)); argc++) {
        while ((*cp == ' ') || (*cp == ','))
            cp++; // skip blanks
        if ((*cp == '\r') || (*cp == '\n'))
            break;
        argv[argc] = cp; // start of string
        while ((*cp != ' ') && (*cp != ',') && (*cp != '\r') && (*cp != '\n'))
            cp++; // skip non blank
        if ((*cp == '\r') || (*cp == '\n'))
            not_last = false;
        *cp++ = 0; // terminate string
    }
    if (likely(argc))
        *argv[0] |= ' '; // to lower lowercase
}

#if N_ROOMS == 20 // dodecahedron must have 20 rooms

static uint8_t A[N_ROOMS][N_ROOMS];
static uint8_t B[N_ROOMS][N_ROOMS];
static uint8_t T[N_ROOMS][N_ROOMS];

#if !defined(NDEBUG)

// Known dodecahedron for debug sanity check
static const uint8_t dodecahedron[N_ROOMS][N_TUNNELS] = {
    {1, 4, 7},   {0, 2, 9},    {1, 3, 11},  {2, 4, 13},  {0, 3, 5},    {4, 6, 14},   {5, 7, 16},
    {0, 6, 8},   {7, 9, 17},   {1, 8, 10},  {9, 11, 18}, {2, 10, 12},  {11, 13, 19}, {3, 12, 14},
    {5, 13, 15}, {14, 16, 19}, {6, 15, 17}, {8, 16, 18}, {10, 17, 19}, {12, 15, 18}};

#endif // !defined(NDEBUG)

// The dodecahedron detector
static inline void matrix_clear(uint8_t T[N_ROOMS][N_ROOMS]) {
    for (uint32_t i = 0; i < N_ROOMS; i++)
        for (uint32_t j = 0; j < N_ROOMS; j++)
            T[i][j] = 0;
}

static void matrix_mult(uint8_t T[N_ROOMS][N_ROOMS], const uint8_t A[N_ROOMS][N_ROOMS],
                        const uint8_t B[N_ROOMS][N_ROOMS]) {
    matrix_clear(T);
    for (uint32_t i = 0; i < N_ROOMS; i++)
        for (uint32_t k = 0; k < N_ROOMS; k++)
            for (uint32_t j = 0; j < N_ROOMS; j++)
                T[i][j] += A[i][k] * B[k][j];
}

static void matrix_square(uint8_t T[N_ROOMS][N_ROOMS], const uint8_t A[N_ROOMS][N_ROOMS]) {
    matrix_clear(T);
    for (uint32_t i = 0; i < N_ROOMS; i++)
        for (uint32_t k = 0; k < N_ROOMS; k++)
            for (uint32_t j = 0; j < N_ROOMS; j++)
                T[i][j] += A[i][k] * A[k][j];
}

// Return true if cave forms a dodecahedron
static bool is_dodecahedron(void) {
    matrix_clear(A);
    for (uint32_t v = 0; v < N_ROOMS; v++)
        for (uint32_t d = 0; d < N_TUNNELS; d++)
            A[v][buf.rooms[v][d]] = 1;
    matrix_square(T, A);
    matrix_square(B, T);
    matrix_mult(T, A, B);
    for (uint32_t v = 0; v < N_ROOMS; v++)
        if (T[v][v] != 6)
            return false;
    return true;
}

#endif // N_ROOMS == 20

// Cave generator helpers

// Bitmap functions
static inline void occupy_room(uint32_t b) { cave &= ~(1 << b); }
static inline bool room_is_empty(uint32_t b) { return (cave & (1 << b)) != 0; }
static inline uint32_t vacant_room_count(void) { return __builtin_popcount(cave); }

// Try to find player, starting at wumpus
static bool search_for_arrow_path(const uint8_t R[N_ROOMS][N_TUNNELS], uint32_t r, uint32_t depth,
                                  bool print) {
    flags[r] |= HAS_VISIT;
    for (uint32_t t = 0; t < N_TUNNELS; t++) {
        uint32_t e = R[r][t];
        if (unlikely(e == loc))
            return true;
        if (likely(depth))
            if (!(flags[e] & HAS_VISIT) && search_for_arrow_path(R, e, depth - 1, print)) {
                if (print)
                    printf("%d ", R[r][t] + 1);
                return true;
            }
    }
    return false;
}

static bool verify_map(const uint8_t R[N_ROOMS][N_TUNNELS]) {
    // Map sanity check
    for (uint32_t i = 0; i < N_ROOMS; i++)
        for (uint32_t j = 0; j < N_TUNNELS; j++)
            if (unlikely(R[i][j] >= N_ROOMS))
                return false;
    uint32_t count[N_ROOMS];
    for (uint32_t i = 0; i < N_ROOMS; i++)
        count[i] = 0;
    for (uint32_t i = 0; i < N_ROOMS; i++) {
        // 3 unique tunnels
        if (unlikely((R[i][0] == R[i][1]) || (R[i][0] == R[i][2]) || (R[i][1] == R[i][2])))
            return false;
        for (uint32_t j = 0; j < N_TUNNELS; j++) {
            // tunnel doesn't circle back
            if (unlikely(R[i][j] == i))
                return false;
            count[R[i][j]]++;
        }
    }
    // Each room has 3 tunnels
    for (uint32_t i = 0; i < N_ROOMS; i++)
        if (unlikely(count[i] != 3))
            return false;
    // Is it connected?
    for (uint32_t r1 = 0; r1 < N_ROOMS - 1; r1++) {
        loc = r1;
        for (uint32_t r2 = r1 + 1; r2 < N_ROOMS; r2++) {
            for (uint32_t j = 0; j < N_ROOMS; j++)
                flags[j] &= ~HAS_VISIT;
            if (unlikely(!search_for_arrow_path(R, r2, N_ROOMS - 1, false)))
                return false;
        }
    }
    return true;
}

// Pick and occupy a random vacant room
static uint32_t pick_and_occupy_empty_room(void) {
    uint32_t r, n = random_number(vacant_room_count());
    for (r = 0; r < N_ROOMS; r++)
        if (room_is_empty(r)) {
            if (n == 0)
                break;
            n--;
        }
    occupy_room(r);
    return r;
}

// Add tunnel from room to room
static void add_direct_tunnel(uint32_t f, uint32_t t) {
    for (uint32_t i = 0; i < N_TUNNELS; i++)
        if (buf.rooms[f][i] == UN_MAPPED) {
            buf.rooms[f][i] = t;
            break;
        }
}

// Add tunnels connecting to and from
static void add_tunnel(uint32_t f, uint32_t t) {
    add_direct_tunnel(f, t);
    add_direct_tunnel(t, f);
}

// Exchange adjacent bytes if 1st byte is greater than 2nd
static inline void exchange(uint8_t* t) {
    if (*t > *(t + 1)) {
        *t ^= *(t + 1);
        *(t + 1) ^= *t;
        *t ^= *(t + 1);
    }
}

// Generate a new cave
static bool directed_graph(void) {

    // Clear the tunnel map
    for (uint32_t r = 0; r < N_ROOMS; r++)
        for (uint32_t t = 0; t < N_TUNNELS; t++)
            buf.rooms[r][t] = UN_MAPPED;

    // Step 1 - Generate a random 20 room cycle.
    uint32_t r = 0, rs = 0;
    cave = empty_cave;
    occupy_room(r);
    do {
        uint32_t e = pick_and_occupy_empty_room();
        add_tunnel(r, e);
        r = e;
    } while (unlikely(cave));
    add_tunnel(r, rs);

    // Step 2 - add the third tunnels... if possible.
    cave = empty_cave;
    for (uint32_t n = 0; n < N_ROOMS / 2; n++) {
        assert(cave); // can't happen
        r = pick_and_occupy_empty_room();
        uint32_t save_cave = cave;
        // disqualify neighbors
        for (uint32_t t = 0; t < N_TUNNELS; t++)
            occupy_room(buf.rooms[r][t]);
        if (unlikely(!cave)) // Oops, can't complete this one!
            return false;
        uint32_t e = pick_and_occupy_empty_room();
        cave = save_cave;
        occupy_room(e);
        add_tunnel(r, e);
    }

    // Step 3 - sort the tunnels
    for (uint32_t i = 0; i < N_ROOMS; i++) {
        exchange(buf.rooms[i]);
        exchange(buf.rooms[i] + 1);
        exchange(buf.rooms[i]);
    }

#if !defined(NDEBUG)
    assert(verify_map(buf.rooms));
#endif // !defined(NDEBUG)

    return true;
}

// Recursive depth 1st neighbor search for hazard
static bool near(uint32_t r, uint8_t haz, uint32_t depth) {
    for (uint32_t t = 0; t < N_TUNNELS; t++) {
        if (flags[buf.rooms[r][t]] & haz)
            return true;
        if ((depth > 1) && near(buf.rooms[r][t], haz, depth - 1))
            return true;
    }
    return false;
}

// State functions
typedef void* (*func_ptr)(void);

static func_ptr instruction_handler(void);
static func_ptr init_1st_cave_handler(void);
static func_ptr init_cave_handler(void);
static func_ptr setup_handler(void);
static func_ptr loop_handler(void);
static func_ptr done_handler(void);
static func_ptr again_handler(void);
static func_ptr move_player_handler(void);
static func_ptr shoot_handler(void);
static func_ptr move_wumpus_handler(void);

static bool valid_room_number(int n) {
    bool b = (n >= 0) && (n < N_ROOMS);
    if (!b)
        printf("\n%d is not a room number\n", n + 1);
    return b;
}

// Show instructions
static func_ptr instruction_handler(void) {
    printf(intro1, N_ROOMS, N_TUNNELS, N_PITS, N_BATS);
    put_str("Hit RETURN to continue ");
    get_and_parse_cmd();
    put_newline();
    printf(intro2, N_BATS, N_ARROWS, N_ARROWS);
    put_str("Hit RETURN to continue ");
    get_and_parse_cmd();
    put_newline();
    put_str((char*)intro3);
    return (func_ptr)init_1st_cave_handler;
}

// Create or load cave from flash
static func_ptr init_1st_cave_handler(void) {
    const uint8_t(*flash)[N_ROOMS][N_TUNNELS] =
        (void*)(XIP_BASE + PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE);
    if (!verify_map(*flash))
        return (func_ptr)init_cave_handler;
    put_str("\nContinue with saved cave (Y/n) ? ");
    get_and_parse_cmd();
    if ((argc == 0) || (*argv[0] == 'y')) {
        for (uint32_t r = 0; r < N_ROOMS; r++)
            for (uint32_t t = 0; t < N_TUNNELS; t++)
                buf.rooms[r][t] = (*flash)[r][t];
        return (func_ptr)setup_handler;
    }
    return (func_ptr)init_cave_handler;
}

// Create a fresh cave
static func_ptr init_cave_handler(void) {
    put_str("\nCreating new cave map.");
    while (!directed_graph())
        ;
#if N_ROOMS == 20
    if (unlikely(is_dodecahedron()))
        put_str(" Ooh! You're entering the rarest of caves, a dodecahedron.");
#endif // N_ROOMS == 20
    put_newline();
    new_cave = true;
    return (func_ptr)setup_handler;
}

// Setup a new game in the current cave
static func_ptr setup_handler(void) {
    // put in player, wumpus, pits and bats
    uint32_t i, j;
    arrow = N_ARROWS;
    for (i = 0; i < N_ROOMS; i++)
        flags[i] = 0;
    for (i = 0; i < N_PITS;) {
        j = random_number(N_ROOMS);
        if (unlikely(!(flags[j] & HAS_PIT))) {
            flags[j] |= HAS_PIT;
            i++;
        }
    }
    for (i = 0; i < N_BATS;) {
        j = random_number(N_ROOMS);
        if (unlikely(!(flags[j] & (HAS_PIT | HAS_BAT)))) {
            flags[j] |= HAS_BAT;
            i++;
        }
    }
    wloc = random_number(N_ROOMS);
    flags[wloc] |= HAS_WUMPUS;
    for (;;)
    {
        i = random_number(N_ROOMS);
        if (unlikely(!(flags[i] & (HAS_PIT | HAS_BAT | HAS_WUMPUS)))) {
            loc = i;
            break;
        }
    }
    return (func_ptr)loop_handler;
}

// Just landed in new room, game loop
static func_ptr loop_handler(void) {
    printf("\nYou are in room %d", (int)loc + 1);
    // check for hazards
    if (flags[loc] & HAS_PIT) {
        put_str(". You fell into a pit. You lose.\n");
        return (func_ptr)done_handler;
    }
    if (flags[loc] & HAS_WUMPUS) {
        put_str(". You were eaten by the wumpus. You lose.\n");
        return (func_ptr)done_handler;
    }
    if (flags[loc] & HAS_BAT) {
        put_str(". Theres a bat in your room. Carying you away.\n");
        loc = random_number(N_ROOMS);
        return (func_ptr)loop_handler;
    }
    // anything nearby?
    if (near(loc, HAS_WUMPUS, 2))
        put_str(". I smell a wumpus");
    if (near(loc, HAS_BAT, 1))
        put_str(". Bats nearby");
    if (near(loc, HAS_PIT, 1))
        put_str(". I feel a draft");
    // travel options
    printf(". There are tunnels to rooms %d, %d and %d.\n", buf.rooms[loc][0] + 1,
           buf.rooms[loc][1] + 1, buf.rooms[loc][2] + 1);
    return (func_ptr)again_handler;
}

#if !defined(NDEBUG) || CHEAT
// Dump the cave cheat command
static func_ptr dump_cave_handler(void) {
    for (uint32_t r = 0; r < N_ROOMS; r++) {
        if ((r & 3) == 0)
            put_newline();
        printf("%02lu:%02d %02d %02d  ", r + 1, buf.rooms[r][0] + 1, buf.rooms[r][1] + 1,
               buf.rooms[r][2] + 1);
    }
    printf("\n\nPlayer:%02lu  Wumpus:%02lu  Pits:", loc + 1, wloc + 1);
    for (uint32_t r = 0; r < N_ROOMS; r++)
        if (flags[r] & HAS_PIT)
            printf("%02lu ", r + 1);
    put_str(" Bats:");
    for (uint32_t r = 0; r < N_ROOMS; r++)
        if (flags[r] & HAS_BAT)
            printf("%02lu ", r + 1);
    put_newline();
    return (func_ptr)again_handler;
}

// Find the best shot cheat command
static func_ptr best_shot_handler(void) {
    printf("\nBest shot: ");
    uint32_t i;
    for (i = 0; i <= N_ARROWS; i++) {
        for (uint32_t j = 0; j < N_ROOMS; j++)
            flags[j] &= ~HAS_VISIT;
        if (likely(search_for_arrow_path(buf.rooms, wloc, i, true))) {
            printf("%lu", wloc + 1);
            break;
        }
    }
    if (unlikely(i == N_ARROWS))
        put_str("none");
    put_newline();
    return (func_ptr)again_handler;
}

#endif // NDEBUG

// What are you going to do here?
static func_ptr again_handler(void) {
    put_str("\nMove or shoot (m/s) ? ");
    get_and_parse_cmd();
    if (argc == 0)
        return (func_ptr)again_handler;
    switch (*argv[0]) {
    case 'm':
        return (func_ptr)move_player_handler;
    case 's':
        return (func_ptr)shoot_handler;
#if !defined(NDEBUG) || CHEAT
    case 'd': // dump cave map
        return (func_ptr)dump_cave_handler;
    case 'b': // find best shot
        return (func_ptr)best_shot_handler;
#endif // NDEBUG
    }
    put_str("\nWhat ?\n");
    return (func_ptr)again_handler;
}

// Move on to next room
static func_ptr move_player_handler(void) {
    if (argc < 2) {
        put_str("\nwhich room ?\n");
        return (func_ptr)again_handler;
    }
    int r, t;
    r = atoi(argv[1]) - 1;
    if (!valid_room_number(r))
        return (func_ptr)again_handler;
    if (r < N_ROOMS)
        for (t = 0; t < N_TUNNELS; t++)
            if (r == buf.rooms[loc][t]) {
                loc = r;
                if (flags[r] & HAS_WUMPUS)
                    return (func_ptr)move_wumpus_handler;
                return (func_ptr)loop_handler;
            }
    put_str("You hit the wall!\n");
    return (func_ptr)again_handler;
}

// Shoot an arrow
static func_ptr shoot_handler(void) {
    if (unlikely(argc < 2)) {
        put_str("\nWhich tunnel(s) ?\n");
        return (func_ptr)again_handler;
    }
    for (uint32_t i = 1; i < argc; i++)
        if (unlikely(!valid_room_number(atoi(argv[i]) - 1)))
            return (func_ptr)again_handler;
    uint32_t t, r = atoi(argv[1]) - 1;
    for (t = 0; t < N_TUNNELS; t++)
        if (buf.rooms[loc][t] == r)
            break;
    if (unlikely(t == N_TUNNELS)) {
        put_str("\nNo tunnel to that room!\n");
        return (func_ptr)again_handler;
    }
    put_newline();
    int l = loc;
    for (uint32_t i = 0; i < N_ARROWS; i++) {
        if (i > argc - 2)
            break;
        r = atoi(argv[i + 1]) - 1;
        for (t = 0; t < N_TUNNELS; t++)
            if (r == buf.rooms[l][t])
                break;
        if (t == N_TUNNELS)
            t = random_number(N_TUNNELS);
        r = buf.rooms[l][t];
        put_str("~>");
        sleep_ms(500);
        printf("%d", (int)r + 1);
        if (r == loc) {
            put_str("\n\nYou shot yourself! You lose.\n");
            return (func_ptr)done_handler;
        }
        if (flags[r] & HAS_WUMPUS) {
            printf("\n\nYou slew the wumpus in room %d. You win!\n", (int)r + 1);
            return (func_ptr)done_handler;
        }
        sleep_ms(500);
        l = r;
    }
    put_str("\n\nYou missed!");
    if (--arrow == 0) {
        put_str(" That was your last shot! You lose.\n");
        return (func_ptr)done_handler;
    }
    put_newline();
    return (func_ptr)move_wumpus_handler;
}

// Wumpus disturbed, time to move it
static func_ptr move_wumpus_handler(void) {
    int i;
    flags[wloc] &= ~HAS_WUMPUS;
    i = random_number(N_TUNNELS + 1);
    if (likely(i != N_TUNNELS))
        wloc = buf.rooms[wloc][i];
    if (unlikely(wloc == loc)) {
        printf("\nThe wumpus %sate you. You lose.\n", ((i == N_TUNNELS) ? "" : "moved and "));
        return (func_ptr)done_handler;
    }
    flags[wloc] |= HAS_WUMPUS;
    return (func_ptr)loop_handler;
}

// Game over. Play again?
static func_ptr done_handler(void) {
    put_str("\nAnother game (Y/n) ? ");
    get_and_parse_cmd();
    if ((argc == 0) || (*argv[0] == 'y')) {
        put_str("\nSame room setup (Y/n) ? ");
        get_and_parse_cmd();
        if ((argc == 0) || (*argv[0] == 'y'))
            return (func_ptr)setup_handler;
        else
            return (func_ptr)init_cave_handler;
    }
    if (new_cave) {
        put_str("\nSaving cave for later...");
        const uint32_t offset = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
        flash_range_erase(offset, FLASH_SECTOR_SIZE);
        flash_range_program(offset, buf.rooms[0], FLASH_PAGE_SIZE);
        put_newline();
    }
    // Exit. Nowhere to go...
    put_str("\nBye!\n\n");
    exit(0);
}

// Forever loop
int main(void) {
    stdio_init_all();
    getchar_timeout_us(1000); // swallow the spurious EOF character???
    put_str("\033[H\033[J");  // try to clear the screen

#if !defined(NDEBUG) && (N_ROOMS == 20)
    // test the dodecahedron detector
    for (uint32_t r = 0; r < N_ROOMS; r++)
        for (uint32_t t = 0; t < N_TUNNELS; t++)
            buf.rooms[r][t] = dodecahedron[r][t];
    assert(is_dodecahedron());
#endif // !defined(NDEBUG) && (N_ROOMS == 20)

    empty_cave = (uint32_t)-1 >> (32 - N_ROOMS);
    printf("%s"
           "Welcome to HUNT THE WUMPUS.\n\n"
           "Instructions (y/N) ? ",
           banner);
    get_and_parse_cmd();

    srand(time_us_32());

    func_ptr state = (func_ptr)(((argc == 0) || (*argv[0] == 'n')) ? init_1st_cave_handler
                                                                   : instruction_handler);

    for (;; state = state())
        ;
}
