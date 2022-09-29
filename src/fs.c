#include "fs.h"

#include "event.h"
#include "heap.h"
#include "queue.h"
#include "thread.h"
#include "lz4/lz4.h"

#include <stdio.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct fs_t
{
	heap_t* heap;
	queue_t* file_queue;
	queue_t* compressed_queue;
	thread_t* file_thread;
	thread_t* compressed_thread;
} fs_t;

typedef struct thread_info_t
{
	heap_t* heap;
	void* fs;
	bool compression_enabled;
} thread_info_t;

typedef enum fs_work_op_t
{
	k_fs_work_op_read,
	k_fs_work_op_write,
} fs_work_op_t;

typedef struct fs_work_t
{
	heap_t* heap;
	fs_work_op_t op;
	char path[1024];
	bool null_terminate;
	bool use_compression;
	void* buffer;
	size_t size;
	event_t* done;
	int result;
} fs_work_t;

static int file_thread_func(thread_info_t* user);

fs_t* fs_create(heap_t* heap, int queue_capacity)
{
	fs_t* fs = heap_alloc(heap, sizeof(fs_t), 8);
	fs->heap = heap;
	fs->file_queue = queue_create(heap, queue_capacity);
	fs->compressed_queue = queue_create(heap, queue_capacity);
	thread_info_t* info_non_compressed = heap_alloc(heap, sizeof(thread_info_t), 8);
	info_non_compressed->heap = heap;
	info_non_compressed->fs = fs;
	info_non_compressed->compression_enabled = false;
	thread_info_t* info_compressed = heap_alloc(heap, sizeof(thread_info_t), 8);
	info_compressed->heap = heap;
	info_compressed->fs = fs;
	info_compressed->compression_enabled = true;
	fs->file_thread = thread_create(file_thread_func, info_non_compressed);
	fs->compressed_thread = thread_create(file_thread_func, info_compressed);
	/*info_non_compressed->fs = NULL;
	info_compressed->fs = NULL;
	heap_free(heap, info_non_compressed);
	heap_free(heap, info_compressed);*/
	return fs;
}

void fs_destroy(fs_t* fs)
{
	queue_push(fs->file_queue, NULL);
	queue_push(fs->compressed_queue, NULL);
	thread_destroy(fs->file_thread);
	thread_destroy(fs->compressed_thread);
	queue_destroy(fs->file_queue);
	queue_destroy(fs->compressed_queue);
	heap_free(fs->heap, fs);
}

fs_work_t* fs_read(fs_t* fs, const char* path, heap_t* heap, bool null_terminate, bool use_compression)
{
	fs_work_t* work = heap_alloc(fs->heap, sizeof(fs_work_t), 8);
	work->heap = heap;
	work->op = k_fs_work_op_read;
	strcpy_s(work->path, sizeof(work->path), path);
	work->buffer = NULL;
	work->size = 0;
	work->done = event_create();
	work->result = 0;
	work->null_terminate = null_terminate;
	work->use_compression = use_compression;
	queue_push(fs->file_queue, work);
	return work;
}

fs_work_t* fs_write(fs_t* fs, const char* path, const void* buffer, size_t size, bool use_compression)
{
	fs_work_t* work = heap_alloc(fs->heap, sizeof(fs_work_t), 8);
	work->heap = fs->heap;
	work->op = k_fs_work_op_write;
	strcpy_s(work->path, sizeof(work->path), path);
	work->buffer = (void*)buffer;
	work->size = size;
	work->done = event_create();
	work->result = 0;
	work->null_terminate = false;
	work->use_compression = use_compression;

	if (use_compression)
	{
		// HOMEWORK 2: Queue file write work on compression queue!
		char compressed_buffer[512];
		int compressed_size = LZ4_compress_default((const char*)buffer, compressed_buffer, (int)strlen(buffer), sizeof(compressed_buffer));
		compressed_buffer[compressed_size] = 0;
		work->buffer = (void*)compressed_buffer;
		queue_push(fs->compressed_queue, work);
	}
	else
	{
		queue_push(fs->file_queue, work);
	}

	return work;
}

