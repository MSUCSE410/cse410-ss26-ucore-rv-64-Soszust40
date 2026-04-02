#include "queue.h"
#include "defs.h"
#include "proc.h"

extern struct proc pool[];

void init_queue(struct queue *q)
{
	q->front = q->tail = 0;
    q->empty = 1;
}

void push_queue(struct queue *q, int value)
{
	if (!q->empty && q->front == q->tail) {
        panic("queue shouldn't be overflow");
    }
    q->empty = 0;
    q->data[q->tail] = value;
    q->tail = (q->tail + 1) % QUEUE_SIZE;
}

int pop_queue(struct queue *q)
{
	if (q->empty)
        return -1;
        
    int min_i = q->front;
    uint64 min_stride = -1ULL;
    
    // Find the process with the smallest stride
    for (int i = q->front; i != q->tail; i = (i + 1) % QUEUE_SIZE) {
        int pid_idx = q->data[i];
        if (pool[pid_idx].stride < min_stride) {
            min_stride = pool[pid_idx].stride;
            min_i = i;
        }
    }
    
    int best_proc = q->data[min_i];
    
    // Remove the chosen item by shifting elements backward
    for (int i = min_i; i != q->front; ) {
        int prev = (i - 1 + QUEUE_SIZE) % QUEUE_SIZE;
        q->data[i] = q->data[prev];
        i = prev;
    }
    q->front = (q->front + 1) % QUEUE_SIZE;
    
    if (q->front == q->tail)
        q->empty = 1;
        
    // Update the stride of the scheduled process
    pool[best_proc].stride += pool[best_proc].pass;
    
    return best_proc;
}
