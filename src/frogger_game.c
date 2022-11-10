#include "ecs.h"
#include "fs.h"
#include "gpu.h"
#include "heap.h"
#include "render.h"
#include "timer_object.h"
#include "transform.h"
#include "wm.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>

typedef struct transform_component_t
{
	transform_t transform;
} transform_component_t;

typedef struct camera_component_t
{
	mat4f_t projection;
	mat4f_t view;
} camera_component_t;

typedef struct model_component_t
{
	gpu_mesh_info_t* mesh_info;
	gpu_shader_info_t* shader_info;
} model_component_t;

typedef struct player_component_t
{
	int index;
} player_component_t;

typedef struct name_component_t
{
	char name[32];
} name_component_t;

typedef struct traffic_component_t
{
	int row;
} traffic_component_t;

typedef struct frogger_game_t
{
	heap_t* heap;
	fs_t* fs;
	wm_window_t* window;
	render_t* render;

	timer_object_t* timer;
	int row_timer0, row_timer1, row_timer2;
	int row2_count;

	ecs_t* ecs;
	int transform_type;
	int camera_type;
	int model_type;
	int player_type;
	int name_type;
	int traffic_type;
	ecs_entity_ref_t player_ent;
	ecs_entity_ref_t traffic_ent;
	ecs_entity_ref_t camera_ent;

	gpu_mesh_info_t cube_mesh;
	gpu_mesh_info_t rect_mesh;
	gpu_mesh_info_t pillar_mesh;
	gpu_shader_info_t cube_shader;
	gpu_shader_info_t traffic_shader;
	fs_work_t* vertex_shader_work;
	fs_work_t* vertex_shader_work_R;
	fs_work_t* fragment_shader_work;
} frogger_game_t;

static void load_resources(frogger_game_t* game);
static void unload_resources(frogger_game_t* game);
static void spawn_player(frogger_game_t* game, int index);
static void spawn_traffic_block(frogger_game_t* game, int row);
static void spawn_camera(frogger_game_t* game);
static void update_players(frogger_game_t* game);
static void update_traffic(frogger_game_t* game);
static void draw_models(frogger_game_t* game);

frogger_game_t* frogger_game_create(heap_t* heap, fs_t* fs, wm_window_t* window, render_t* render)
{
	frogger_game_t* game = heap_alloc(heap, sizeof(frogger_game_t), 8);
	game->heap = heap;
	game->fs = fs;
	game->window = window;
	game->render = render;

	game->timer = timer_object_create(heap, NULL);
	
	game->ecs = ecs_create(heap);
	game->transform_type = ecs_register_component_type(game->ecs, "transform", sizeof(transform_component_t), _Alignof(transform_component_t));
	game->camera_type = ecs_register_component_type(game->ecs, "camera", sizeof(camera_component_t), _Alignof(camera_component_t));
	game->model_type = ecs_register_component_type(game->ecs, "model", sizeof(model_component_t), _Alignof(model_component_t));
	game->player_type = ecs_register_component_type(game->ecs, "player", sizeof(player_component_t), _Alignof(player_component_t));
	game->name_type = ecs_register_component_type(game->ecs, "name", sizeof(name_component_t), _Alignof(name_component_t));
	game->traffic_type = ecs_register_component_type(game->ecs, "traffic", sizeof(traffic_component_t), _Alignof(traffic_component_t));

	load_resources(game);
	spawn_player(game, 0);
	spawn_camera(game);
	spawn_traffic_block(game, 0);
	spawn_traffic_block(game, 1);
	spawn_traffic_block(game, 2);

	game->row_timer0 = 0;
	game->row_timer1 = 0;
	game->row_timer2 = 0;
	game->row2_count = 1;

	return game;
}

void frogger_game_destroy(frogger_game_t* game)
{
	ecs_destroy(game->ecs);
	timer_object_destroy(game->timer);
	unload_resources(game);
	heap_free(game->heap, game);
}

