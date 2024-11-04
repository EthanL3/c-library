#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include "ec440threads.h"
#include <semaphore.h>
#define JB_RBX 0
#define JB_RBP 1
#define JB_R12 2
#define JB_R13 3
#define JB_R14 4
#define JB_R15 5
#define JB_RSP 6
#define JB_PC 7
#define READY 0
#define EXITED -1
#define RUNNING 1
#define BLOCKED 2

typedef struct {
    pthread_t id;
    jmp_buf context;
    void *stack_pointer;
    int status; // 0: ready, 1: running, -1: exited, 2: blocked
    void *exit_value;
    int waiting_on; // threads that are BLOCKED (waiting) for this thread to finish
} tcb;

typedef struct {
    int value;
    int initialized;
    int waiting_threads[128];
    int wait_count;   
    
} my_sem_t;

tcb thread_table[128];
int current_thread = -1;
int total_thread_count = 0;

sigset_t alarm_mask;

void schedule();

void lock() {
    sigemptyset(&alarm_mask);
    sigaddset(&alarm_mask, SIGALRM);
    sigprocmask(SIG_BLOCK, &alarm_mask, NULL);
}

void unlock() {
    sigprocmask(SIG_UNBLOCK, &alarm_mask, NULL);
}

void pthread_exit_wrapper() {
    unsigned long int res;
    asm("movq %%rax, %0\n":"=r"(res));
    pthread_exit((void *) res);
}

void init_thread_sys() {
    int i;
    for(i = 0; i < 128; i++) {
        thread_table[i].status = EXITED;
    }
    current_thread = 0;
    int new_thread_id = 0;
    thread_table[current_thread].id = (pthread_t)(unsigned long)new_thread_id;
    thread_table[current_thread].status = RUNNING;
    thread_table[current_thread].stack_pointer = NULL;
    thread_table[current_thread].waiting_on = -1;
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
        if (thread_table[i].status == EXITED) {
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
    thread_table[new_thread_id].status = READY;
    thread_table[new_thread_id].waiting_on = -1; // not waiting on any thread
    if (setjmp(thread_table[new_thread_id].context) == 0) {
        unsigned long int *stack_top = (unsigned long int *)((char *)thread_table[new_thread_id].stack_pointer + 32767);
        stack_top = (unsigned long int *)((unsigned long int)stack_top & ~0xF);
        *(--stack_top) = (unsigned long int)pthread_exit_wrapper;
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
    lock();
    thread_table[current_thread].exit_value = value_ptr;
    thread_table[current_thread].status = EXITED;
    if(thread_table[current_thread].waiting_on != -1) {
        thread_table[thread_table[current_thread].waiting_on].status = READY;
    }
    free(thread_table[current_thread].stack_pointer);
    unlock();
    schedule();
    exit(0);
}

pthread_t pthread_self(void) {
    return thread_table[current_thread].id;
}

int pthread_join(pthread_t thread, void **value_ptr) {
    lock();
    int target = (int)(unsigned long)thread;
    if (thread_table[target].status == EXITED) {
        if (value_ptr) {
            *value_ptr = thread_table[target].exit_value;
        }
        unlock();
        return 0;
    }
    thread_table[current_thread].status = BLOCKED;
    thread_table[target].waiting_on = current_thread;
    unlock();
    schedule();
    lock();
    if (value_ptr) {
        *value_ptr = thread_table[target].exit_value;
    }
    unlock();
    return 0;
}

void schedule() {
    if (setjmp(thread_table[current_thread].context) == 0) {
        lock();
        if (thread_table[current_thread].status == RUNNING) {
            thread_table[current_thread].status = READY;
        }
        do {
            current_thread = (current_thread + 1) % 128;
        } while (thread_table[current_thread].status != READY);
        thread_table[current_thread].status = RUNNING;
        unlock();
        longjmp(thread_table[current_thread].context, 1);
    }
}

int sem_init(sem_t *sem, int pshared, unsigned value) {
    my_sem_t *my_sem = (my_sem_t *)malloc(sizeof(my_sem_t));
    if (my_sem == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for semaphore\n");
        return -1;
    }

    my_sem->value = value;
    my_sem->initialized = 1;
    my_sem->wait_count = 0;
    *((my_sem_t **)&sem->__align) = my_sem; // Store custom semaphore in __align
    return 0;
}

// Decrements the semaphore (locks it)
int sem_wait(sem_t *sem) {
    my_sem_t *my_sem = *((my_sem_t **)&sem->__align);

    lock();
    // If value is 0, block the current thread
    if (my_sem->value == 0) {
        my_sem->waiting_threads[my_sem->wait_count++] = current_thread;
        thread_table[current_thread].status = BLOCKED;

        unlock();
        raise(SIGALRM);
    } else {
        my_sem->value--;  // Acquire the semaphore
        unlock();
    }
    return 0;
}

int sem_post(sem_t *sem) {
    my_sem_t *my_sem = *((my_sem_t **)&sem->__align);

    lock();

    my_sem->value++;
    if (my_sem->wait_count > 0) {
        int next_thread = my_sem->waiting_threads[0];
        thread_table[next_thread].status = READY;
        int i;
        for(i = 1; i < my_sem->wait_count; i++) {
            my_sem->waiting_threads[i - 1] = my_sem->waiting_threads[i];
        }
        my_sem->wait_count--;
    }
    else {
        my_sem->value++;
    }
    unlock();
    return 0;
}

// Destroys the semaphore
int sem_destroy(sem_t *sem) {
    my_sem_t *my_sem = *((my_sem_t **)&sem->__align);
    if (!my_sem->initialized) {
        fprintf(stderr, "Error: Semaphore not initialized\n");
        return -1;
    }

    free(my_sem);
    my_sem = NULL;
    return 0;
}