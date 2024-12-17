#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "tls.h"

typedef struct thread_local_storage
{
    pthread_t tid;
    unsigned int size; /* size in bytes */
    unsigned int page_num; /* number of pages */
    struct page **pages; /* array of pointers to pages */
} TLS;

struct page {
    void* address; /* start address of page */
    int ref_count; /* counter for shared pages */
};

TLS tls_table[128];
int init = 0;
int page_size;

static int tls_thread_count = 0;
int get_tls_index(pthread_t tid) {
    int i;
    for (i = 0; i < tls_thread_count; i++) {
        if (tls_table[i].tid == tid) {
            return i;
        }
    }
    if (tls_thread_count >= 128) {
        fprintf(stderr, "Maximum thread limit reached\n");
        exit(1);
    }
    return tls_thread_count++;
}

void tls_handle_page_fault(int sig, siginfo_t *si, void *context);

void tls_init()
{
    struct sigaction sigact;
    /* get the size of a page */
    page_size = getpagesize();
    /* install the signal handler for page faults (SIGSEGV, SIGBUS) */
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_SIGINFO; /* use extended signal handling */
    sigact.sa_sigaction = tls_handle_page_fault;
    sigaction(SIGBUS, &sigact, NULL);
    sigaction(SIGSEGV, &sigact, NULL);
    init = 1;
}

void tls_handle_page_fault(int sig, siginfo_t *si, void *context) {
    void* p_fault = (void*)((uintptr_t)si->si_addr & ~(page_size - 1));
    int i, j;
    for (i = 0; i < 128; i++) {
        if (tls_table[i].size == 0) {
            continue;
        }
        for (j = 0; j < tls_table[i].page_num; j++) {
            if (tls_table[i].pages[j]->address == p_fault) {
                fprintf(stderr, "Page fault handled\n");
                pthread_exit(NULL);
            }
        }
    }
    signal(SIGSEGV, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    raise(sig);
}

void tls_unprotect(struct page *p)
{
    if (mprotect(p->address, getpagesize(), PROT_READ | PROT_WRITE)) {
        perror("mprotect failed");
        exit(1);
    }
}

void tls_protect(struct page *p)
{
    if (mprotect((void *)p->address, getpagesize(), 0)) {
        perror("mprotect failed");
        exit(1);
    }
}

int tls_create(unsigned int size) {
    if (init == 0) {
        tls_init();
    }
    int current_thread = get_tls_index(pthread_self());
    // check if tls already exists
    if (tls_table[current_thread].size > 0) {
        printf("TLS already exists for this thread\n");
        return -1;
    }
    tls_table[current_thread].size = size;
    tls_table[current_thread].tid = pthread_self();
    tls_table[current_thread].page_num = (size / getpagesize()) + 1;
    tls_table[current_thread].pages = (struct page **) calloc(tls_table[current_thread].page_num, sizeof(struct page *));
    int i;
    for (i = 0; i < tls_table[current_thread].page_num; i++) {
        struct page *p = calloc(1, sizeof(struct page));

        p->address = mmap(0, getpagesize(), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        p->ref_count = 1;
        tls_table[current_thread].pages[i] = p;
    }
    return 0; 
}

int tls_destroy() {
    int current_thread = get_tls_index(pthread_self());
    if (tls_table[current_thread].size == 0) {
        printf("No TLS exists for this thread\n");
        return -1;
    }
    int i;
    for (i = 0; i < tls_table[current_thread].page_num; i++) {
        if (tls_table[current_thread].pages[i]->ref_count > 1) {
            tls_table[current_thread].pages[i]->ref_count--;
        } else {
            munmap((void *)tls_table[current_thread].pages[i]->address, getpagesize());
            free(tls_table[current_thread].pages[i]);
        }
    }
    free(tls_table[current_thread].pages);
    tls_table[current_thread].size = 0;
    tls_table[current_thread].tid = -1;
    tls_table[current_thread].page_num = 0;
    return 0;
}

int tls_write(unsigned int offset, unsigned int length, char *buffer) {
    int current_thread = get_tls_index(pthread_self());
    if (offset + length > tls_table[current_thread].size || tls_table[current_thread].size == 0) {
        fprintf(stderr, "Write out of bounds\n");
        return -1;
    }
    int i;
    for (i = 0; i < tls_table[current_thread].page_num; i++) {
        tls_unprotect(tls_table[current_thread].pages[i]);
    }
    int cnt, idx;
    for (cnt = 0, idx = offset; idx < (offset + length); ++cnt, ++idx) {
        unsigned int pn = idx / page_size;
        unsigned int poff = idx % page_size;
        struct page *p = tls_table[current_thread].pages[pn];

        // Handle copy-on-write if page is shared
        if (p->ref_count > 1) {
            struct page *new_page = (struct page *)calloc(1, sizeof(struct page));
            if (!new_page) {
                perror("Failed to allocate new page");
                exit(1);
            }
            new_page->address = mmap(0, page_size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
            if (new_page->address == MAP_FAILED) {
                perror("mmap failed");
                exit(1);
            }

            memcpy(new_page->address, p->address, page_size);
            new_page->ref_count = 1;
            tls_table[current_thread].pages[pn] = new_page;
            p->ref_count--;
        }
        p = tls_table[current_thread].pages[pn];
        char *dst = ((char *)p->address) + poff;
        *dst = buffer[cnt];
    }

    for (i = 0; i < tls_table[current_thread].page_num; i++) {
        tls_protect(tls_table[current_thread].pages[i]);
    }
    return 0;
}


int tls_read(unsigned int offset, unsigned int length, char *buffer) {
    int current_thread = get_tls_index(pthread_self());
    if (offset + length > tls_table[current_thread].size || tls_table[current_thread].size == 0) {
        return -1;
    }

    int i;
    for(i = 0; i < tls_table[current_thread].page_num; i++) {
        tls_unprotect(tls_table[current_thread].pages[i]);
    }

    int cnt, idx;
    for (cnt = 0, idx = offset; idx < (offset + length); ++cnt, ++idx) {
        struct page *p;
        unsigned int pn, poff;
        pn = idx / page_size;
        poff = idx % page_size;
        p = tls_table[current_thread].pages[pn];
        char* src = ((char *)(unsigned long int)p->address) + poff;
        buffer[cnt] = *src;
    }

    for(i = 0; i < tls_table[current_thread].page_num; i++) {
        tls_protect(tls_table[current_thread].pages[i]);
    }
    return 0;
}

int tls_clone(pthread_t tid) {
    int current_thread = get_tls_index(pthread_self());
    int target_thread = get_tls_index(tid);
    if (tls_table[current_thread].size > 0) {
        fprintf(stderr, "TLS already exists for this thread\n");
        return -1;
    }
    if (tls_table[target_thread].size == 0) {
        fprintf(stderr, "No TLS exists for the thread to be cloned\n");
        return -1;
    }
    tls_table[current_thread].size = tls_table[target_thread].size;
    tls_table[current_thread].tid = pthread_self();
    tls_table[current_thread].page_num = tls_table[target_thread].page_num;
    tls_table[current_thread].pages = (struct page **) calloc(tls_table[current_thread].page_num, sizeof(struct page *));
    int i;
    for (i = 0; i < tls_table[current_thread].page_num; i++) {
        struct page *target_page = tls_table[target_thread].pages[i];
        target_page->ref_count++;
        tls_table[current_thread].pages[i] = target_page;
        tls_protect(target_page);
    }
    return 0;
}