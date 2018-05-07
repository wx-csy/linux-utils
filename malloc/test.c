/**
 * \file   test-malloc_test.c
 * \author C. Lever and D. Boreham, Christian Eder ( ederc@mathematik.uni-kl.de )
 * \date   2000
 * \brief  Test file for xmalloc. This is a multi-threaded test system by
 *         Lever and Boreham. It is first noted in their paper "malloc()
 *         Performance in a Multithreaded Linux Environment", appeared at the
 *         USENIX 2000 Annual Technical Conference: FREENIX Track.
 *         This file is part of XMALLOC, licensed under the GNU General
 *         Public License version 3. See COPYING for more information.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>

#include "malloc.h"

//=========================================================
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>

//=========================================================
// Logging malloc/free operation for testing
//=========================================================
typedef struct Log {
    void *addr_;
    size_t len_;
} Log;

int __log_count __attribute__((aligned(64))) = 0;

#define LOG_BUF_SIZE 5000000
static Log __log_buf[LOG_BUF_SIZE];

static Log* new_log() {
    int i = __atomic_fetch_add(&__log_count, 1, __ATOMIC_SEQ_CST);
    //
    // If this assertion fails, you can set LOG_BUF_SIZE larger
    //
    assert(i < LOG_BUF_SIZE);
    return &__log_buf[i];
}

static void do_log(void *ptr, size_t size) {
    Log *log = new_log();
    log->addr_ = ptr;
    log->len_ = size;
}

static void do_finish() {
    FILE *fp = fopen("./mem.log", "w");
    if (!fp) {
        fprintf(stderr, "Error in `fopen`\n");
        return;
    }

    char buf[100];
    int sz =  __atomic_load_n(&__log_count, __ATOMIC_SEQ_CST);
    for (int i = 0; i < sz; ++i) {
        //
        // free(NULL); is a no-op
        //
        if (__log_buf[i].addr_ == NULL && __log_buf[i].len_ == 0) {
            continue;
        }
        size_t l = sprintf(buf, "%p,%lu\n", __log_buf[i].addr_,
                                            __log_buf[i].len_);
        assert(l < 100);
        fwrite(buf, 1, l, fp);
    }

    fclose(fp);
    printf("#Total operations: %d\n", __log_count);
}
//=========================================================

static void new_malloc_init() {
    if (atexit(do_finish)) {
        fprintf(stderr, "Error in `atexit`\n");
        exit(EXIT_FAILURE);
    }
    return;
}

//=========================================================
// Function wrappers
//=========================================================

static void* new_malloc(size_t size) {
    assert(size != 0);
    void *ptr = do_malloc(size);
    do_log(ptr, size);
    return ptr;
}

static void new_free(void *ptr) {
    do_log(ptr, 0);
    do_free(ptr);
}
//=========================================================



/* #include "random.h" */
// =======================================================

/* lran2.h
 * by Wolfram Gloger 1996.
 *
 * A small, portable pseudo-random number generator.
 */

#ifndef _LRAN2_H
#define _LRAN2_H

#define LRAN2_MAX 714025l /* constants for portable */
#define IA	  1366l	  /* random number generator */
#define IC	  150889l /* (see e.g. `Numerical Recipes') */

struct lran2_st {
    long x, y, v[97];
};

static void
lran2_init(struct lran2_st* d, long seed)
{
  long x;
  int j;

  x = (IC - seed) % LRAN2_MAX;
  if(x < 0) x = -x;
  for(j=0; j<97; j++) {
    x = (IA*x + IC) % LRAN2_MAX;
    d->v[j] = x;
  }
  d->x = (IA*x + IC) % LRAN2_MAX;
  d->y = d->x;
}

static 
long lran2(struct lran2_st* d)
{
  int j = (d->y % 97);

  d->y = d->v[j];
  d->x = (IA*d->x + IC) % LRAN2_MAX;
  d->v[j] = d->x;
  return d->y;
}

#undef IA
#undef IC
#endif
// =======================================================

#define CACHE_ALIGNED 1

