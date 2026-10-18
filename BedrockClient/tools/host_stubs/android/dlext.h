#pragma once
struct android_dlextinfo { unsigned long long flags; void* reserved_addr; unsigned long reserved_size; int relro_fd; int library_fd; };
#define ANDROID_DLEXT_USE_LIBRARY_FD 0x10
void* android_dlopen_ext(const char*, int, const android_dlextinfo*);
