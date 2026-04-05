#include "syscall.h"
#include "console.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include "timer.h"
#include "vm.h"

struct stat {
    int dev;
    uint ino;
    short type;
    short nlink;
    uint64 size;
};

typedef struct {
    int status;
    uint32 syscall_times[MAX_SYSCALL_NUM];
    uint64 time;
} TaskInfo;

uint64 console_write(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	tracef("write size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return len;
}

uint64 console_read(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	tracef("read size = %d", len);
	for (int i = 0; i < len; ++i) {
		int c = consgetc();
		str[i] = c;
	}
	copyout(p->pagetable, va, str, len);
	return len;
}

uint64 sys_write(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd >= FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_write(va, len);
	case FD_INODE:
		return inodewrite(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
}

uint64 sys_read(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd >= FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_read(va, len);
	case FD_INODE:
		return inoderead(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(uint64 va_val, int _tz) {
    struct proc *p = curr_proc();

    // Translate virtual address to physical address 
    TimeVal *pa_val = (TimeVal *)useraddr(p->pagetable, va_val);
    if (pa_val == NULL) return -1;

    uint64 cycle = get_cycle();
    pa_val->sec = cycle / CPU_FREQ;
    pa_val->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
    return 0;
}

// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
/*
* LAB1: you may need to define sys_task_info here
*/
uint64 sys_task_info(uint64 va_ti) {
    struct proc *p = curr_proc();
    
    TaskInfo *pa_ti = (TaskInfo *)useraddr(p->pagetable, va_ti);
    if (pa_ti == NULL) return -1;

    // Map internal kernel state to TaskStatus
    if (p->state == RUNNING) {
        pa_ti->status = 2;
    } else if (p->state == RUNNABLE) {
        pa_ti->status = 1;
    } else if (p->state == ZOMBIE) {
        pa_ti->status = 3;
    } else {
        pa_ti->status = 0;
    }

    pa_ti->status = p->state;
    
    for (int i = 0; i < MAX_SYSCALL_NUM; i++) {
        pa_ti->syscall_times[i] = p->syscall_counts[i]; 
    }
    
    pa_ti->time = (get_cycle() - p->start_time) / (CPU_FREQ / 1000); 
    return 0;
}

uint64 sys_mmap(uint64 start, uint64 len, int port, int flag, int fd) {
    if (len == 0) return 0; 
    
    if (len > (1024 * 1024 * 1024)) return -1;
    if ((port & ~0x7) != 0 || (port & 0x7) == 0) return -1;

    if (start % PGSIZE != 0) return -1;

    struct proc *p = curr_proc();
    uint64 va = PGROUNDDOWN(start);
    uint64 end = PGROUNDUP(start + len);
    
    for (uint64 a = va; a < end; a += PGSIZE) {
        if (walkaddr(p->pagetable, a) != 0) {
            return -1; 
        }
    }

    int perm = PTE_U | PTE_V | (port << 1); 

    for (uint64 a = va; a < end; a += PGSIZE) {
        char *mem = kalloc(); 
        if (mem == 0) {
            sys_munmap(va, a - va); 
            return -1;
        }
        memset(mem, 0, PGSIZE);
        if (mappages(p->pagetable, a, PGSIZE, (uint64)mem, perm) != 0) {
            kfree(mem);
            sys_munmap(va, a - va);
            return -1;
        }
    }

    uint64 new_max_page = end / PGSIZE;
    if (new_max_page > p->max_page) {
        p->max_page = new_max_page;
    }

    return 0; 
}

uint64 sys_munmap(uint64 start, uint64 len) {
    if (len == 0) return 0;

    // Reject unaligned start addresses
    if (start % PGSIZE != 0) return -1;
    
    struct proc *p = curr_proc();
    uint64 va = PGROUNDDOWN(start);
    uint64 end = PGROUNDUP(start + len);

    for (uint64 a = va; a < end; a += PGSIZE) {
        if (walkaddr(p->pagetable, a) == 0) {
            return -1; 
        }
    }

    uvmunmap(p->pagetable, va, (end - va) / PGSIZE, 1);
    return 0; 
}

uint64 sys_getpid()
{
	return curr_proc()->pid;
}

uint64 sys_getppid()
{
	struct proc *p = curr_proc();
	return p->parent == NULL ? IDLE_PID : p->parent->pid;
}

uint64 sys_clone()
{
	debugf("fork!");
	return fork();
}

static inline uint64 fetchaddr(pagetable_t pagetable, uint64 va)
{
	uint64 *addr = (uint64 *)useraddr(pagetable, va);
	return *addr;
}

uint64 sys_exec(uint64 path, uint64 uargv)
{
	struct proc *p = curr_proc();
	char name[MAX_STR_LEN];
	copyinstr(p->pagetable, name, path, MAX_STR_LEN);
	uint64 arg;
	static char strpool[MAX_ARG_NUM][MAX_STR_LEN];
	char *argv[MAX_ARG_NUM];
	int i;
	for (i = 0; uargv && (arg = fetchaddr(p->pagetable, uargv));
	     uargv += sizeof(char *), i++) {
		copyinstr(p->pagetable, (char *)strpool[i], arg, MAX_STR_LEN);
		argv[i] = (char *)strpool[i];
	}
	argv[i] = NULL;
	return exec(name, (char **)argv);
}

uint64 sys_wait(int pid, uint64 va)
{
	struct proc *p = curr_proc();
	int *code = (int *)useraddr(p->pagetable, va);
	return wait(pid, code);
}

uint64 sys_spawn(uint64 va)
{
    struct proc *p = curr_proc();
    char name[200];
    
    // Get Target Program path from user space
    if (copyinstr(p->pagetable, name, va, 200) < 0) {
        return -1;
    }
    
    // Find the file in the file system
    struct inode *ip;
    if ((ip = namei(name)) == 0) {
        return -1; // File not found
    }
    
    // Allocate a new process
    struct proc *np = allocproc();
    if (np == 0) {
        iput(ip);
        return -1; 
    }
    
    // Load the binary from the file into the new process
    if (bin_loader(ip, np) < 0) {
        freeproc(np);
        iput(ip);
        return -1;
    }
    
    // Release the inode after successfully loading
    iput(ip); 
    
    // Inherit file descriptors from the parent process
    for (int i = 0; i < FD_BUFFER_SIZE; i++) {
        if (p->files[i] != NULL) {
            p->files[i]->ref++;
            np->files[i] = p->files[i];
        }
    }

    // Initialize the new process's stack with arguments
    char *argv[2];
    argv[0] = name;
    argv[1] = NULL;
    np->trapframe->a0 = push_argv(np, argv);
    
    // Setup parent relationship and add to scheduler
    np->parent = p;
    np->state = RUNNABLE;
    add_task(np);
    
    return np->pid;
}

uint64 sys_set_priority(long long prio)
{
	// TODO: your job is to complete the sys call
	return -1;
	if (prio < 2) return -1;
    
    struct proc *p = curr_proc();
    p->priority = prio;
    p->pass = BIG_STRIDE / p->priority;
    
    return prio;
}

uint64 sys_openat(uint64 va, uint64 omode, uint64 _flags)
{
	struct proc *p = curr_proc();
	char path[200];
	copyinstr(p->pagetable, path, va, 200);
	return fileopen(path, omode);
}

uint64 sys_close(int fd)
{
	if (fd < 0 || fd >= FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d", fd);
		return -1;
	}
	fileclose(f);
	p->files[fd] = 0;
	return 0;
}

int sys_linkat(int olddirfd, uint64 oldpath, int newdirfd, uint64 newpath, uint64 flags) {
    // This system call is responsible for creating a new hard link to an existing file.
    // Hard link creates a new filename that points to that exact same inode.
    char old_name[200], new_name[200];
    struct proc *p = curr_proc();

    // Fetch arguments from user space
    if (copyinstr(p->pagetable, old_name, oldpath, 200) < 0) return -1;
    if (copyinstr(p->pagetable, new_name, newpath, 200) < 0) return -1;

    // Linking a file with the same name
    if (strncmp(old_name, new_name, 200) == 0) return -1;

    struct inode *dp = root_dir();
    struct inode *ip;

    // Find the original file
    if ((ip = dirlookup(dp, old_name, 0)) == 0) {
        iput(dp);
        return -1;
    }

    ivalid(ip);

    // Increment link count and sync
    ip->nlink++;
    iupdate(ip);

    // Create a new directory entry
    if (dirlink(dp, new_name, ip->inum) < 0) {
        ip->nlink--;
        iupdate(ip);
        iput(ip);
        iput(dp);
        return -1;
    }

    // Release inodes (Does not free, just reduces ref count)
    iput(ip);
    iput(dp);
    return 0;
}

int sys_unlinkat(int dirfd, uint64 path, uint64 flags) {
    char name[200];
    struct proc *p = curr_proc();

    if (copyinstr(p->pagetable, name, path, 200) < 0) return -1;

    struct inode *dp = root_dir();
    struct inode *ip;
    uint off;

    // Find the file and its offset in the directory
    if ((ip = dirlookup(dp, name, &off)) == 0) {
        iput(dp);
        return -1; // File does not exist
    }

    ivalid(ip);

    // Clear the directory entry
    struct dirent de;
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) {
        panic("sys_unlinkat: dir read error");
    }
    
    de.inum = 0; // Mark entry as empty
    if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) {
        panic("sys_unlinkat: dir write error");
    }

    // Decrement the link count
    ip->nlink--;
    iupdate(ip);

    // Release the inode. If nlink == 0 and this is the last reference
    iput(ip); 
    iput(dp);
    
    return 0;
}

struct kstat {
    uint64 dev;
    uint64 ino;
    uint32 mode;
    uint32 nlink;
    uint64 pad[7];
};

int sys_fstat(int fd, uint64 stat) {
    // Looks at open file and returns some metadata about it, such as size, type, and link count.
    if (fd < 0 || fd >= FD_BUFFER_SIZE) return -1;
    
    struct proc *p = curr_proc();
    struct file *f = p->files[fd];
    
    if (f == NULL) return -1;

    struct kstat st;
    memset(&st, 0, sizeof(st));

    if (f->type == FD_INODE) {
        struct inode *ip = f->ip;
        ivalid(ip); // Ensure inode is cached from disk
        st.dev = ip->dev;
        st.ino = ip->inum;
        st.nlink = ip->nlink;
        
        // Map xv6 file types to the rCore stat modes
        if (ip->type == 1) {
            st.mode = 0x040000;
        } else {
            st.mode = 0x100000;
        }
        
        // Store size in the first padding slot just in case
        st.pad[0] = ip->size; 
        
    } else if (f->type == FD_STDIO) {
        st.dev = 0;
        st.ino = 0;
        st.mode = 0x100000;
        st.nlink = 1;
        st.pad[0] = 0;
    } else {
        return -1;
    }

    // Copy to user space with the new 80-byte size
    if (copyout(p->pagetable, stat, (char *)&st, sizeof(st)) < 0) {
        return -1;
    }
    
    return 0;
}

extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);

	 /*
	* LAB1: you may need to update syscall counter for task info here
	*/
	struct proc *p = curr_proc();
	if (id >= 0 && id < MAX_SYSCALL_NUM) {
		p->syscall_counts[id]++;
	}

	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_read:
		ret = sys_read(args[0], args[1], args[2]);
		break;
	case SYS_openat:
		ret = sys_openat(args[0], args[1], args[2]);
		break;
	case SYS_close:
		ret = sys_close(args[0]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday(args[0], (int)args[1]);
		break;
	case SYS_getpid:
		ret = sys_getpid();
		break;
	case SYS_getppid:
		ret = sys_getppid();
		break;
	case SYS_clone: // SYS_fork
		ret = sys_clone();
		break;
	case SYS_execve:
		ret = sys_exec(args[0], args[1]);
		break;
	case SYS_wait4:
		ret = sys_wait(args[0], args[1]);
		break;
	case SYS_fstat:
	    ret = sys_fstat(args[0],args[1]);
		break;
	case SYS_linkat:
	    ret = sys_linkat(args[0],args[1],args[2],args[3],args[4]);
		break;
	case SYS_unlinkat:
	    ret = sys_unlinkat(args[0],args[1],args[2]);
		break;
	case SYS_spawn:
		ret = sys_spawn(args[0]);
		break;
	case SYS_task_info: 
        // Pass the raw virtual address directly
        ret = sys_task_info(args[0]);
        break;
	case SYS_mmap:
		ret = sys_mmap(args[0], args[1], (int)args[2], (int)args[3], (int)args[4]);
		break;
	case SYS_munmap:
		ret = sys_munmap(args[0], args[1]);
		break;
    case SYS_setpriority:
        ret = sys_set_priority(args[0]);
        break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
