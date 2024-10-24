#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include "ec440threads.h"
#define JB_RBX 0
#define JB_RBP 1
#define JB_R12 2
#define JB_R13 3
#define JB_R14 4
#define JB_R15 5
#define JB_RSP 6
#define JB_PC 7

typedef struct {
    pthread_t id;
    jmp_buf context;
    void *stack_pointer;
    int status; // 0: ready, 1: running, -1: exited
} tcb;

tcb thread_table[128];
int current_thread = -1;

void schedule();

void init_thread_sys() {
    int i;
    for(i = 0; i < 128; i++) {
        thread_table[i].status = -1;
    }
    current_thread = 0;
    int new_thread_id = 0;
    thread_table[current_thread].id = (pthread_t)(unsigned long)new_thread_id;
    thread_table[current_thread].status = 1;
    thread_table[current_thread].stack_pointer = NULL;

    if (setjmp(thread_table[current_thread].context) == 0) {
    }
}

int pthread_create (pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg) {
    if (current_thread == -1) {
        init_thread_sys();
    }
    int new_thread_id = -1;
    int i;
    for(i = 0; i < 128; i++) {
        if (thread_table[i].status == -1) {
            new_thread_id = i;
            break;
        }
    }
    if (new_thread_id == -1) {
        return -1;
    }

    thread_table[new_thread_id].stack_pointer = malloc(32767);
    if (thread_table[new_thread_id].stack_pointer == NULL) {
        printf("Error: Failed to allocate stack for thread %d\n", new_thread_id);
        return -1;
    }
    thread_table[new_thread_id].id = (pthread_t)(unsigned long)new_thread_id;
    thread_table[new_thread_id].status = 0;

    if (setjmp(thread_table[new_thread_id].context) == 0) {
        unsigned long int *stack_top = (unsigned long int *)((char *)thread_table[new_thread_id].stack_pointer + 32767);
        stack_top = (unsigned long int *)((unsigned long int)stack_top & ~0xF);
        *(--stack_top) = (unsigned long int)pthread_exit;

        thread_table[new_thread_id].context->__jmpbuf[JB_R12] = (unsigned long int)start_routine;
        thread_table[new_thread_id].context->__jmpbuf[JB_R13] = (unsigned long int)arg;
        thread_table[new_thread_id].context->__jmpbuf[JB_RSP] = ptr_mangle((unsigned long int)stack_top);
        thread_table[new_thread_id].context->__jmpbuf[JB_PC]  = ptr_mangle((unsigned long int)start_thunk);

        *thread = thread_table[new_thread_id].id;
        // first time running pthread_create
        if (new_thread_id == 1) {
            struct sigaction sa;
            sa.sa_handler = schedule;
            sa.sa_flags = SA_NODEFER;
            sigaction(SIGALRM, &sa, NULL);
            ualarm(50000, 50000);
        }
        schedule();
        return 0;
    }
    return -1;
}

void pthread_exit(void *value_ptr) {
    thread_table[current_thread].status = -1;
    free(thread_table[current_thread].stack_pointer);
    int i;
    for (i = 0; i < 128; i++) {
        if (thread_table[i].status == 0 || thread_table[i].status == 1) {
            raise(SIGALRM);
        }
    }
    exit(0);
}

pthread_t pthread_self(void) {
    return thread_table[current_thread].id;
}

void schedule() {
    if (setjmp(thread_table[current_thread].context) == 0) {
        if (thread_table[current_thread].status != -1) {
            thread_table[current_thread].status = 0;
        }
        do {
            current_thread = (current_thread + 1) % 128;
        } while (thread_table[current_thread].status != 0);
        thread_table[current_thread].status = 1;
        longjmp(thread_table[current_thread].context, 1);
    }
}
