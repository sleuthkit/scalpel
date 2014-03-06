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
// priority queue header file "prioque.h" */
// (c) 198x/1998/2001/2004 (minor updates) by Golden G. Richard III, Ph.D. */
//
//
// Major update October 2004: The package is now thread-safe.  The
// package now depends on the pthreads library (for access to
// semaphores).  All functions now take one or more pointers to a
// queue or context, so existing applications will need to be 
// modified slightly.

// Additionally, new functions are now provided for walking the queue
// "locally"--that is, more than one thread can maintain a position in
// the queue using a local Context.  The old 'rewind_queue()',
// 'pointer_to_current()', etc. are maintained for a single position
// during a global walk.
//
// Finally, a long-standing memory leak was corrected.
//
// April 2007: added an option to init_queue to allow priority field to 
// serve only as a tag--the queue is not sorted by the priority field when 
// 'priority_is_tag_only' is set.
//

#include <pthread.h>

#define  TRUE  1
#define  FALSE 0
#define CONSISTENCY_CHECKING

#if ! defined(QUEUE_TYPE_DEFINED)
#define QUEUE_TYPE_DEFINED

/* type of one element in a queue */

typedef struct _Queue_element
{
    void *info;
    int priority;
    struct _Queue_element *next;
} *Queue_element;

/* basic queue type */

typedef struct Queue
{
    Queue_element queue;		/* linked list of elements */
    Queue_element current;	/* current position for sequential access functions */
    Queue_element previous;	/* one step back from current */
    int queuelength;		/* # of elements in queue */
    int elementsize;		/* 'sizeof()' one element */
    int duplicates;		/* are duplicates allowed? */
    int (*compare) (void *e1, void *e2);	/* element comparision function */
    pthread_mutex_t lock;
    int priority_is_tag_only;
} Queue;

typedef struct Context
{
    Queue_element current;	/* current position for local seq access functions */
    Queue_element previous;	/* one step back from current */
    Queue *queue;			/* queue associated with this context */
} Context;



/********/
/*
NOTE: init_queue() must be called for a new queue before any other "prioque.c" 
functions are called.
*/
/********/


/* function prototypes and descriptions for visible "prioque.c" functions
*/

////////////////////////////
// SECTION 1
////////////////////////////

/* initializes a new queue 'q' to have elements of size 'elementsize'.
   If 'duplicates' is true, then duplicate elements in the queue are
   allowed, otherwise duplicates are silently deleted.  The
   element-comparing function 'compare' is required only if
   duplicates==FALSE or either equal_queues() or element_in_queue()
   are used (otherwise, a null function is acceptable).  'compare'
   should be a standard qsort()-style element comparison function:
   returns 0 if elements match, otherwise a non-0 result (<, > cases
   are not used).

   NOTE:Only the 'compare' function is used for duplicate
   detection--priority is not considered (i.e., attempting to add a
   "duplicate" element that has a different priority than the existing
   element will have no effect!)
*/
void init_queue (Queue * q, int elementsize, int duplicates,
		 int (*compare) (void *e1, void *e2),
		 int priority_is_tag_only);


/* destroys all elements in 'q'
*/
void destroy_queue (Queue * q);


/* adds 'element' to the 'q' with position based on 'priority'.
   Elements with lower-numbered priorities are placed closer to the
   front of the queue, with strict 'to the rear' placement for items
   with equal priority [that is, given two items with equal priority,
   the one most recently added will appear closer to the rear of the
   queue].
*/
void add_to_queue (Queue * q, void *element, int priority);




/* removes the element at the front of the 'q' and places it in 'element'.
*/
void remove_from_front (Queue * q, void *element);


/* returns TRUE if the 'element' exists in the 'q', otherwise false.
   The 'compare' function is used for matching.  As a side-effect, the
   current position in the queue is set to matching element, so
   'update_current()' can be used to update the value of the
   'element'.  If the element is not found, the current position is
   set to the first element in the queue.
*/
int element_in_queue (Queue * q, void *element);


/* returns TRUE if 'q' is empty, FALSE otherwise 
*/
int empty_queue (Queue * q);


/* returns the number of elements in the 'q'.
*/
int queue_length (Queue * q);


/* makes a copy of 'q2' into 'q1'.  'q2' is not modified.
*/
void copy_queue (Queue * q1, Queue * q2);