void frogger_game_update(frogger_game_t* game)
{
	timer_object_update(game->timer);
	game->row_timer0 += timer_object_get_delta_ms(game->timer);
	game->row_timer1 += timer_object_get_delta_ms(game->timer);
	game->row_timer2 += timer_object_get_delta_ms(game->timer);
	ecs_update(game->ecs);
	update_players(game);
	update_traffic(game);
	if (game->row_timer0 > 1500)
	{
		game->row_timer0 = 0;
		spawn_traffic_block(game, 0);
	}
	if (game->row_timer1 > 3200)
	{
		game->row_timer1 = 0;
		spawn_traffic_block(game, 1);
	}
	if (game->row_timer2 > 800)
	{
		game->row_timer2 = 0;
		game->row2_count++;
		if (game->row2_count >= 3)
		{
			game->row2_count = 0;
			game->row_timer2 = -1700;
		}
		spawn_traffic_block(game, 2);
	}
	draw_models(game);
	render_push_done(game->render);
}

static void load_resources(frogger_game_t* game)
{
	game->vertex_shader_work = fs_read(game->fs, "shaders/triangle.vert.spv", game->heap, false, false);
	game->fragment_shader_work = fs_read(game->fs, "shaders/triangle.frag.spv", game->heap, false, false);
	game->cube_shader = (gpu_shader_info_t)
	{
		.vertex_shader_data = fs_work_get_buffer(game->vertex_shader_work),
		.vertex_shader_size = fs_work_get_size(game->vertex_shader_work),
		.fragment_shader_data = fs_work_get_buffer(game->fragment_shader_work),
		.fragment_shader_size = fs_work_get_size(game->fragment_shader_work),
		.uniform_buffer_count = 1,
	};

	static vec3f_t cube_verts[] =
	{
		{ -1.0f, -1.0f,  1.0f }, { 0.0f, 1.0f,  1.0f },
		{  1.0f, -1.0f,  1.0f }, { 1.0f, 0.0f,  1.0f },
		{  1.0f,  1.0f,  1.0f }, { 1.0f, 1.0f,  0.0f },
		{ -1.0f,  1.0f,  1.0f }, { 1.0f, 0.0f,  0.0f },
		{ -1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f,  0.0f },
		{  1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f,  1.0f },
		{  1.0f,  1.0f, -1.0f }, { 1.0f, 1.0f,  1.0f },
		{ -1.0f,  1.0f, -1.0f }, { 0.0f, 0.0f,  0.0f },
	};
	static vec3f_t rect_verts[] =
	{
		{ -1.0f, -3.0f,  1.0f }, { 0.0f, 3.0f,  1.0f },
		{  1.0f, -3.0f,  1.0f }, { 1.0f, 0.0f,  1.0f },
		{  1.0f,  3.0f,  1.0f }, { 1.0f, 3.0f,  0.0f },
		{ -1.0f,  3.0f,  1.0f }, { 1.0f, 0.0f,  0.0f },
		{ -1.0f, -3.0f, -1.0f }, { 0.0f, 3.0f,  0.0f },
		{  1.0f, -3.0f, -1.0f }, { 0.0f, 0.0f,  1.0f },
		{  1.0f,  3.0f, -1.0f }, { 1.0f, 3.0f,  1.0f },
		{ -1.0f,  3.0f, -1.0f }, { 0.0f, 0.0f,  0.0f },
	};
	static vec3f_t pillar_verts[] =
	{
		{ -1.0f, -6.0f,  1.0f }, { 0.0f, 6.0f,  1.0f },
		{  1.0f, -6.0f,  1.0f }, { 1.0f, 0.0f,  1.0f },
		{  1.0f,  6.0f,  1.0f }, { 1.0f, 6.0f,  0.0f },
		{ -1.0f,  6.0f,  1.0f }, { 1.0f, 0.0f,  0.0f },
		{ -1.0f, -6.0f, -1.0f }, { 0.0f, 6.0f,  0.0f },
		{  1.0f, -6.0f, -1.0f }, { 0.0f, 0.0f,  1.0f },
		{  1.0f,  6.0f, -1.0f }, { 1.0f, 6.0f,  1.0f },
		{ -1.0f,  6.0f, -1.0f }, { 0.0f, 0.0f,  0.0f },
	};
	static uint16_t cube_indices[] =
	{
		0, 1, 2,
		2, 3, 0,
		1, 5, 6,
		6, 2, 1,
		7, 6, 5,
		5, 4, 7,
		4, 0, 3,
		3, 7, 4,
		4, 5, 1,
		1, 0, 4,
		3, 2, 6,
		6, 7, 3
	};
	game->cube_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = cube_verts,
		.vertex_data_size = sizeof(cube_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};
	game->rect_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = rect_verts,
		.vertex_data_size = sizeof(rect_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};
	game->pillar_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = pillar_verts,
		.vertex_data_size = sizeof(pillar_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};
}

