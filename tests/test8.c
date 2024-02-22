// Test interleaved mallocs and frees
// writing to all allocated locations with all 1s
// requesting random multiples of 8 bytes up to 2**22

#ifndef DEMO_TEST
#include <malloc.h>
extern int rand(void);
#else
#include <stdlib.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

int allones = ~0; // allones for int

int *testmalloc(int size) {
  int *data = (int *) malloc(size);
  if (data != NULL) {
    // void *memset(void *s, int c, size_t n);
    memset((void *) data, allones, size);
  }
  return data;
}

void *thread_func(void *) {
  int i;
  for (i = 0; i < 1; i++) {
    int *data = (int *) testmalloc(8);
    int *data1 = (int *) testmalloc(16);
    int *data2 = (int *) testmalloc(32);
    int *data3 = (int *) testmalloc(64);
    int *data4 = (int *) testmalloc(128);
    int *data5 = (int *) testmalloc(256);
    int *data6 = (int *) testmalloc(512);
    int *data7 = (int *) testmalloc(1024);

    free(data);
    free(data1);
    free(data2);
    free(data3);
    free(data4);
    free(data5);
    free(data6);
    free(data7);
  }
}

int main() {
  pthread_t thread[3];

  int i;
  for (i = 0; i < 2; i++) {
    pthread_create(&thread[i], NULL, thread_func, NULL);
  }

  for (i = 0; i < 2; i++) {
    pthread_join(thread[i], NULL);
  }

  return 0;

}
