#include "queue.h"

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>

typedef struct node {
    void *value;
    struct node *next;
} node_t;

typedef struct queue {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    node_t *root;
    node_t *end;
} queue_t;

queue_t *queue_init(void) {
    queue_t *queue = (queue_t *) malloc(sizeof(queue_t));
    assert(queue != NULL);
    assert(!pthread_mutex_init(&queue->mutex, NULL));
    assert(!pthread_cond_init(&queue->cond, NULL));

    queue->root = NULL;
    queue->end = NULL;
    return queue;
}

void queue_enqueue(queue_t *queue, void *value) {
    node_t *new_node = (node_t *) malloc(sizeof(node_t));
    assert(new_node != NULL);

    new_node->value = value;
    new_node->next = NULL;

    assert(!pthread_mutex_lock(&queue->mutex));

    if (queue->root == NULL) { // size = 0
        queue->root = new_node;
        queue->end = new_node;
    }
    else { // size > 0
        queue->end->next = new_node;
        queue->end = new_node;
    }

    assert(!pthread_cond_signal(&queue->cond));
    assert(!pthread_mutex_unlock(&queue->mutex));
}

void *queue_dequeue(queue_t *queue) {
    assert(!pthread_mutex_lock(&queue->mutex));
    while (queue->root == NULL) { // size = 0
        assert(!pthread_cond_wait(&queue->cond, &queue->mutex));
    }

    void *val = queue->root->value;
    if (queue->root->next == NULL) { // size = 1
        free(queue->root);
        queue->root = NULL;
        queue->end = NULL;
    }
    else { // size > 1
        node_t *node = queue->root;
        queue->root = queue->root->next;
        free(node);
    }

    assert(!pthread_mutex_unlock(&queue->mutex));
    return val;
}

void queue_free(queue_t *queue) {
    assert(!pthread_mutex_destroy(&queue->mutex));
    assert(!pthread_cond_destroy(&queue->cond));
    free(queue);
}