#define xmalloc new_malloc
#define xfree new_free

#define DEFAULT_OBJECT_SIZE 1024

int debug_flag = 0;
int verbose_flag = 0;
#define num_workers_default 4
int num_workers = num_workers_default;
/* double run_time = 5.0; */
double run_time = 0.2;
int object_size = DEFAULT_OBJECT_SIZE;
/* array for thread ids */
pthread_t *thread_ids;
/* array for saving result of each thread */
struct counter {
  long c
#if CACHE_ALIGNED
 __attribute__((aligned(64)))
#endif
;
};
struct counter *counters;

int done_flag = 0;
struct timeval begin;

static void
tvsub(tdiff, t1, t0)
	struct timeval *tdiff, *t1, *t0;
{

	tdiff->tv_sec = t1->tv_sec - t0->tv_sec;
	tdiff->tv_usec = t1->tv_usec - t0->tv_usec;
	if (tdiff->tv_usec < 0)
		tdiff->tv_sec--, tdiff->tv_usec += 1000000;
}

double elapsed_time(struct timeval *time0)
{
	struct timeval timedol;
	struct timeval td;
	double et = 0.0;

	gettimeofday(&timedol, (struct timezone *)0);
	tvsub( &td, &timedol, time0 );
	et = td.tv_sec + ((double)td.tv_usec) / 1000000;

	return( et );
}

static const long possible_sizes[] = {8,12,16,24,32,48,64,96,128,192,256,(256*3)/2,512, (512*3)/2, 1024, (1024*3)/2, 2048};
static const int n_sizes = sizeof(possible_sizes)/sizeof(long);

#define OBJECTS_PER_BATCH 4096
struct batch {
  struct batch *next_batch;
  void *objects[OBJECTS_PER_BATCH];
};

struct batch *batches = NULL;
int batch_count = 0;
const int batch_count_limit = 100;
pthread_cond_t empty_cv = PTHREAD_COND_INITIALIZER;
pthread_cond_t full_cv = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void enqueue_batch(struct batch *batch) {
  pthread_mutex_lock(&lock);
  while (batch_count >= batch_count_limit && !done_flag) {
    pthread_cond_wait(&full_cv, &lock);
  }
  batch->next_batch = batches;
  batches = batch;
  batch_count++;
  pthread_cond_signal(&empty_cv);
  pthread_mutex_unlock(&lock);
}

struct batch* dequeue_batch() {
  pthread_mutex_lock(&lock);
  while (batches == NULL && !done_flag) {
    pthread_cond_wait(&empty_cv, &lock);
  }
  struct batch* result = batches;
  if (result) {
    batches = result->next_batch;
    batch_count--;
    pthread_cond_signal(&full_cv);
  }
  pthread_mutex_unlock(&lock);
  return result;
}

#define atomic_load(addr) __atomic_load_n(addr, __ATOMIC_CONSUME)
#define atomic_store(addr, v) __atomic_store_n(addr, v, __ATOMIC_RELEASE)

void *mem_allocator (void *arg) {
  int thread_id = *(int *)arg;
  struct lran2_st lr;
  lran2_init(&lr, thread_id);

  while (!atomic_load(&done_flag)) {
    struct batch *b = xmalloc(sizeof(*b));
    for (int i = 0; i < OBJECTS_PER_BATCH; i++) {
      size_t siz = object_size > 0 ? object_size : possible_sizes[lran2(&lr)%n_sizes];
      b->objects[i] = xmalloc(siz);
      //memset(b->objects[i], i%256, siz);
    }
    enqueue_batch(b);
  }
  return NULL;
}

void *mem_releaser(void *arg) {
  int thread_id = *(int *)arg;

  while(!atomic_load(&done_flag)) {
    struct batch *b = dequeue_batch();
    if (b) {
      for (int i = 0; i < OBJECTS_PER_BATCH; i++) {
	xfree(b->objects[i]);
      }
      xfree(b);
    }
    counters[thread_id].c += OBJECTS_PER_BATCH;
  }
  return NULL;
}

