#ifndef SYSCALL_H
#define SYSCALL_H

#include "proc.h"

void syscall();
uint64 sys_task_info(TaskInfo *ti);

#endif // SYSCALL_H
