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

#ifndef QUEUE_H
#define QUEUE_H


#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#define TRUE 	1
#define FALSE 	0


typedef struct
{
  const char *qname;
  void **buf;
  unsigned long head, tail;
  int full, empty;
  pthread_mutex_t *mut;
  pthread_cond_t *notFull, *notEmpty;
  unsigned long size;
} syncqueue_t;


// public queue.c functions
void *get (syncqueue_t * queue);
void put (syncqueue_t * queue, void *elem);
void enqueue (syncqueue_t * q, void *elem);
syncqueue_t *syncqueue_init (const char *qname, unsigned long queuesize);
void syncqueue_destroy (syncqueue_t * q);


#endif // QUEUE_H
