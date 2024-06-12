#ifndef MY_VM_H_INCLUDED
#define MY_VM_H_INCLUDED
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>


#define PGSIZE (4096)
#define PAGE_AMOUNT (unsigned int)(MEMSIZE / PGSIZE)
#define BITMAP_SIZE (PAGE_AMOUNT / 8)

#define MAX_MEMSIZE (4ULL * 1024 * 1024 * 1024)

#define ADDRESS_SPACE (32)
#define MEMSIZE (1024 * 1024 * 1024)

typedef unsigned long pte_t;

typedef unsigned long pde_t;

#define TLB_ENTRIES 512

typedef struct Entry {
    int vpn;
    int ppn;
} Entry;

typedef struct TLB {
    Entry *entries;
} TLB;

struct TLB cache;


int check_TLB(int vpn);
void add_TLB(int vpn, int ppn);
void print_TLB_missrate();

void set_physical_mem();
void *t_malloc(unsigned int num_bytes);
void t_free(void *va, int size);
int put_value(void *va, void *val, int size);
void get_value(void *va, void *val, int size);
void mat_mult(void *mat1, void *mat2, int size, void *answer);


#endif
