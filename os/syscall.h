#ifndef SYSCALL_H
#define SYSCALL_H

#include "proc.h"

void syscall();
uint64 sys_gettimeofday(uint64 va_val, int _tz);
uint64 sys_task_info(uint64 va_ti);

uint64 sys_mmap(uint64 start, uint64 len, int port, int flag, int fd);
uint64 sys_munmap(uint64 start, uint64 len);

#endif // SYSCALL_H
