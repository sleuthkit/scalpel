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

//
// implementation of "prioque.h" priority queue functions */
// (c) 198x/1998/2001 (minor updates) by Golden G. Richard III, Ph.D. */
//
// Major update in 2007.  See prioque.h for details.
//

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

//C++ STL headers
#include <exception>
#include <stdexcept>
#include <string>
#include <sstream>

#include "prioque.h"


// global lock on entire package
pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;

// for init purposes
pthread_mutex_t initial_mutex = PTHREAD_MUTEX_INITIALIZER;

// function prototypes for internal functions
void nolock_next_element(Queue * q);
void nolock_rewind_queue(Queue * q);
int nolock_element_in_queue(Queue * q, void *element);
void nolock_destroy_queue(Queue * q);
void local_nolock_next_element(Context * ctx);
void local_nolock_rewind_queue(Context * ctx);


void
init_queue(Queue * q, int elementsize, int duplicates,
	   int (*compare) (void *e1, void *e2), int priority_is_tag_only) {

  q->queuelength = 0;
  q->elementsize = elementsize;
  q->queue = 0;
  q->duplicates = duplicates;
  q->compare = compare;
  q->priority_is_tag_only = priority_is_tag_only;
  nolock_rewind_queue(q);
  q->lock = initial_mutex;

}


void destroy_queue(Queue * q) {

  // lock entire queue
  pthread_mutex_lock(&(q->lock));

  nolock_destroy_queue(q);

  // release lock on queue
  pthread_mutex_unlock(&(q->lock));
}


void nolock_destroy_queue(Queue * q) {

  Queue_element temp;

  if(q != 0) {
    while (q->queue != 0) {
      free(q->queue->info);
      q->queue->info = NULL;
      temp = q->queue;
      q->queue = q->queue->next;
      free(temp);
      (q->queuelength)--;
    }
  }

  nolock_rewind_queue(q);
}




int element_in_queue(Queue * q, void *element) {

  int found;
  // lock entire queue
  pthread_mutex_lock(&(q->lock));

  found = nolock_element_in_queue(q, element);

  // release lock on queue
  pthread_mutex_unlock(&(q->lock));

  return found;
}

int nolock_element_in_queue(Queue * q, void *element) {

  int found = 0;

  if(q->queue != 0) {
    nolock_rewind_queue(q);
    while (!end_of_queue(q) && !found) {
      if(q->compare(element, q->current->info) == 0) {
	found = 1;
      }
      else {
	nolock_next_element(q);
      }
    }
  }
  if(!found) {
    nolock_rewind_queue(q);
  }

  return found;
}



void nolock_add_to_queue(Queue * q, void *element, int priority) {

  Queue_element new_element, ptr, prev = 0;

  if(!q->queue ||
     (q->queue && (q->duplicates || !nolock_element_in_queue(q, element)))) {

    new_element = (Queue_element) malloc(sizeof(struct _Queue_element));
    if(new_element == 0) {
      std::string msg ("Malloc failed in function add_to_queue()\n");
      fprintf(stderr, msg.c_str());
      throw std::runtime_error(msg);
    }
    new_element->info = (void *)malloc(q->elementsize);
    if(new_element->info == 0) {
      std::string msg("Malloc failed in function add_to_queue()\n");
      fprintf(stderr, msg.c_str());
      throw std::runtime_error(msg);
    }

    memcpy(new_element->info, element, q->elementsize);

    new_element->priority = priority;

    (q->queuelength)++;

    if(q->queue == 0) {
      new_element->next = 0;
      q->queue = new_element;
    }
    else if(q->priority_is_tag_only || (q->queue)->priority >= priority) {
      new_element->next = q->queue;
      q->queue = new_element;
    }
    else {
      ptr = q->queue;
      while (ptr != 0 && priority >= ptr->priority) {


	putchar('.');		//**//

	prev = ptr;
	ptr = ptr->next;
      }

      new_element->next = prev->next;
      prev->next = new_element;
    }

    nolock_rewind_queue(q);
  }
}


void add_to_queue(Queue * q, void *element, int priority) {

  // lock entire queue
  pthread_mutex_lock(&(q->lock));

  nolock_add_to_queue(q, element, priority);

  // release lock on queue
  pthread_mutex_unlock(&(q->lock));

}


int empty_queue(Queue * q) {

  return q->queue == 0;
}