static void unload_resources(frogger_game_t* game)
{
	fs_work_destroy(game->fragment_shader_work);
	fs_work_destroy(game->vertex_shader_work);
}

static void spawn_player(frogger_game_t* game, int index)
{
	uint64_t k_player_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->player_type) |
		(1ULL << game->name_type);
	game->player_ent = ecs_entity_add(game->ecs, k_player_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->transform_type, true);
	transform_identity(&transform_comp->transform);
	transform_comp->transform.translation.y = 2.0f * index;
	transform_comp->transform.translation.z = 8.0f;

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "player");

	player_component_t* player_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->player_type, true);
	player_comp->index = index;

	model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->model_type, true);
	model_comp->mesh_info = &game->cube_mesh;
	model_comp->shader_info = &game->cube_shader;

}

static void spawn_traffic_block(frogger_game_t* game, int row)
{
	uint64_t k_traffic_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->name_type) |
		(1ULL << game->traffic_type);
	game->traffic_ent = ecs_entity_add(game->ecs, k_traffic_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->traffic_ent, game->transform_type, true);
	transform_identity(&transform_comp->transform);
	transform_comp->transform.translation.y = -26.0f;
	if (row != 1)
	{
		transform_comp->transform.translation.y = 22.0f;
	}
	transform_comp->transform.translation.z = 4.0f - (float)row * 7.0f;

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->traffic_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "traffic block");

	traffic_component_t* traffic_comp = ecs_entity_get_component(game->ecs, game->traffic_ent, game->traffic_type, true);
	traffic_comp->row = row;

	model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->traffic_ent, game->model_type, true);
	if (row == 1)
	{
		model_comp->mesh_info = &game->pillar_mesh;
	}
	else
	{
		model_comp->mesh_info = &game->rect_mesh;
	}
	model_comp->shader_info = &game->cube_shader;
}

static void spawn_camera(frogger_game_t* game)
{
	uint64_t k_camera_ent_mask =
		(1ULL << game->camera_type) |
		(1ULL << game->name_type);
	game->camera_ent = ecs_entity_add(game->ecs, k_camera_ent_mask);

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->camera_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "camera");

	camera_component_t* camera_comp = ecs_entity_get_component(game->ecs, game->camera_ent, game->camera_type, true);
	
	float view_distance = 7.5f;
	mat4f_make_orthodox(&camera_comp->projection, -16.0f / 9.0f * view_distance, 16.0f / 9.0f * view_distance, -view_distance, view_distance, -1000.0f, 1000.0f);

	vec3f_t eye_pos = vec3f_scale(vec3f_forward(), -7.5f);
	vec3f_t forward = vec3f_forward();
	vec3f_t up = vec3f_up();
	mat4f_make_lookat(&camera_comp->view, &eye_pos, &forward, &up);
}

static void update_players(frogger_game_t* game)
{
	float dt = (float)timer_object_get_delta_ms(game->timer) * 0.001f;

	uint32_t key_mask = wm_get_key_mask(game->window);

	uint64_t k_query_mask = (1ULL << game->transform_type) | (1ULL << game->player_type);

	for (ecs_query_t query = ecs_query_create(game->ecs, k_query_mask);
		ecs_query_is_valid(game->ecs, &query);
		ecs_query_next(game->ecs, &query))
	{
		transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
		player_component_t* player_comp = ecs_query_get_component(game->ecs, &query, game->player_type);

		if (transform_comp->transform.translation.z < -10.0f)
		{
			transform_comp->transform.translation.z = 8.0f;
		}

		transform_t move;
		transform_identity(&move);
		if (key_mask & k_key_up)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), -10.0f * dt));
		}
		transform_multiply(&transform_comp->transform, &move);
	}
}

