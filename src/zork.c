#include "zork.h"
#include "ecs.h"
#include "fs.h"
#include "heap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>



typedef struct room_t
{
	heap_t* heap;
	int id;
	char description[256];
	int next_room[4];
} room_t;

typedef struct zork_game_t
{
	heap_t* heap;
	fs_t* fs;
	room_t* rooms[64];
	int pos;
} zork_game_t;

// create a struct that holds the info of a room
room_t* room_create(heap_t* heap, int id, int n, int s, int e, int w, char* msg)
{
	room_t* room = heap_alloc(heap, sizeof(room_t), 8);
	room->heap = heap;
	room->id = id;
	room->next_room[0] = n;
	room->next_room[1] = s;
	room->next_room[2] = e;
	room->next_room[3] = w;
	strncpy_s(room->description, strlen(msg) + 1, msg, 256);
	return room;
}

void room_destroy(room_t* room)
{
	heap_free(room->heap, room);
}

int read_number(FILE* file)
{
	int ch, i = 0;
	char num[3];
	while (EOF != (ch = fgetc(file)) && isdigit(ch))
	{
		num[i] = ch;
		i++;
	}
	if (i == 0)
		return -2;
	return atoi(num)-1;
}

zork_game_t* zork_game_create(heap_t* heap, fs_t* fs, char* info_path)
{
	zork_game_t* game = heap_alloc(heap, sizeof(zork_game_t), 8);
	game->heap = heap;
	game->fs = fs;
	game->pos = 0;

	errno_t err;
	FILE* file;
	err = fopen_s(&file, info_path, "r");
	if (err != 0)
	{
		printf("Error opening file\n");
		return NULL;
	}
	int maximum_line_buffer_length = 256;
	char* line = (char*)malloc(sizeof(char) * maximum_line_buffer_length);
	int current_id = 0;
	while (read_number(file) != -2)
	{
		int n = read_number(file);
		int s = read_number(file);
		int e = read_number(file);
		int w = read_number(file);
		fgets(line, 256, file);
		game->rooms[current_id] = room_create(game->heap, current_id, n, s, e, w, line);
		current_id++;
	}

	free(line);
	return game;
}

void zork_game_destroy(zork_game_t* game)
{
	for (int i = 0; i < 64; i++)
	{
		if (game->rooms[i] == NULL)
			break;
		room_destroy(game->rooms[i]);
	}
	heap_free(game->heap, game);
}

void look_around(zork_game_t* game, char** msg)
{
	strncpy_s(*msg, strlen(game->rooms[game->pos]->description) + 1, game->rooms[game->pos]->description, 256);
}

void move_player(zork_game_t* game, int dir, char** msg)
{
	if (game->rooms[game->pos]->next_room[dir] == -1)
	{
		strncpy_s(*msg, 256, "You can't go that way.", 256);
		return;
	}

	game->pos = game->rooms[game->pos]->next_room[dir];
	if (dir == 0)
	{
		strncpy_s(*msg, 256, "You moved north.", 256);
	}
	else if (dir == 1)
	{
		strncpy_s(*msg, 256, "You moved south.", 256);
	}
	else if (dir == 2)
	{
		strncpy_s(*msg, 256, "You moved east.", 256);
	}
	else if (dir == 3)
	{
		strncpy_s(*msg, 256, "You moved west.", 256);
	}
}