void remove_from_front(Queue * q, void *element) {

  Queue_element temp;

  // lock entire queue
  pthread_mutex_lock(&(q->lock));

#if defined(CONSISTENCY_CHECKING)
  if(q->queue == 0) {
    std::string msg("Malloc failed in function remove_from_front()\n");
    fprintf(stderr, msg.c_str());
    throw std::runtime_error(msg);
  }
  else
#endif
  {

    memcpy(element, q->queue->info, q->elementsize);

    free(q->queue->info);
    q->queue->info = NULL;
    temp = q->queue;
    q->queue = q->queue->next;
    free(temp);
    (q->queuelength)--;
  }

  nolock_rewind_queue(q);

  // release lock on queue
  pthread_mutex_unlock(&(q->lock));
}



void peek_at_current(Queue * q, void *element) {

  // lock entire queue
  pthread_mutex_lock(&(q->lock));

#if defined(CONSISTENCY_CHECKING)
  if(q->queue == 0 || q->current == 0) {
	  std::string msg("NULL pointer in function peek_at_current()\n");
      fprintf(stderr, msg.c_str());
      throw std::runtime_error(msg);
  }
  else
#endif
  {

    memcpy(element, (q->current)->info, q->elementsize);

    // release lock on queue
    pthread_mutex_unlock(&(q->lock));
  }
}


void *pointer_to_current(Queue * q) {

  void *data;

  // lock entire queue
  pthread_mutex_lock(&(q->lock));

#if defined(CONSISTENCY_CHECKING)
  if(q->queue == 0 || q->current == 0) {
    std::string msg("NULL pointer in function pointer_to_current()\n");
    fprintf(stderr, msg.c_str());
    throw std::runtime_error(msg);
  }
  else
#endif
  {

    data = (q->current)->info;

    // release lock on queue
    pthread_mutex_unlock(&(q->lock));

    return data;
  }
}



int current_priority(Queue * q) {

  int priority;

  // lock entire queue
  pthread_mutex_lock(&(q->lock));

#if defined(CONSISTENCY_CHECKING)
  if(q->queue == 0 || q->current == 0) {
    std::string msg("NULL pointer in function peek_at_current()\n");
    fprintf(stderr, msg.c_str());
    throw std::runtime_error(msg);
  }
  else
#endif
  {

    priority = (q->current)->priority;

    // release lock on queue
    pthread_mutex_unlock(&(q->lock));

    return priority;
  }
}



void update_current(Queue * q, void *element) {

  // lock entire queue
  pthread_mutex_lock(&(q->lock));

#if defined(CONSISTENCY_CHECKING)
  if(q->queue == 0 || q->current == 0) {
    std::string msg("Malloc failed in function update_current()\n");
    fprintf(stderr, msg.c_str());
    throw std::runtime_error(msg);
  }
  else
#endif
  {
    memcpy(q->current->info, element, q->elementsize);
  }

  // release lock on queue
  pthread_mutex_unlock(&(q->lock));
}



void delete_current(Queue * q) {

  Queue_element temp;

  // lock entire queue
  pthread_mutex_lock(&(q->lock));

#if defined(CONSISTENCY_CHECKING)
  if(q->queue == 0 || q->current == 0) {
    std::string msg("Malloc failed in function delete_current()\n");
    fprintf(stderr, msg.c_str());
    throw std::runtime_error(msg);
  }
  else
#endif
  {

    free(q->current->info);
    q->current->info = NULL;
    temp = q->current;

    if(q->previous == 0) {	/* deletion at beginning */
      q->queue = q->queue->next;
      q->current = q->queue;
    }
    else {			/* internal deletion */
      q->previous->next = q->current->next;
      q->current = q->previous->next;
    }

    free(temp);
    (q->queuelength)--;

  }

  // release lock on queue
  pthread_mutex_unlock(&(q->lock));

}



int end_of_queue(Queue * q) {

  return (q->current == 0);

}


void next_element(Queue * q) {

  // lock entire queue
  pthread_mutex_lock(&(q->lock));

  nolock_next_element(q);

  // release lock on queue
  pthread_mutex_unlock(&(q->lock));
}


void nolock_next_element(Queue * q) {

#if defined(CONSISTENCY_CHECKING)
  if(q->queue == 0) {
    std::string msg("NULL pointer in function next_element()\n");
    fprintf(stderr, msg.c_str());
    throw std::runtime_error(msg);
  }
  else if(q->current == 0) {
    std::string msg("Advance past end in NULL pointer in function next_element()\n");
    fprintf(stderr, msg.c_str());
    throw std::runtime_error(msg);
  }
  else
#endif
  {
    q->previous = q->current;
    q->current = q->current->next;
  }
}



