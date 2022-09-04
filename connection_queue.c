#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "connection_queue.h"

int connection_queue_init(connection_queue_t *queue) {
    for (int i = 0; i < CAPACITY; i++){
        queue->client_fds[i] = -1;
    }

    queue->length = 0;
    queue->read_idx = 0;
    queue->write_idx = 0;
    queue->shutdown = 0;

    int ret;

    ret = pthread_mutex_init(&queue->lock, NULL);
    if (ret != 0){
        fprintf(stderr, "pthread_mutex_init failed: %s\n", strerror(ret));
        return -1;
    }

    ret = pthread_cond_init(&queue->queue_empty, NULL);
    if (ret != 0){
        fprintf(stderr, "pthread_cond_init failed: %s\n", strerror(ret));
        return -1;
    }
    
    ret = pthread_cond_init(&queue->queue_full, NULL);
    if (ret != 0){
        fprintf(stderr, "pthread_cond_init failed: %s\n", strerror(ret));
        return -1;
    }

    return 0;
}

int connection_enqueue(connection_queue_t *queue, int connection_fd) {
    int ret;
    
    ret = pthread_mutex_lock(&queue->lock);
    if (ret != 0){
        fprintf(stderr, "pthread_mutex_lock failed: %s\n", strerror(ret));
        return -1;
    }

    while (queue->length == CAPACITY){
        ret = pthread_cond_wait(&queue->queue_full, &queue->lock);
        if (ret != 0){
            fprintf(stderr, "pthread_cond_wait failed: %s\n", strerror(ret));
            return -1;
        }

        if (queue->shutdown){
            ret = pthread_mutex_unlock(&queue->lock);
            if (ret != 0){
                fprintf(stderr, "pthread_mutex_unlock failed: %s\n", strerror(ret));
                return -1;
            }
            return -1;
        }
    }

    //Add to queue in a circular fashion.
    queue->client_fds[queue->write_idx] = connection_fd;
    queue->write_idx = (queue->write_idx + 1) % CAPACITY;
    queue->length++;

    ret = pthread_cond_signal(&queue->queue_empty);
    if (ret != 0){
        fprintf(stderr, "pthread_cond_signal failed: %s\n", strerror(ret));
        return -1;
    }

    ret = pthread_mutex_unlock(&queue->lock);
    if (ret != 0){
        fprintf(stderr, "pthread_mutex_unlock failed: %s\n", strerror(ret));
        return -1;
    }

    return 0;
}

int connection_dequeue(connection_queue_t *queue) {
    int ret;
    ret = pthread_mutex_lock(&queue->lock);
    if (ret != 0){
        fprintf(stderr, "pthread_mutex_lock failed: %s\n", strerror(ret));
        return -1;
    }

    while (queue->length == 0){
        ret = pthread_cond_wait(&queue->queue_empty, &queue->lock);
        if (ret != 0){
            fprintf(stderr, "pthread_cond_wait failed: %s\n", strerror(ret));
            return -1;
        }

        if (queue->shutdown){
            ret = pthread_mutex_unlock(&queue->lock);
            if (ret != 0){
                fprintf(stderr, "pthread_mutex_unlock failed: %s\n", strerror(ret));
                return -1;
            }
            return -1;
        }

    }
    
    //Remove from queue in a circular fashion
    int extracted_fd = queue->client_fds[queue->read_idx];
    queue->read_idx = (queue->read_idx + 1) % CAPACITY;    
    queue->length--;

    ret = pthread_cond_signal(&queue->queue_full);
    if (ret != 0){
        fprintf(stderr, "pthread_cond_signal failed: %s\n", strerror(ret));
        return -1;
    }
    
    ret = pthread_mutex_unlock(&queue->lock);
    if (ret != 0){
        fprintf(stderr, "pthread_mutex_unlock failed: %s\n", strerror(ret));
        return -1;
    }

    return extracted_fd;
}

int connection_queue_shutdown(connection_queue_t *queue) {
    int ret;
    
    ret = pthread_mutex_lock(&queue->lock);
    if (ret != 0){
        fprintf(stderr, "pthread_mutex_lock failed: %s\n", strerror(ret));
        return -1;
    }

    queue->shutdown = 1;

    //Broadcast to all threads, threads waiting for queue_empty move on and shutdown
    ret = pthread_cond_broadcast(&queue->queue_empty);
    if (ret != 0){
        fprintf(stderr, "pthread_cond_broadcast failed: %s\n", strerror(ret));
        return -1;
    }

    //Broadcast to all threads, threads waiting for queue_full move on and shutdown.
    //Other threads already shut down so they don't wait.
    ret = pthread_cond_broadcast(&queue->queue_full);
    if (ret != 0){
        fprintf(stderr, "pthread_cond_broadcast failed: %s\n", strerror(ret));
        return -1;
    }

    ret = pthread_mutex_unlock(&queue->lock);
    if (ret != 0){
        fprintf(stderr, "pthread_mutex_unlock failed: %s\n", strerror(ret));
        return -1;
    }

    return 0;
}

int connection_queue_free(connection_queue_t *queue) {
    int ret;

    ret = pthread_mutex_destroy(&queue->lock);
    if (ret != 0){
        fprintf(stderr, "pthread_mutex_destroy failed: %s\n", strerror(ret));
        return -1;
    }
    
    ret = pthread_cond_destroy(&queue->queue_empty);
    if (ret != 0){
        fprintf(stderr, "pthread_cond_destroy failed: %s\n", strerror(ret));
        return -1;
    }
    
    ret = pthread_cond_destroy(&queue->queue_full);
    if (ret != 0){
        fprintf(stderr, "pthread_cond_destroy failed: %s\n", strerror(ret));
        return -1;
    }

    return 0;
}
