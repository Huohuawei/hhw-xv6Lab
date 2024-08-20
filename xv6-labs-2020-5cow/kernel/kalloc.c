// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// NEW
// 引用计数数据结构结构
// 并声明一个全局实例 ref以供调用
// xv6最多可分配的进程数为64 故只需要int8即可
struct pagerefcnt {
  struct spinlock lock;
  uint8 refcount[PHYSTOP / PGSIZE]; // 最大物理地址除以一个物理页面的大小
} ref;

// NEW
// 当引用物理页面的用户页表数量增加或者减少时
// 在引用计数数组中进行记录
void incref(uint64 va) {
  acquire(&ref.lock); // 上锁
  if(va < 0 || va > PHYSTOP)
    panic("wrong virtual address");
  ref.refcount[va / PGSIZE]++; // 引用数自增
  release(&ref.lock); // 释放锁
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");

  // NEW
  initlock(&ref.lock, "ref"); // 初始化自旋锁

  freerange(end, (void*)PHYSTOP);
}

// Changed
// 修改了kfree后会导致初始化时引用计数为-1
// 故此处初始化为1避免错误
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    ref.refcount[(uint64)p / PGSIZE] = 1; //这里设置为1再kfree就变成0了
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// Changed 只有当引用计数为0时才将页面放回空闲列表
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.

  // NEW 引用计数大于0时直接返回
  acquire(&ref.lock);
  if(--ref.refcount[(uint64)pa / PGSIZE] > 0) {
    release(&ref.lock);
    return;
  }
  release(&ref.lock);
  
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// Changed 在分配页面时 引用计数置为1
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
    acquire(&ref.lock);
    ref.refcount[(uint64)r / PGSIZE] = 1; // 新分配页面的引用计数置为1
    release(&ref.lock);
  }
    
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
