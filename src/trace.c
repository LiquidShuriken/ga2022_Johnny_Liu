#include "trace.h"
#include "heap.h"
#include "mutex.h"
#include "queue.h"
#include "timer_object.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct event_info_t
{
	heap_t* heap;
	char* name;
	DWORD tid;
} event_info_t;

typedef struct trace_t
{
	heap_t* heap;
	queue_t* event_queue;
	timer_object_t* timer;
	mutex_t* mutex;
	char* event_buffer;
	char* path;
	size_t event_capacity;
	bool capturing;
} trace_t;

trace_t* trace_create(heap_t* heap, int event_capacity)
{
	trace_t* trace = heap_alloc(heap, sizeof(trace_t), 8);
	trace->heap = heap;
	trace->event_queue = queue_create(heap, event_capacity);
	trace->timer = timer_object_create(heap, NULL);
	trace->mutex = mutex_create(heap);
	trace->event_capacity = (size_t)event_capacity;
	trace->event_buffer = calloc(trace->event_capacity * 128, sizeof(char));
	trace->capturing = false;
	return trace;
}

void trace_destroy(trace_t* trace)
{
	queue_push(trace->event_queue, NULL);
	queue_destroy(trace->event_queue);
	timer_object_destroy(trace->timer);
	mutex_destroy(trace->mutex);
	free(trace->event_buffer);
	heap_free(trace->heap, trace);
}

void trace_duration_push(trace_t* trace, const char* name)
{
	timer_object_update(trace->timer);
	event_info_t* e = event_info_create(trace->heap, name);
	queue_push(trace->event_queue, e);
	if (trace->capturing)
	{
		char info[128];
		get_event_info(e, 'B', timer_object_get_ms(trace->timer), info);
		mutex_lock(trace->mutex);
		strncat_s(trace->event_buffer, strlen(trace->event_buffer) + strlen(info) + 1, info, strlen(info));
		mutex_unlock(trace->mutex);
	}
}

void trace_duration_pop(trace_t* trace)
{
	timer_object_update(trace->timer);
	event_info_t* e = queue_pop(trace->event_queue);
	if (trace->capturing)
	{
		char info[128];
		get_event_info(e, 'E', timer_object_get_ms(trace->timer), info);
		mutex_lock(trace->mutex);
		strncat_s(trace->event_buffer, trace->event_capacity * 128, info, strlen(info));
		mutex_unlock(trace->mutex);
	}

	event_info_destroy(e);
}

void trace_capture_start(trace_t* trace, const char* path)
{
	trace->path = calloc(strlen(path) + 1, sizeof(char));
	strncpy_s(trace->path, strlen(path) + 1, path, strlen(path));
	char* header = "{\n\t\"displayTimeUnit\": \"ns\", \"traceEvents\" : [\n";
	mutex_lock(trace->mutex);
	strncat_s(trace->event_buffer, strlen(trace->event_buffer) + strlen(header) + 1, header, strlen(header));
	mutex_unlock(trace->mutex);
	trace->capturing = true;
}

void trace_capture_stop(trace_t* trace)
{
	trace->capturing = false;
	char* tail = "\t]\n}";
	mutex_lock(trace->mutex);
	strncat_s(trace->event_buffer, strlen(trace->event_buffer) + strlen(tail) + 1, tail, strlen(tail));

	wchar_t wide_path[1024];
	if (MultiByteToWideChar(CP_UTF8, 0, trace->path, -1, wide_path, sizeof(wide_path)) <= 0)
	{
		perror("File path conversion failed");
		return;
	}

	HANDLE handle = CreateFile(wide_path, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE)
	{
		perror("File creation failed");
		return;
	}

	if (!WriteFile(handle, trace->event_buffer, (DWORD)strlen(trace->event_buffer), NULL, NULL))
	{
		perror("Write failed");
		return;
	}
	CloseHandle(handle);
	mutex_unlock(trace->mutex);
}

event_info_t* event_info_create(heap_t* heap, const char* name)
{
	event_info_t* event = heap_alloc(heap, sizeof(event_info_t), 8);
	event->heap = heap;
	event->name = calloc(strlen(name) + 1, sizeof(char));
	strncpy_s(event->name, strlen(name) + 1, name, strlen(name));
	event->tid = GetCurrentThreadId();
	return event;
}

void event_info_destroy(event_info_t* event)
{
	free(event->name);
	heap_free(event->heap, event);
}

void get_event_info(event_info_t* event, char ph, unsigned int tms, char* info)
{
	snprintf(info, 128, "\t\t{\"name\":\"%s\",\"ph\":\"%c\",\"pid\":0,\"tid\":\"%d\",\"ts\":\"%d\"},\n",
		event->name, ph, event->tid, tms);
}