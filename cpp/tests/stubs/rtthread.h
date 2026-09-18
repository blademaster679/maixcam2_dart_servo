#pragma once
#include <stdint.h>
typedef void *rt_thread_t;
extern const char *test_console;
#define RT_NULL ((void*)0)
#define RT_EOK 0
#define RT_TICK_PER_SECOND 100
#define RT_CONSOLE_DEVICE_NAME test_console
unsigned rt_tick_get(void);
void rt_kprintf(const char*,...);
void rt_thread_mdelay(int);
rt_thread_t rt_thread_create(const char*,void(*)(void*),void*,unsigned,unsigned,unsigned);
int rt_thread_startup(rt_thread_t);
int rt_thread_delete(rt_thread_t);
