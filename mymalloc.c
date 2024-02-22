#define _DEFAULT_SOURCE
#define _BSD_SOURCE 
#include <malloc.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <debug.h>
#include <pthread.h>
#include <sys/mman.h>

#define BLOCK_SIZE sizeof(block_t)
#define PAGE_SIZE sysconf(_SC_PAGESIZE)

// block data structure
typedef struct block {
  size_t size;        // How many bytes beyond this block have been allocated in the heap
  struct block *next; // Where is the next block in your linked list
  int free;           // Is this memory free, i.e., available to give away?
  int debug;
} block_t;

// global block to represent the head block in our linked list of data
block_t* HEAD = NULL;

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;

// helper function for debugging
void print_blocks() {
  pthread_mutex_lock(&mutex1);
  block_t* iterator = HEAD;
  int counter = 0;
  while(iterator != NULL) {
    debug_printf("Block %d has size %zu bytes with free %d\n", counter++, iterator->size, iterator->free);
    iterator = iterator->next;
  }
  pthread_mutex_unlock(&mutex1);
  pthread_mutex_destroy(&mutex1);
}

// helper to get the tail of our linked list
block_t* get_tail() {
  block_t* iterator = HEAD;
  while(iterator != NULL) {
    if (iterator->next == NULL) {
      return iterator;
    } else {
      iterator = iterator->next;
    }
  }
  return NULL;
}

// traverses our list and coalesces adjacent free blocks
void coalescing() {
  block_t* previous_block = NULL;
  block_t* iterator = HEAD;
  size_t size_so_far = 0;
  int block_coalesced = 0;
  while(iterator != NULL) {
    // code to make sure that we are not crossing our pages in memory since this will result in a fault
    // due to mmap not garunteing all pages to be mapped adjacently
    if ((block_coalesced == 0) 
    && (previous_block != NULL) 
    && (size_so_far == 0)) {
      size_so_far += previous_block->size + BLOCK_SIZE + BLOCK_SIZE + iterator->size;
    } else if (block_coalesced == 0 && previous_block != NULL) {
      size_so_far += BLOCK_SIZE + iterator->size;
    }
    // coalesce block if it passes the conditions
    if ((previous_block != NULL) 
    && (previous_block->free == 1) 
    && (iterator->free == 1)
    && (PAGE_SIZE >= BLOCK_SIZE + previous_block->size + BLOCK_SIZE + iterator->size)
    && (size_so_far <= PAGE_SIZE)) {
      previous_block->size += iterator->size + BLOCK_SIZE;
      block_coalesced == 1;
      previous_block->next = iterator->next;
    } else {
      // if no coalesce we do not need to skip
      block_coalesced == 0;
      previous_block = iterator;
    }
    iterator = iterator->next;
    // reset the size of data seen so far
    if (size_so_far > PAGE_SIZE) {
      size_so_far = 0;
    }
  }

  // deals with removing any blocks that have been coalesced into a page
  previous_block = NULL;
  iterator = HEAD;
  block_t* block_to_free = NULL;
  size_t s = 0;
  while(iterator != NULL) {
    // we are removing the head
    if (iterator == HEAD 
    && (iterator->size + BLOCK_SIZE >= PAGE_SIZE)
    && (iterator->free == 1)) {
      block_to_free = HEAD;
      s = iterator->size;
      HEAD = HEAD->next;
      break;
    }
    // we have found the block we want to remove
    else if ((iterator->size + BLOCK_SIZE >= PAGE_SIZE)
    && ((iterator->free == 1))) {
      block_to_free = iterator;
      s = iterator->size;
      previous_block->next = block_to_free->next;
      break;
    }
    // iterate
    else {
      previous_block = iterator;
      iterator = iterator->next;
    }
  }
  // munmap the memory
  if(block_to_free != NULL) {
    if (munmap(block_to_free, PAGE_SIZE) == -1) {
      debug_printf("munmap failed\n");
    }
  }
}

