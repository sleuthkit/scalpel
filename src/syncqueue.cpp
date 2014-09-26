/*
Copyright (C) 2013, Basis Technology Corp.
Copyright (C) 2007-2011, Golden G. Richard III and Vico Marziale.
Copyright (C) 2005-2007, Golden G. Richard III.
*
Written by Golden G. Richard III and Vico Marziale.
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
*
http://www.apache.org/licenses/LICENSE-2.0
*
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
Thanks to Kris Kendall, Jesse Kornblum, et al for their work
on Foremost. Foremost 0.69 was used as the starting point for
Scalpel, in 2005.
*/

// A fifo queue with synchronization support.

//C++ STL headers
#include <exception>
#include <stdexcept>
#include <string>
#include <sstream>

#include "syncqueue.h"

static void *dequeue(syncqueue_t * q);


// synchronized wrapper for getting out of queue.
void *get(syncqueue_t * queue) {

    void *elem;
    pthread_mutex_lock(queue->mut);
    while (queue->empty) {
        //              printf("queue %s EMPTY.\n", queue->qname);
        pthread_cond_wait(queue->notEmpty, queue->mut);
    }
    elem = dequeue(queue);
    pthread_mutex_unlock(queue->mut);
    pthread_cond_signal(queue->notFull);
    return elem;
}


// synchronized wrapper for putting into queue.
void put(syncqueue_t * queue, void *elem) {

    pthread_mutex_lock(queue->mut);
    while (queue->full) {
        //              printf ("queue %s FULL.\n", queue->qname);
        pthread_cond_wait(queue->notFull, queue->mut);
    }
    enqueue(queue, elem);
    pthread_mutex_unlock(queue->mut);
    pthread_cond_signal(queue->notEmpty);
}


// create a new initialized queue
syncqueue_t *syncqueue_init(const char *qname, unsigned long size) {

    syncqueue_t *q;

    q = (syncqueue_t *) calloc(1, sizeof(syncqueue_t));
    if(q == NULL) {
        std::string msg("Couldn't create queue! Aborting.");
        fprintf(stderr, "%s", msg.c_str());
        throw std::runtime_error(msg);
    }

    q->buf = (void **)calloc(size, sizeof(void *));
    q->qname = qname;
    q->empty = TRUE;
    q->full = FALSE;
    q->head = 0;
    q->tail = 0;
    q->mut = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(q->mut, NULL);
    q->notFull = (pthread_cond_t *) malloc(sizeof(pthread_cond_t));
    pthread_cond_init(q->notFull, NULL);
    q->notEmpty = (pthread_cond_t *) malloc(sizeof(pthread_cond_t));
    pthread_cond_init(q->notEmpty, NULL);
    q->size = size;

    return (q);
}


// destroy a queue and reclaim memory
void syncqueue_destroy(syncqueue_t * q) {

    // watch for memory leaks here!!!!
    //      free(q->qname);
    pthread_mutex_destroy(q->mut);
    free(q->mut);
    q->mut = NULL;
    pthread_cond_destroy(q->notFull);
    free(q->notFull);
    q->notFull = NULL;
    pthread_cond_destroy(q->notEmpty);
    free(q->notEmpty);
    q->notEmpty = NULL;
    free(q->buf);
    q->buf = NULL;
    free(q);
}


// add an element to end of q
void enqueue(syncqueue_t * q, void *elem) {

    q->buf[q->tail] = elem;
    q->tail++;
    if(q->tail == q->size)
        q->tail = 0;
    if(q->tail == q->head)
        q->full = TRUE;
    q->empty = FALSE;
}


// remove and return the oldest packet_t in the queue
static void *dequeue(syncqueue_t * q) {

    void *next = q->buf[q->head];

    q->head++;
    if(q->head == q->size)
        q->head = 0;
    if(q->head == q->tail)
        q->empty = TRUE;
    q->full = FALSE;

    return next;
}