static void update_traffic(frogger_game_t* game)
{
	float dt = (float)timer_object_get_delta_ms(game->timer) * 0.001f;

	uint64_t k_query_mask = (1ULL << game->transform_type) | (1ULL << game->traffic_type);
	uint64_t pl_query_mask = (1ULL << game->transform_type) | (1ULL << game->player_type);
	ecs_query_t pl_query = ecs_query_create(game->ecs, pl_query_mask);
	if (!ecs_query_is_valid(game->ecs, &pl_query))
	{
		return;
	}
	transform_component_t* pl_transform_comp = ecs_query_get_component(game->ecs, &pl_query, game->transform_type);

	for (ecs_query_t query = ecs_query_create(game->ecs, k_query_mask);
		ecs_query_is_valid(game->ecs, &query);
		ecs_query_next(game->ecs, &query))
	{
		transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
		traffic_component_t* traffic_comp = ecs_query_get_component(game->ecs, &query, game->traffic_type);

		if (transform_comp->transform.translation.y > 27.0f || transform_comp->transform.translation.y < -27.0f)
		{
			ecs_entity_remove(game->ecs, ecs_query_get_entity(game->ecs, &query), false);
		}
		
		if (traffic_comp->row == 1)
		{
			if (fabs(transform_comp->transform.translation.y - pl_transform_comp->transform.translation.y) < 6.9f
				&& fabs(transform_comp->transform.translation.z - pl_transform_comp->transform.translation.z) < 1.9f)
			{
				pl_transform_comp->transform.translation.z = 8.0f;
			}
		}
		else
		{
			if (fabs(transform_comp->transform.translation.y - pl_transform_comp->transform.translation.y) < 3.9f
				&& fabs(transform_comp->transform.translation.z - pl_transform_comp->transform.translation.z) < 1.9f)
			{
				pl_transform_comp->transform.translation.z = 8.0f;
			}
		}
		

		transform_t move;
		transform_identity(&move);
		float speed = -8.0f;
		if (traffic_comp->row == 1)
		{
			speed = 6.0f;
		}
		move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), speed * dt));
		transform_multiply(&transform_comp->transform, &move);
	}
}
static void draw_models(frogger_game_t* game)
{
	uint64_t k_camera_query_mask = (1ULL << game->camera_type);
	for (ecs_query_t camera_query = ecs_query_create(game->ecs, k_camera_query_mask);
		ecs_query_is_valid(game->ecs, &camera_query);
		ecs_query_next(game->ecs, &camera_query))
	{
		camera_component_t* camera_comp = ecs_query_get_component(game->ecs, &camera_query, game->camera_type);

		uint64_t k_model_query_mask = (1ULL << game->transform_type) | (1ULL << game->model_type);
		for (ecs_query_t query = ecs_query_create(game->ecs, k_model_query_mask);
			ecs_query_is_valid(game->ecs, &query);
			ecs_query_next(game->ecs, &query))
		{
			transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
			model_component_t* model_comp = ecs_query_get_component(game->ecs, &query, game->model_type);
			ecs_entity_ref_t entity_ref = ecs_query_get_entity(game->ecs, &query);

			struct
			{
				mat4f_t projection;
				mat4f_t model;
				mat4f_t view;
			} uniform_data;
			uniform_data.projection = camera_comp->projection;
			uniform_data.view = camera_comp->view;
			transform_to_matrix(&transform_comp->transform, &uniform_data.model);
			gpu_uniform_buffer_info_t uniform_info = { .data = &uniform_data, sizeof(uniform_data) };

			render_push_model(game->render, &entity_ref, model_comp->mesh_info, model_comp->shader_info, &uniform_info);
		}
	}
}
