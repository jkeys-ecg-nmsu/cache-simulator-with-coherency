// Authored by: Jonathan Cook, PhD, Professor and Interim Department Head, NMSU 
//
// Threaded array computation program used to generate
// a trace of threaded array accesses, or also to time
// various configurations of computations.
//
// Compile using "gcc thtrace.c -lpthread -lm" on Linux
//
// Command-line options:
//    -s #     set array sizes to 2^^# (default 3 == 8 elements)
//    -t #     set number of threads to # (default 2)
//    -n #     set number of computation repetitions to # (default 1)
//    -d #     set debug level to # (0,1 useful)
//    -time    do not print out trace but print execution time instead
//    -reverse run even-thread array computations in reverse
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>

#define PAD (1024*256)

int *a;
int *b;
int *pad;

long size = 8;
int numThreads = 2;
int numRepetitions = 1;
int reverse = 0;
int trace = 1;
int debug = 0;
int numCores = 1;
pthread_barrier_t barrier;

void *threadComputation(void *data);

int main(int argc, char **argv)
{
   int i, stat;
   pthread_t *thread;
   struct timespec start, end;
   //cpu_set_t coreMask;
   long totalmsecs = 0;
   int numCores = sysconf(_SC_NPROCESSORS_ONLN);
   clock_gettime(CLOCK_REALTIME, &start);   
   for (i=1; i < argc; i++) {
      if (!strcmp(argv[i],"-s"))
         size = pow(2,strtol(argv[++i],0,10));
      if (!strcmp(argv[i],"-t"))
         numThreads = strtol(argv[++i],0,10);
      if (!strcmp(argv[i],"-d"))
         debug = strtol(argv[++i],0,10);
      if (!strcmp(argv[i],"-n"))
         numRepetitions = strtol(argv[++i],0,10);
      if (!strcmp(argv[i],"-time"))
         trace = 0;
      if (!strcmp(argv[i],"-reverse"))
         reverse = 1;
   }
   if (debug > 0)
      printf("Number of CPU cores: %d\n", numCores);
   if (debug > 0)
      printf("Using %d threads over %ld-element arrays...\n", numThreads, size);
   a = (int*) malloc(sizeof(int)*size);
   pad = (int*) malloc(sizeof(int)*PAD);
   b = (int*) malloc(sizeof(int)*size);
   if (debug > 0)
      printf("Creating threads...\n");
   pthread_barrier_init(&barrier,0,numThreads);
   thread = (pthread_t *) malloc(sizeof(pthread_t)*numThreads);
   for (i=0; i < numThreads; i++) {
      stat = pthread_create(&(thread[i]), 0, threadComputation, (void*) ((long) i));
      if (stat) {
          printf("Error creating thread %d: %d\n", i, stat);
          continue;
      }
      // not compiling for me right now
      //CPU_ZERO(&coreMask);
      //CPU_SET((i%numCores), &coreMask);
      //pthread_setaffinity_np(thread[i], sizeof(cpu_set_t), coreMask);
   }
   if (debug > 0)
      printf("Waiting for threads to finish...\n");
   for (i=0; i < numThreads; i++) {
      pthread_join(thread[i], 0);
   }
   if (debug > 0)
      printf("Threads are finished!\n");
   if (trace == 0) {
      clock_gettime(CLOCK_REALTIME, &end);
      totalmsecs = (end.tv_sec - start.tv_sec) * 1000;
      if (end.tv_nsec > start.tv_nsec)
         totalmsecs += (end.tv_nsec - start.tv_nsec) / 1000000;
      else
         totalmsecs -= (start.tv_nsec - end.tv_nsec) / 1000000;
      printf("Time spent: %ld msec\n",totalmsecs);
   }
   pthread_barrier_destroy(&barrier);
}
   
void *threadComputation(void *data)
{
   int i, reps, myTID, offset;
   myTID = (int) ((long) data);
   offset = myTID * (size/numThreads);
   for (i=0; i < size/numThreads; i++) {
      a[i+offset] = 99+i;
      b[i+offset] = 42+i;
      if (trace > 0) {
         printf("%d %p W\n", myTID, &(a[i+offset]));
         printf("%d %p W\n", myTID, &(b[i+offset]));
      }
      if (debug > 1)
         printf("a[%d]=%d b[%d]=%d ", i, a[i], i, b[i]);
   }
   if (debug > 1)
      printf("\n");
   if (debug > 0)
      printf("Thread %d done initializing\n",myTID);
   // barrier below makes sure all threads finish their
   // init part before any move on to compute over the arrays
   pthread_barrier_wait(&barrier);
   for (reps = 0; reps < numRepetitions; reps++) {
      if (reverse && myTID % 2 == 0) {
         for (i=size-1; i > 0; i--) {
            a[i] = a[i] * b[i];
            if (trace > 0) {
               printf("%d %p R\n", myTID, &(a[i]));
               printf("%d %p R\n", myTID, &(b[i]));
               printf("%d %p W\n", myTID, &(a[i]));
               if ((i+myTID)%2) usleep(1); // encourage interleaving
            }
         }
      } else {
         for (i=0; i < size; i++) {
            a[i] = a[i] * b[i];
            if (trace > 0) {
               printf("%d %p R\n", myTID, &(a[i]));
               printf("%d %p R\n", myTID, &(b[i]));
               printf("%d %p W\n", myTID, &(a[i]));
               if ((i+myTID)%2) usleep(1); // encourage interleaving
            }
         }
      }
   }
   if (debug > 1) {
      for (i=0; i < size; i++)
         printf("a[%d]=%d ", i, a[i]);
      printf("\n");
   }
   if (debug > 0)
      printf("Thread %d done computing\n",myTID);
   return (void*) 0;
}