/* determines if 'q1' and 'q2' are equivalent.  Uses the 'compare'
   function of the first queue, which should match the 'compare' for
   the second!  Returns TRUE if the queues are equal, otherwise
   returns FALSE.
*/
int equal_queues (Queue * q1, Queue * q2);


/* merge 'q2' into 'q1'.   'q2' is not modified.
*/
void merge_queues (Queue * q1, Queue * q2);


////////////////////////////
// SECTION 2
////////////////////////////

/* the following are functions used to "walk" the queue (globally)
   like a linked list, examining or deleting elements along the way.
   Current position is rewound to the beginning by functions above (in
   SECTION 1), except for 'empty_queue()' and 'queue_length()', which
   do not modify the global current position.
*/

/********************/
/********************/


/* move to the first element in the 'q' */
void rewind_queue (Queue * q);


/* move to the next element in the 'q' */
void next_element (Queue * q);


/* allows update of current element.  The priority should not
   be changed by this function!
*/

void update_current (Queue * q, void *element);


/* retrieve the element stored at the current position in the 'q' */
void peek_at_current (Queue * q, void *element);


/* return a pointer to the data portion of the current element */
void *pointer_to_current (Queue * q);


/* return priority of current element in the 'q' */
int current_priority (Queue * q);


/* delete the element stored at the current position */
void delete_current (Queue * q);



/* has the current position in 'q'  moved beyond the last valid element?  
   Returns TRUE if so, FALSE otherwise.
*/
int end_of_queue (Queue * q);

////////////////////////////
// SECTION 3
////////////////////////////

// Functions in this section provide the ability to walk the queue
// with a locally maintained position.  They do not affect the global
// queue position for functions in SECTION 2.
// 

/* create a new local context for queue 'q'.  The first element in 
   'q' becomes the current element in the newly created context.
*/
void local_init_context (Queue * q, Context * ctx);


/* move to the first element in the 'q', maintaining a local position */
void local_rewind_queue (Context * ctx);


/* move to the next element in the 'q', maintaining a local position */
void local_next_element (Context * ctx);


/* allows update of current element, relative to current local position.
   The priority should not be changed by this function!
*/

void local_update_current (Context * ctx, void *element);


/* retrieve the element stored at the current local position in the 'q' */
void local_peek_at_current (Context * ctx, void *element);


/* return a pointer to the data portion of the element at the current
   local position */
void *local_pointer_to_current (Context * ctx);


/* return priority of element at current local position in the 'q' */
int local_current_priority (Context * ctx);


/* delete the element stored at the current local position */
void local_delete_current (Context * ctx);


/* has the current local position in 'q' moved beyond the last valid
   element?  Returns TRUE if so, FALSE otherwise.
*/
int local_end_of_queue (Context * ctx);


#endif


/*** QUICK REFERENCE ***

// SECTION 1
void init_queue(Queue *q, int elementsize, int duplicates, 
		int (*compare)(void *e1, void *e2), int priority_is_tag_only);
void destroy_queue(Queue *q);
void add_to_queue(Queue *q, void *element, int priority);
void remove_from_front(Queue *q, void *element);
int element_in_queue(Queue *q, void *element);
int empty_queue(Queue *q);
int queue_length(Queue *q);
void copy_queue(Queue *q1, Queue *q2);
int equal_queues(Queue q1, Queue *q2);
void merge_queues(Queue *q1, Queue *q2);

// SECTION 2

void rewind_queue(Queue *q);
void next_element(Queue *q);
void update_current(Queue *q, void *element);
void peek_at_current(Queue *q, void *element);
void *pointer_to_current(Queue *q);
int current_priority(Queue *q);
void delete_current(Queue *q);
int end_of_queue(Queue *q);

// SECTION 3
void local_init_context(Queue, *q, Context *ctx);
void local_rewind_queue(Context *ctx);
void local_next_element(Context *ctx);
void local_update_current(Context *ctx, void *element);
void local_peek_at_current(Context *ctx, void *element);
void local_*pointer_to_current(Context *ctx);
int local_current_priority(Context *ctx);
void local_delete_current(Context *ctx);
int local_end_of_queue(Context *ctx);
 *** QUICK REFERENCE ***/