bool fs_work_is_done(fs_work_t* work)
{
	return work ? event_is_raised(work->done) : true;
}

void fs_work_wait(fs_work_t* work)
{
	if (work)
	{
		event_wait(work->done);
	}
}

int fs_work_get_result(fs_work_t* work)
{
	fs_work_wait(work);
	return work ? work->result : -1;
}

void* fs_work_get_buffer(fs_work_t* work)
{
	fs_work_wait(work);
	return work ? work->buffer : NULL;
}

size_t fs_work_get_size(fs_work_t* work)
{
	fs_work_wait(work);
	return work ? work->size : 0;
}

void fs_work_destroy(fs_work_t* work)
{
	if (work)
	{
		event_wait(work->done);
		event_destroy(work->done);
		heap_free(work->heap, work);
	}
}

static void file_read(fs_work_t* work)
{
	wchar_t wide_path[1024];
	if (MultiByteToWideChar(CP_UTF8, 0, work->path, -1, wide_path, sizeof(wide_path)) <= 0)
	{
		work->result = -1;
		return;
	}

	HANDLE handle = CreateFile(wide_path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE)
	{
		work->result = GetLastError();
		return;
	}

	if (!GetFileSizeEx(handle, (PLARGE_INTEGER)&work->size))
	{
		work->result = GetLastError();
		CloseHandle(handle);
		return;
	}

	work->buffer = heap_alloc(work->heap, work->null_terminate ? work->size + 1 : work->size, 8);

	DWORD bytes_read = 0;
	if (!ReadFile(handle, work->buffer, (DWORD)work->size, &bytes_read, NULL))
	{
		work->result = GetLastError();
		CloseHandle(handle);
		return;
	}

	work->size = bytes_read;
	if (work->null_terminate)
	{
		((char*)work->buffer)[bytes_read] = 0;
	}

	CloseHandle(handle);

	if (work->use_compression)
	{
		// HOMEWORK 2: Queue file read work on decompression queue!
		char decompress_buffer[1000000];
		LZ4_decompress_safe(work->buffer, decompress_buffer, (int)work->size, sizeof(decompress_buffer));
		decompress_buffer[work->size] = 0;
		work->buffer = (void*)decompress_buffer;
		event_signal(work->done);
	}
	else
	{
		event_signal(work->done);
	}
}

static void file_write(fs_work_t* work)
{
	wchar_t wide_path[1024];
	if (MultiByteToWideChar(CP_UTF8, 0, work->path, -1, wide_path, sizeof(wide_path)) <= 0)
	{
		work->result = -1;
		return;
	}

	HANDLE handle = CreateFile(wide_path, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE)
	{
		work->result = GetLastError();
		return;
	}

	DWORD bytes_written = 0;
	if (!WriteFile(handle, work->buffer, (DWORD)work->size, &bytes_written, NULL))
	{
		work->result = GetLastError();
		CloseHandle(handle);
		return;
	}

	work->size = bytes_written;

	CloseHandle(handle);

	event_signal(work->done);
}

static int file_thread_func(thread_info_t* user)
{
	heap_t* heap = user->heap;
	fs_t* fs = user->fs;
	bool compressed = user->compression_enabled;
	while (true)
	{
		fs_work_t* work;
		if (compressed)
		{
			work = queue_pop(fs->compressed_queue);
		}
		else
		{
			work = queue_pop(fs->file_queue);
		}
		if (work == NULL)
		{
			user->fs = NULL;
			heap_free(heap, user);
			break;
		}
		
		switch (work->op)
		{
		case k_fs_work_op_read:
			file_read(work);
			break;
		case k_fs_work_op_write:
			file_write(work);
			break;
		}
	}
	return 0;
}
