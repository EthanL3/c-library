#include <stdio.h>
#include <pthread.h>
#include "tls.h"

void *clone_function(void *arg) {
    pthread_t tid = *(pthread_t *)arg;

    // Clone the TLS of the original thread
    if (tls_clone(tid) != 0) {
        fprintf(stderr, "Failed to clone TLS\n");
        return NULL;
    }

    // Read from the cloned TLS
    char buffer[6] = {0}; // Ensure buffer is null-terminated
    tls_read(0, 5, buffer);
    printf("Cloned thread read: %s\n", buffer); // Should print "Hello"

    return NULL;
}

int main() {
    // Create TLS for the original thread
    if (tls_create(4096) != 0) {
        fprintf(stderr, "Failed to create TLS\n");
        return 1;
    }

    // Write initial data to the TLS
    if (tls_write(0, 5, "Hello") != 0) {
        fprintf(stderr, "Failed to write to TLS\n");
        return 1;
    }

    // Spawn a thread to clone the TLS
    pthread_t clone_tid;
    pthread_t self_tid = pthread_self();
    if (pthread_create(&clone_tid, NULL, clone_function, (void *)&self_tid) != 0) {
        fprintf(stderr, "Failed to create thread\n");
        return 1;
    }

    // Wait for the cloned thread to finish
    pthread_join(clone_tid, NULL);

    // Write new data to the original TLS
    if (tls_write(0, 5, "World") != 0) {
        fprintf(stderr, "Failed to write to original TLS\n");
        return 1;
    }

    // Read from the original TLS
    char buffer[6] = {0}; // Ensure buffer is null-terminated
    tls_read(0, 5, buffer);
    printf("Original thread read: %s\n", buffer); // Should print "World"

    // Destroy the original TLS
    if (tls_destroy() != 0) {
        fprintf(stderr, "Failed to destroy TLS\n");
        return 1;
    }

    return 0;
}