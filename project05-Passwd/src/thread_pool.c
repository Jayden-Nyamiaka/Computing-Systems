#include "thread_pool.h"

#include <pthread.h>
#include <stdlib.h>

typedef struct task {
    work_function_t func;
    void *arg;
} task_t;

typedef struct thread_pool {
    queue_t *queue;
    size_t num_workers;
    pthread_t *workers;
} thread_pool_t;

void *work(void *pool_queue) {
    queue_t *queue = (queue_t *) pool_queue;

    task_t *task = queue_dequeue(queue);
    while (task != NULL) {
        task->func(task->arg);
        free(task);
        task = queue_dequeue(queue);
    }
    return NULL;
}

thread_pool_t *thread_pool_init(size_t num_worker_threads) {
    thread_pool_t *pool = (thread_pool_t *) malloc(sizeof(thread_pool_t));
    pool->queue = queue_init();
    pool->num_workers = num_worker_threads;
    pool->workers = (pthread_t *) malloc(num_worker_threads * sizeof(pthread_t));
    for (size_t pid = 0; pid < num_worker_threads; pid++) {
        pthread_create(&(pool->workers[pid]), NULL, work, pool->queue);
    }
    return pool;
}

void thread_pool_add_work(thread_pool_t *pool, work_function_t function, void *aux) {
    task_t *new_task = (task_t *) malloc(sizeof(task_t));
    new_task->func = function;
    new_task->arg = aux;
    queue_enqueue(pool->queue, new_task);
}

void thread_pool_finish(thread_pool_t *pool) {
    for (size_t pid = 0; pid < pool->num_workers; pid++) {
        queue_enqueue(pool->queue, NULL);
    }

    for (size_t pid = 0; pid < pool->num_workers; pid++) {
        pthread_join(pool->workers[pid], NULL);
    }

    queue_free(pool->queue);
    free(pool->workers);
    free(pool);
}