void rewind_queue(Queue * q) {

  // lock entire queue
  pthread_mutex_lock(&(q->lock));

  nolock_rewind_queue(q);
  // release lock on queue
  pthread_mutex_unlock(&(q->lock));
}

void nolock_rewind_queue(Queue * q) {

  q->current = q->queue;
  q->previous = 0;

}


int queue_length(Queue * q) {

  return q->queuelength;
}


void copy_queue(Queue * q1, Queue * q2) {

  Queue_element temp, new_element, endq1;

  // to avoid deadlock, this function acquires a global package
  // lock!
  pthread_mutex_lock(&global_lock);

  // lock entire queues q1, q2
  pthread_mutex_lock(&(q1->lock));
  pthread_mutex_lock(&(q2->lock));

  /* free elements in q1 before copy */

  nolock_destroy_queue(q1);

  /* now make q1 a clone of q2 */

  q1->queuelength = 0;
  q1->elementsize = q2->elementsize;
  q1->queue = 0;
  q1->duplicates = q2->duplicates;
  q1->compare = q2->compare;

  temp = q2->queue;
  endq1 = q1->queue;

  while (temp != 0) {
    new_element = (Queue_element) malloc(sizeof(struct _Queue_element));

    if(new_element == 0) {
      std::string msg("Malloc failed in function copy_queue()\n");
      fprintf(stderr, msg.c_str());
      throw std::runtime_error(msg);
    }

    new_element->info = (void *)malloc(q1->elementsize);
    if(new_element->info == 0) {
      std::string msg("Malloc failed in function copy_queue()\n");
      fprintf(stderr, msg.c_str());
      throw std::runtime_error(msg);
    }
    memcpy(new_element->info, temp->info, q1->elementsize);

    new_element->priority = temp->priority;
    new_element->next = 0;

    (q1->queuelength)++;

    if(endq1 == 0) {
      q1->queue = new_element;
    }
    else {
      endq1->next = new_element;
    }
    endq1 = new_element;
    temp = temp->next;
  }

  nolock_rewind_queue(q1);

  // release locks on q1, q2
  pthread_mutex_unlock(&(q2->lock));
  pthread_mutex_unlock(&(q1->lock));

  // release global package lock
  pthread_mutex_unlock(&global_lock);

}



int equal_queues(Queue * q1, Queue * q2) {

  Queue_element temp1, temp2;
  int same = TRUE;

  // to avoid deadlock, this function acquires a global package
  // lock!
  pthread_mutex_lock(&global_lock);

  // lock entire queues q1, q2
  pthread_mutex_lock(&(q1->lock));
  pthread_mutex_lock(&(q2->lock));

  if(q1->queuelength != q2->queuelength || q1->elementsize != q2->elementsize) {
    same = FALSE;
  }
  else {
    temp1 = q1->queue;
    temp2 = q2->queue;
    while (same && temp1 != 0) {
      same = (!memcmp(temp1->info, temp2->info, q1->elementsize) &&
	      temp1->priority == temp2->priority);
      temp1 = temp1->next;
      temp2 = temp2->next;
    }
  }

  // release locks on q1, q2
  pthread_mutex_unlock(&(q2->lock));
  pthread_mutex_unlock(&(q1->lock));

  // release global package lock
  pthread_mutex_unlock(&global_lock);

  return same;
}


void merge_queues(Queue * q1, Queue * q2) {

  Queue_element temp;

  // to avoid deadlock, this function acquires a global package
  // lock!
  pthread_mutex_lock(&global_lock);

  // lock entire queues q1, q2
  pthread_mutex_lock(&(q1->lock));
  pthread_mutex_lock(&(q2->lock));

  temp = q2->queue;

  while (temp != 0) {
    nolock_add_to_queue(q1, temp->info, temp->priority);
    temp = temp->next;
  }

  nolock_rewind_queue(q1);

  // release locks on q1, q2
  pthread_mutex_unlock(&(q2->lock));
  pthread_mutex_unlock(&(q1->lock));

  // release global package lock
  pthread_mutex_unlock(&global_lock);

}



void local_peek_at_current(Context * ctx, void *element) {

  // lock entire queue
  pthread_mutex_lock(&(ctx->queue->lock));

#if defined(CONSISTENCY_CHECKING)
  if(ctx->queue == 0 || ctx->current == 0) {
    std::string msg("NULL pointer in function peek_at_current()\n");
    fprintf(stderr, msg.c_str());
    throw std::runtime_error(msg);
  }
  else
#endif
  {

    memcpy(element, (ctx->current)->info, ctx->queue->elementsize);

    // release lock on queue
    pthread_mutex_unlock(&(ctx->queue->lock));

  }
}


