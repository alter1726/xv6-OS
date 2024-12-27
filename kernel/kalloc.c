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
} kmem[NCPU];           //每个CPU分配一个freelist，多个CPU并发分配物理内存不会相互竞争

char *kmem_lock_names[]={
    "kmem_cpu_0",
    "kmem_cpu_1",
    "kmem_cpu_2",
    "kmem_cpu_3",
    "kmem_cpu_4",
    "kmem_cpu_5",
    "kmem_cpu_6",
    "kmem_cpu_7",
};

void
kinit()
{
  for (int i = 0; i < NCPU; i++)
  { 
    //初始化所有锁
    initlock(&kmem[i].lock, kmem_lock_names[i]);
  }
  freerange(end, (void*)PHYSTOP); 
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off(); //关闭中断
  int cpu=cpuid();

  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);

  pop_off();  //开中断
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  
  int cpu=cpuid();

  acquire(&kmem[cpu].lock);

  if (!kmem[cpu].freelist)
  {                                         //当前CPU空闲列表为空时，去其他CPU偷内存
    int steal_pages=64;                      //偷64个内存页
    for (int i = 0; i < NCPU; i++)  
    { 
      if(i==cpu)  
        continue;                           //跳过当前CPU
      
      acquire(&kmem[i].lock);
      if (!kmem[i].freelist)
      {
        release(&kmem[i].lock);             //想偷页的CPU空闲列表也为空，则释放锁
        continue;
      }

      struct run* sr=kmem[i].freelist;
      while (sr&&steal_pages)               //循环将kmem[i]的空闲列表移动到kmem[cpu]中
      {
        kmem[i].freelist=sr->next;
        sr->next=kmem[cpu].freelist;
        kmem[cpu].freelist=sr;
        sr=kmem[i].freelist;
        steal_pages--;
      }

      release(&kmem[i].lock);

      if(steal_pages==0)                    //偷到64页后退出循环
        break;
    }
  }
  
  r = kmem[cpu].freelist;
  if(r)
    kmem[cpu].freelist = r->next;
  release(&kmem[cpu].lock);

  pop_off();
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