int run_memory_free_test()
{
	void *ptr = NULL;
	int i;
	double elapse_time = 0.0;
	long total = 0;
	int *ids = (int *)xmalloc(sizeof(int) * num_workers);

	/* Initialize counter */
	for(i = 0; i < num_workers; ++i)
		counters[i].c = 0;

	gettimeofday(&begin, (struct timezone *)0);

	/* Start up the mem_allocator and mem_releaser threads  */
	for(i = 0; i < num_workers; ++i) {
		ids[i] = i;
		if (verbose_flag) printf("Starting mem_releaser %i ...\n", i);
		if (pthread_create(&thread_ids[i * 2], NULL, mem_releaser, (void *)&ids[i])) {
			perror("pthread_create mem_releaser");
			exit(errno);
		}

		if (verbose_flag) printf("Starting mem_allocator %i ...\n", i);
		if (pthread_create(&thread_ids[i * 2 + 1], NULL, mem_allocator, (void *)&ids[i])) {
			perror("pthread_create mem_allocator");
			exit(errno);
		}
	}

	if (verbose_flag) printf("Testing for %.2f seconds\n\n", run_time);

	while (1) {
	  usleep(1000);
	  if (elapsed_time(&begin) > run_time) {
	    atomic_store(&done_flag, 1);
	    pthread_cond_broadcast(&empty_cv);
	    pthread_cond_broadcast(&full_cv);
	    break;
	  }
	}

	for(i = 0; i < num_workers * 2; ++i)
		pthread_join (thread_ids[i], &ptr);

	elapse_time = elapsed_time (&begin);

	for(i = 0; i < num_workers; ++i) {
		if (verbose_flag) {
			printf("Thread %2i frees %ld blocks in %.2f seconds. %.2f free/sec.\n",
			       i, counters[i].c, elapse_time, ((double)counters[i].c/elapse_time));
		}
	}
	if (verbose_flag) printf("----------------------------------------------------------------\n");
	for(i = 0; i < num_workers; ++i) total += counters[i].c;
	if (verbose_flag)
	  printf("Total %ld freed in %.2f seconds. %.2fM free/second\n",
		 total, elapse_time, ((double) total/elapse_time)*1e-6);
	else
	  printf("%.0f\n", (double)total/elapse_time);

	if (verbose_flag) printf("Program done\n");
	return(0);
}

void usage(char *prog)
{
	printf("%s [-w workers] [-t run_time] [-d] [-v]\n", prog);
	printf("\t -w number of producer threads (and number of consumer threads), default %d\n", num_workers_default);
	printf("\t -t run time in seconds, default 20.0 seconds.\n");
	printf("\t -s size of object to allocate (default %d bytes) (specify -1 to get many different object sizes)\n", DEFAULT_OBJECT_SIZE);
	printf("\t -d debug mode\n");
	printf("\t -v verbose mode (-v -v produces more verbose)\n");
	exit(1);
}

int main(int argc, char **argv)
{
    new_malloc_init();
	int c;
	while ((c = getopt(argc, argv, "w:t:ds:v")) != -1) {

		switch (c) {

		case 'w':
			num_workers = atoi(optarg);
			break;
		case 't':
			run_time = atof(optarg);
			break;
		case 'd':
			debug_flag = 1;
			break;
		case 's':
			object_size = atoi(optarg);
			break;
		case 'v':
			verbose_flag++;
			break;
		default:
			usage(argv[0]);
		}
	}

	/* allocate memory for working arrays */
	thread_ids = (pthread_t *) xmalloc(sizeof(pthread_t) * num_workers * 2);
	counters = (struct counter *) xmalloc(sizeof(*counters) * num_workers);

	run_memory_free_test();
	while (batches) {
	  struct batch *b = batches;
	  batches = b->next_batch;
	  for (int i = 0 ; i < OBJECTS_PER_BATCH; i++) {
	    xfree(b->objects[i]);
	  }
	  xfree(b);
	}
	return 0;
}
