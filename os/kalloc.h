#ifndef KALLOC_H
#define KALLOC_H
#include "riscv.h"
struct page{
	int ref;
};

void *kalloc();
void kfree(void *);
void kinit();
int get_page_ref(uint64 pa);
int get_page(uint64 pa);
int put_page(uint64 pa);
#endif // KALLOC_H