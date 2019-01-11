#ifndef TIDSTACK_H
#define TIDSTACK_H

#include <pthread.h>

// simple stack data structure to store thread id's for being able to join() them at shutdown

#define TIDSTACK_INITSIZE 20
#define TIDSTACK_GROWFACTOR 2.0F
#define TIDSTACK_SHRINKFACTOR 0.75F

typedef struct {
    pthread_t* tid_arr;
    size_t nr_elems;
    size_t max_elems;
} tidstack_t;

void tidstack_init(tidstack_t* stack_ptr);
void tidstack_destroy(tidstack_t* stack_ptr);
void tidstack_resize(tidstack_t* stack_ptr, size_t new_size);
void tidstack_push(tidstack_t* stack_ptr, pthread_t tid);
pthread_t tidstack_pop(tidstack_t* stack_ptr);

#endif // TIDSTACK_H