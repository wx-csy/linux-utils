#define _GNU_SOURCE

#include "malloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>

//=========================================================
// Your implementations HERE
//=========================================================

static void die(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  exit(EXIT_FAILURE);
}

static const size_t page_size = 1024 * 64;

#define PAGE_MINI_BLOCK     0
#define PAGE_HUGE_BLOCK     1

//========================================================
//  FREE PAGE MANAGEMENT
//========================================================

typedef struct free_page {
  void *next_page;
} free_page;

static free_page *free_page_head = NULL;
static int free_page_count = 0;

static pthread_mutex_t fp_mutex = PTHREAD_MUTEX_INITIALIZER;

static void release_free_page(free_page *page);
static free_page *alloc_free_page() {
  pthread_mutex_lock(&fp_mutex);
  free_page *current_page = free_page_head;
  if (current_page != NULL) {
    free_page_head = free_page_head->next_page;
    free_page_count--;
    pthread_mutex_unlock(&fp_mutex);
  } else {
    pthread_mutex_unlock(&fp_mutex);
    void *page = mmap(NULL, page_size << 1, PROT_READ | PROT_WRITE, 
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == NULL) return NULL;
    size_t margin = ((uintptr_t)page) & (page_size - 1);
    if (margin == 0) {
      current_page = page;
      release_free_page((free_page *)(page + page_size));
    } else {
      current_page = (free_page *)((uint8_t *)(page) + 
          (page_size - margin));
      munmap(page, page_size - margin);
      munmap((uint8_t *)(current_page) + page_size, margin);
    }
  }
  return current_page;
}

static void release_free_page(free_page *page) {
  pthread_mutex_lock(&fp_mutex);
  if (free_page_count >= 32) {
    pthread_mutex_unlock(&fp_mutex);
    munmap(page, page_size);
  } else {
    page->next_page = free_page_head;
    free_page_head = page;
    free_page_count++;
    pthread_mutex_unlock(&fp_mutex);
  }
}

//========================================================
//  MINI PAGES
//========================================================

typedef struct page_mini_block {
  uint16_t page_type;
  uint16_t active;
  uint32_t used_block;
  uint32_t freed_block;
  uint32_t ptr;
} page_mini_block;

#define NUM_PAGE_MINI_BLOCK   4
page_mini_block *current_page_mini_block[NUM_PAGE_MINI_BLOCK];

static page_mini_block *alloc_page_mini_block() {
  page_mini_block *page = (page_mini_block *)alloc_free_page();
  if (page == NULL) return NULL;
  page->page_type = PAGE_MINI_BLOCK;
  page->active = 1;
  page->used_block = 0;
  page->freed_block = 0;
  page->ptr = page_size;
  return page;
}

static void free_mini_block(page_mini_block *page);
static void *alloc_mini_block(size_t sz) {
  void *ret;
  // try to find a page with enough space by atomic xchg operation
  page_mini_block *page = NULL;
  for (int i = 0; i < NUM_PAGE_MINI_BLOCK; i++) {
    page = __atomic_exchange_n(current_page_mini_block + i, page, 
        __ATOMIC_SEQ_CST);
    if (page != NULL && sizeof(page_mini_block) + sz <= page->ptr)
      goto alloc_page_succ;
  }
  // fail to find a suitable page, allocate a new one
  page = alloc_page_mini_block();
  if (page == NULL) return NULL;
alloc_page_succ:
  ret = (uint8_t *)page + (page->ptr -= sz);
  page->used_block++;

  // put back the page into the list
  void *expected = NULL;
  for (int i = 0; i < NUM_PAGE_MINI_BLOCK; i++) {
    if (__atomic_compare_exchange_n(current_page_mini_block + i, 
          &expected, page, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
      goto putback_succ;
    expected = NULL;
  }
  // fail to put back the page
  // add a reference count to avoid atomicity violation
  page->used_block++;
  // mark the page inactive; once the page becomes inactive, 
  // the number of used blocks never increases
  __atomic_store_n(&page->active, 0, __ATOMIC_SEQ_CST);
  // decrease the reference count
  free_mini_block(page);
putback_succ:
  return ret;
}

static void free_mini_block(page_mini_block *page) {
  uint32_t freed_block = __atomic_add_fetch(&page->freed_block, 1, 
      __ATOMIC_SEQ_CST);
  if (__atomic_load_n(&page->active, __ATOMIC_SEQ_CST) == 0 
      && page->used_block == freed_block)
    release_free_page((free_page *)page);
}

//================================================
//  HUGE PAGES
//================================================

typedef struct page_huge_block {
  uint16_t page_type;
  uint16_t active;
  size_t tot_size;
} page_huge_block;

static void *alloc_huge_block(size_t size) {
  size_t tot_size = size + sizeof(page_huge_block) + page_size;
  page_huge_block *page = mmap(NULL, tot_size,
      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (page == MAP_FAILED) return NULL;
  size_t margin = (uintptr_t)page & (page_size - 1);
  munmap(page, page_size - margin);
  tot_size -= page_size - margin;
  page = (page_huge_block *)((uint8_t*)page + (page_size - margin));
  page->page_type = PAGE_HUGE_BLOCK;
  page->active = 1;
  page->tot_size = tot_size;
  return page + sizeof(page_huge_block);
}

static void free_huge_block(page_huge_block *page) {
  munmap(page, page->tot_size);
}

void *do_malloc(size_t size) {
  void *ret;
  if (size < page_size / 4) {
    ret = alloc_mini_block(size);
  } else {
    ret = alloc_huge_block(size);
  }
  return ret;
}

void do_free(void *ptr) {
  void *page = (void *)((uintptr_t)(ptr) & (-page_size));
  switch (*(uint16_t *)page) {
    case PAGE_MINI_BLOCK:
      free_mini_block(page);
      break;
    case PAGE_HUGE_BLOCK:
      free_huge_block(page);
      break;
    default:
      die("free(): Heap corruption detected!\n");
  }
}