// allocates memory for a block of size _s_ where the old end of the linked list is cdr
block_t* malloc_block(size_t s, block_t* cdr) {
  size_t printer = s;
  // allocate memory in terms of page sizes
  size_t alloc = PAGE_SIZE;
  if (s >= PAGE_SIZE) {
    int num_pages = s / PAGE_SIZE;
    num_pages = (0 == (s % PAGE_SIZE)) ? num_pages : num_pages + 1;
    alloc = num_pages * PAGE_SIZE;
  }

  // allocate the memory and set up the block
  block_t* new_block = (block_t*) mmap(NULL, alloc, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (new_block == MAP_FAILED) {
    debug_printf("mmap failed to allocate %zu bytes\n\n\n", s);
    return NULL;
  }
 
  if (s >= PAGE_SIZE) {
    new_block->size = alloc - BLOCK_SIZE;
    printer = alloc;
  } 
  else {
    new_block->size = s - BLOCK_SIZE;
  }
  new_block->next = NULL;
  new_block->free = 0;
  // if we are appending to the end we set the old end to point to the new final block
  if (cdr != NULL) {
    cdr->next = new_block;
  }
  

  // if the request is < PAGE_SIZE we then need to make a block to make the extra space from the 4096 usable
  if (s < PAGE_SIZE) {
    size_t remainder = PAGE_SIZE - s;
    if (remainder >= (BLOCK_SIZE + 1)) {
      block_t* filler_block = (void *) new_block + BLOCK_SIZE + new_block->size;
      filler_block->free = 1;    
      filler_block->size = remainder - BLOCK_SIZE;
      filler_block->next = new_block->next;
      new_block->next = filler_block;
    }
    coalescing();
  } 
  debug_printf("malloc %zu bytes\n", alloc);
  return new_block;
}

// helper which finds the first suitable block for a given request, or returns the tail
block_t* suitable_block(size_t s) {
  block_t* tail = NULL;
  block_t* iterator = HEAD;
  while(iterator != NULL) {
    if (iterator->free == 1 && iterator->size >= s) {
      return iterator;
    } else {
      tail = iterator;
      iterator = iterator->next;
    }
  }
  return tail;
}

// our implementation of malloc
void *mymalloc(size_t s) {
  assert(s > 0);
  //TODO debug_printf("Before malloc\n"); //TODO
  //TODO print_blocks(); //TODO
  pthread_mutex_lock(&mutex1);

  // if our request is >= than a PAGE_SIZE we calculate the number of pages needed and allocate
  if (s + BLOCK_SIZE >= PAGE_SIZE) {
    block_t* new_block_at_end = malloc_block(s + BLOCK_SIZE, get_tail());
    if (HEAD == NULL) {
      HEAD = new_block_at_end;
    }
    pthread_mutex_unlock(&mutex1);
    return new_block_at_end + 1;
  }

  // below code is exectuted in the event that the request is less than a page
  block_t* first_suitable_block = suitable_block(s);
  // see if we are initializing the head
  if (HEAD == NULL) {
    //TODO debug_printf("After malloc\n"); //TODO
    HEAD = malloc_block(s + BLOCK_SIZE, NULL);
    pthread_mutex_unlock(&mutex1);
    pthread_mutex_destroy(&mutex1);
    //TODO print_blocks(); //TODO
    return HEAD + 1;
  }
  // see if we are filling a usable block
  else if (first_suitable_block != NULL && first_suitable_block->free == 1 && first_suitable_block->size >= s) {
    first_suitable_block->free = 0;
    size_t remainder = first_suitable_block->size - s;
    if (remainder >= (BLOCK_SIZE + 1)) {
      first_suitable_block->size = s;
      block_t* filler_block  = (void *) first_suitable_block + BLOCK_SIZE + first_suitable_block->size; 
      filler_block->free = 1;
      filler_block->size = remainder - BLOCK_SIZE;
      filler_block->next = first_suitable_block->next;

      first_suitable_block->next = filler_block;
    }
    //TODO debug_printf("After malloc\n"); //TODO
    //TODO print_blocks(); //TODO
    debug_printf("malloc %zu bytes\n", s);
    pthread_mutex_unlock(&mutex1);
    pthread_mutex_destroy(&mutex1);
    return first_suitable_block + 1;
  }
  // we need to add a block to the end
  else if (first_suitable_block != NULL) {
    //TODO debug_printf("After malloc\n"); //TODO
    block_t* new_block_at_end = malloc_block(s + BLOCK_SIZE, first_suitable_block);
    pthread_mutex_unlock(&mutex1);
    pthread_mutex_destroy(&mutex1);
    //TODO print_blocks(); //TODO
    assert(new_block_at_end->size > 0);
    return new_block_at_end + 1;
  } 
  // error in the linked list
  else {
    debug_printf("ERROR bad linked list\n");
    pthread_mutex_unlock(&mutex1);
    pthread_mutex_destroy(&mutex1);
    return NULL;
  }
}

// our implementation of calloc
void *mycalloc(size_t nmemb, size_t s) {
  size_t size = nmemb * s;
  void *calloced = mymalloc(size);
  memset(calloced, 0, size);
  debug_printf("calloc %zu bytes\n", s);
  return calloced;
}

// our implimentation of free
void myfree(void *ptr) {
  assert(ptr != NULL);
  //TODO debug_printf("Before free\n"); //TODO  
  //TODO print_blocks(); //TODO
  pthread_mutex_lock(&mutex1);
  block_t *block_to_free = (block_t *) ptr - 1;
  block_to_free->free = 1;

  // if the request size which we want to free
  // is >= a PAGE_SIZE we remove the block from the linked list
  size_t s = block_to_free->size;
  if (s + BLOCK_SIZE >= PAGE_SIZE) {
    block_t* previous_block = NULL;
    block_t* iterator = HEAD;
    while(iterator != NULL) {
      // we are removing the head
      if (previous_block == NULL && (block_to_free == iterator)) {
        HEAD = HEAD->next;
        break;
      } 
      // we have found the block we want to remove
      else if (block_to_free == iterator) {
        previous_block->next = block_to_free->next;
        break;
      } 
      // iterate
      else {
        previous_block = iterator;
        iterator = iterator->next;
      }
    }
    // munmap the memory
    if(munmap(block_to_free, (s + BLOCK_SIZE)) == -1) {
      debug_printf("munmap failed\n");
    }
  }
  coalescing();
  pthread_mutex_unlock(&mutex1);
  pthread_mutex_destroy(&mutex1);
  //TODO debug_printf("After free\n"); //TODO 
  //TODO print_blocks(); //TODO

  debug_printf("freed %zu bytes\n", s + BLOCK_SIZE);
}
