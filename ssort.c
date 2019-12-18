#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <assert.h>

#include "float_vec.h"
#include "barrier.h"
#include "utils.h"

pthread_mutex_t lock;

typedef struct sort_job {
	long thread_id;
	float* data;
	long size;
	floats* samps;
	long* sizes;
	barrier* bb;
	const char* foutname;
} sort_job;

int
compare(const void * a, const void * b)
{
	int result = ( *(int*)a - *(int*)b );
	if (result > 0)
	{
		return 1;
	}
	else if (result < 0)
	{
		return -1;
	}
	else
	{
		return 0;
	}
}
	
void
qsort_floats(floats* xs)
{
    qsort(xs->data, xs->size, sizeof(float), compare);
}

//-----------------------------------------------------------------------------------------------------

// 2. SAMPLE

floats*
sample(float* data, long size, int P)
{

    // - Select 3 * (P-1) items from the array.
    float* new_floats = malloc( (3*(P-1)) * sizeof(float));
    for(int ii = 0; ii < 3 * (P-1); ++ii) {
	    long rr = rand() % size;
	    float curr = data[rr];
	    new_floats[ii] = curr;
    }



    // - Sort those items.
    qsort(new_floats, 3*(P-1), sizeof(float), compare);
    floats* return_array = make_floats(size+2);



    // - add 0 at the start
    float zero = 0;
    floats_push(return_array, zero);



    // - Take the median of each group of three in the sorted array, producing an array (samples) of (P-1) items
    for(int jj = 0; jj < 3 * (P-1); ++jj) {
	    if((jj - 1) % 3 == 0) {
		   float mover = new_floats[jj];
		   floats_push(return_array, mover);
	    }
    }
    free(new_floats);

    // - add +infinity at the end
    // - has items 0 to P (P+1 items)
    floats_push(return_array, INFINITY);
    return return_array;  
}

//-----------------------------------------------------------------------------------------------------

//-----------------------------------------------------------------------------------------------------

// 3. PARTITION

void*
thread_worker(void* arg)
{
    // Each thread scans full input for what falls between the P's it's given
    sort_job* job = (sort_job*) arg;
    floats* xs = make_floats(job->size);
    float min = job->samps->data[job->thread_id];
    float max = job->samps->data[job->thread_id + 1];
    for(int ii = 0; ii < job->size; ++ii) {
	    if(job->data[ii] >= min && job->data[ii] < max) {
		    floats_push(xs, job->data[ii]);
	    }
    } 
    printf("%ld: start %.04f, count %ld\n", job->thread_id, job->samps->data[job->thread_id], xs->size); 

    // - Write the local size to the sizes[p].
    job->sizes[job->thread_id] = xs->size;

//-----------------------------------------------------------------------------------------------------

    // 4. SORT LOCALLY
 
    // - Each thread uses quicksort to sort the local array
    qsort_floats(xs);

//-----------------------------------------------------------------------------------------------------

    barrier_wait(job->bb);

    int start = 0;
    for(int jj = 0; jj < job->thread_id; ++jj) {
	start = start + job->sizes[jj];
    }

    for (int kk = 0; kk < xs->size; ++kk) {
	job->data[start + kk] = xs->data[kk];
    }
    int fdout = open(job->foutname, O_RDWR);
    //assert(fdout != -1);
    lseek(fdout, (job->sizes[job->thread_id] * sizeof(float)), SEEK_SET);
    for(int ll = 0; ll < xs->size; ++ll) {
		    float currdata = xs->data[ll];
		    write(fdout, &currdata, sizeof(float));
    }
    close(fdout);
    free(job);
    free_floats(xs);
    return 0;
}

void
thread(float* data, long size, int PP, floats* samps, long* sizes, barrier* bb, const char* foutname)
{
	// - Spawn P threads, numbered p in (0 to P-1).
	pthread_t kids[PP];
	pthread_mutex_init(&lock, 0); 
	for(long pp = 0; pp < PP; ++pp) {
		sort_job* job = malloc(sizeof(sort_job));
		job->thread_id = pp;
		job->data = data;
		job->size = size;
		job->samps = samps;
		job->sizes = sizes;
		job->bb = bb;
		job->foutname = foutname;
		pthread_create(&(kids[pp]), 0, thread_worker, job); 
	}	
	for(long pp = 0; pp < PP; ++pp) {
		pthread_join(kids[pp], 0); 
	}
}

//----------------------------------------------------------------------------------------------------

void
sample_sort(float* data, long size, int P, long* sizes, barrier* bb, const char* foutname)
{
    floats* samps = sample(data, size, P);
    thread(data, size, P, samps, sizes, bb, foutname);
    free_floats(samps);
}

int
main(int argc, char* argv[])
{
    alarm(120);

    //error message
    if (argc != 4) {
        printf("Usage:\n");
        printf("\t%s P data.dat\n", argv[0]);
        return 1;
    }

    const int P = atoi(argv[1]);
    const char* fname = argv[2];
    const char* foutname = argv[3];

    seed_rng();

    int rv;
    struct stat st;
    rv = stat(fname, &st);
    check_rv(rv);

    const int fsize = st.st_size;
    if (fsize < 8) {
        printf("File too small.\n");
        return 1;
    }

//-----------------------------------------------------------------------------------------------------

    // 1. SETUP

    // - Read input file into a "floats" in memory.
    int fd = open(fname, O_RDONLY);
    check_rv(fd);
    long sz;
    read(fd, &sz, sizeof(long));
    floats* new_floats = make_floats(sz);
    float* data = new_floats->data;
    float temp;
    for(int ii = 0; ii<sz; ++ii) {
	    read(fd, &temp, sizeof(float));
	    floats_push(new_floats, temp);
    }



    // - Allocate an array, sizes, of P longs
    long sizes_bytes = P * sizeof(long);
    long* sizes = malloc(sizes_bytes);



    // - Use open / ftruncate to create the output file, same size as the input file
    int fdout = open(argv[3], O_RDWR);
    ftruncate(fdout, sz); 

//-----------------------------------------------------------------------------------------------------

    barrier* bb = make_barrier(P);
    sample_sort(data, sz, P, sizes, bb, foutname);
    free_barrier(bb);
    free(data);
    free(sizes);
    free(new_floats);
    close(fd);
    close(fdout);
    return 0;
    //free_floats(new_floats);
}

