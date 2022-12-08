#pragma once

// Zork-like game room info

typedef struct zork_game_t zork_game_t;
typedef struct fs_t fs_t;
typedef struct heap_t heap_t;

// Create an instance of zork test game.
zork_game_t* zork_game_create(heap_t* heap, fs_t* fs, char* info_path);

// Destroy an instance of zork test game.
void zork_game_destroy(zork_game_t* game);

// output the description of the room
void look_around(zork_game_t* game, char** msg);

// move the player to the next room
void move_player(zork_game_t* game, int dir, char** msg);