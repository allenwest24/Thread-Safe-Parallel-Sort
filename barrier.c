// Author: Nat Tuck
// CS3650 starter code

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>

#include "barrier.h"

barrier*
make_barrier(int nn)
{
    int rv;
    barrier* bb = malloc(sizeof(barrier));
    assert(bb != 0);
    if((long) bb == -1) {
	    perror("malloc(barrier)");
	    abort();
    }
    rv = pthread_cond_init(&(bb->barrier), 0);
    if(rv == -1) {
	    perror("sem_init(barrier)");
	    abort();
    }
    pthread_mutex_init(&(bb->mutex), 0);
    if(rv == -1) {
	    perror("sem_init(mutex)");
	    abort();
    }
    bb->count = nn;
    bb->seen = 0;
    return bb;
}

void
barrier_wait(barrier* bb)
{   
    pthread_mutex_lock(&(bb->mutex));
    bb->seen += 1;
    int seen = bb->seen;
    if(seen < bb->count) {
	    pthread_cond_wait(&(bb->barrier), &(bb->mutex));
    }
    else {
	    pthread_cond_broadcast(&(bb->barrier));
    }
    pthread_mutex_unlock(&(bb->mutex));
}

void
free_barrier(barrier* bb)
{
    free(bb);
}