void *local_pointer_to_current(Context * ctx) {

  void *data;

  // lock entire queue
  pthread_mutex_lock(&(ctx->queue->lock));

#if defined(CONSISTENCY_CHECKING)
  if(ctx->queue == 0 || ctx->current == 0) {
    std::string msg("NULL pointer in function pointer_to_current()\n");
    fprintf(stderr, msg.c_str());
    throw std::runtime_error(msg);
  }
  else
#endif
  {

    data = (ctx->current)->info;

    // release lock on queue
    pthread_mutex_unlock(&(ctx->queue->lock));

    return data;
  }
}


int local_current_priority(Context * ctx) {

  int priority;

  // lock entire queue
  pthread_mutex_lock(&(ctx->queue->lock));

#if defined(CONSISTENCY_CHECKING)
  if(ctx->queue == 0 || ctx->current == 0) {
    std::string msg("NULL pointer in function peek_at_current()\n");
    fprintf(stderr, msg.c_str());
    throw std::runtime_error(msg);
  }
  else
#endif
  {
    priority = (ctx->current)->priority;

    // release lock on queue
    pthread_mutex_unlock(&(ctx->queue->lock));

    return priority;
  }
}



void local_update_current(Context * ctx, void *element) {

  // lock entire queue
  pthread_mutex_lock(&(ctx->queue->lock));

#if defined(CONSISTENCY_CHECKING)
  if(ctx->queue == 0 || ctx->current == 0) {
    std::string msg("NULL pointer in function update_current()\n");
    fprintf(stderr, msg.c_str());
    throw std::runtime_error(msg);
  }
  else
#endif
  {
    memcpy(ctx->current->info, element, ctx->queue->elementsize);
  }

  // release lock on queue
  pthread_mutex_unlock(&(ctx->queue->lock));
}



void local_delete_current(Context * ctx) {

  Queue_element temp;

  // lock entire queue
  pthread_mutex_lock(&(ctx->queue->lock));

#if defined(CONSISTENCY_CHECKING)
  if(ctx->queue == 0 || ctx->current == 0) {
    std::string msg("NULL pointer in function delete_current()\n");
    fprintf(stderr, msg.c_str());
    throw std::runtime_error(msg);
  }
  else
#endif
  {

    free(ctx->current->info);
    ctx->current->info = NULL;
    temp = ctx->current;

    if(ctx->previous == 0) {	/* deletion at beginning */
      ctx->queue->queue = ctx->queue->queue->next;
      ctx->current = ctx->queue->queue;
    }
    else {			/* internal deletion */
      ctx->previous->next = ctx->current->next;
      ctx->current = ctx->current->next;
    }

    free(temp);
    (ctx->queue->queuelength)--;

  }

  // release lock on queue
  pthread_mutex_unlock(&(ctx->queue->lock));

}



int local_end_of_queue(Context * ctx) {

  return (ctx->current == 0);

}


void local_next_element(Context * ctx) {

  // lock entire queue
  pthread_mutex_lock(&(ctx->queue->lock));

  local_nolock_next_element(ctx);

  // release lock on queue
  pthread_mutex_unlock(&(ctx->queue->lock));
}


void local_nolock_next_element(Context * ctx) {

#if defined(CONSISTENCY_CHECKING)
  if(ctx->queue == 0) {
    std::string msg("NULL pointer in function next_element()\n");
    fprintf(stderr, msg.c_str());
    throw std::runtime_error(msg);
  }
  else if(ctx->current == 0) {
    std::string msg("Advance past end in NULL pointer in function next_element()\n");
    fprintf(stderr, msg.c_str());
    throw std::runtime_error(msg);
  }
  else
#endif
  {
    ctx->previous = ctx->current;
    ctx->current = ctx->current->next;
  }
}



void local_rewind_queue(Context * ctx) {

  // lock entire queue
  pthread_mutex_lock(&(ctx->queue->lock));

  local_nolock_rewind_queue(ctx);

  // release lock on queue
  pthread_mutex_unlock(&(ctx->queue->lock));
}

void local_nolock_rewind_queue(Context * ctx) {

  ctx->current = ctx->queue->queue;
  ctx->previous = 0;

}

void local_init_context(Queue * q, Context * ctx) {
  ctx->queue = q;
  ctx->current = q->queue;
  ctx->previous = 0;
}
