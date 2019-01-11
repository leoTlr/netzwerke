#include <stdlib.h>
#include <stdio.h>

#include "tidstack.h"

// alloc new array for tid's
void tidstack_init(tidstack_t* stack_ptr) {

    pthread_t* tid_arr = (pthread_t*)malloc(TIDSTACK_INITSIZE*sizeof(pthread_t));
    if (!tid_arr) {
        perror("tidstack_init() : malloc");
        exit(EXIT_FAILURE);
    }

    stack_ptr->tid_arr = tid_arr;
    stack_ptr->nr_elems = 0;
    stack_ptr->max_elems = TIDSTACK_INITSIZE;
}

void tidstack_destroy(tidstack_t* stack_ptr) {
    if (stack_ptr->tid_arr)
        free(stack_ptr->tid_arr);
    stack_ptr->max_elems = 0;
    stack_ptr->nr_elems = 0;
}

// grow/shrink arr (eventually copy content to new address)
void tidstack_resize(tidstack_t* stack_ptr, size_t new_nr_elems) {

    pthread_t* tid_arr_new = (pthread_t *)realloc(stack_ptr->tid_arr, new_nr_elems*sizeof(pthread_t));
    if (!tid_arr_new) {
        tidstack_destroy(stack_ptr);
        perror("tidstack_resize() : realloc");
        exit(EXIT_FAILURE);
    }

    stack_ptr->tid_arr = tid_arr_new;
    stack_ptr->max_elems = new_nr_elems;
}

// push tid (resize if needed)
void tidstack_push(tidstack_t* stack_ptr, pthread_t tid) {

    if (stack_ptr->nr_elems+1 >= stack_ptr->max_elems)
        tidstack_resize(stack_ptr, stack_ptr->max_elems*TIDSTACK_GROWFACTOR);

    stack_ptr->tid_arr[++(stack_ptr->nr_elems)] = tid;
}

// pop and possibly shrink arr (ret ULONG_MAX if empty)
pthread_t tidstack_pop(tidstack_t* stack_ptr) {

    if (stack_ptr->nr_elems == 0) return 0;
    
    if (stack_ptr->nr_elems < (stack_ptr->max_elems/2))
        if (stack_ptr->max_elems*TIDSTACK_SHRINKFACTOR >= TIDSTACK_INITSIZE)
            tidstack_resize(stack_ptr, stack_ptr->max_elems*TIDSTACK_SHRINKFACTOR);

    return stack_ptr->tid_arr[(stack_ptr->nr_elems)--];
}