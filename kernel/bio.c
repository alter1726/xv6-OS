// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

//哈希表中的桶号索引，设为质数个数可以降低哈希冲突的可能
#define NBUFMAP_BUCKET  13
//哈希索引
#define BUFMAP_HASH(dev,blockno)  ((((dev)<<27)|(blockno))%NBUFMAP_BUCKET)

struct {
  // struct spinlock lock;
  struct buf buf[NBUF];

  struct spinlock eviction_lock;                  //驱逐锁
  struct buf bufmap[NBUFMAP_BUCKET];              //哈希表-散列桶
  struct spinlock bufmap_locks[NBUFMAP_BUCKET];   //桶锁
  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;
} bcache;

void
binit(void)
{
  //初始化桶锁
  for (int i = 0; i < NBUFMAP_BUCKET; i++)
  {
    initlock(&bcache.bufmap_locks[i],"bcache_bufmap");
    bcache.bufmap[i].next=0;
  }
  
  for (int i = 0; i < NBUF; i++)
  {
    //初始化缓存区块
    struct buf* b=&bcache.buf[i];
    initsleeplock(&b->lock,"buffer");
    b->lastused=0;
    b->refcnt=0;
    //将所有缓存块添加到bufmap[0]
    b->next=bcache.bufmap[0].next;
    bcache.bufmap[0].next=b;
  }
  initlock(&bcache.eviction_lock,"bcache_eviction");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  //哈希获取桶号
  uint key=BUFMAP_HASH(dev,blockno);

  acquire(&bcache.bufmap_locks[key]);

  // 编号为blockno的缓存区块是否已经在缓存区中
  for(b = bcache.bufmap[key].next; b ; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bufmap_locks[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  //不在缓存区
  release(&bcache.bufmap_locks[key]);     //防止死锁，释放当前桶锁
  acquire(&bcache.eviction_lock);         //防止blockno的缓存区块被重复创建，加上驱逐锁

  //释放桶锁到加驱逐锁的间隙有可能创建blockno的缓存区块，再作检查
  for ( b = bcache.bufmap[key].next; b ; b=b->next)
  {
    if(b->dev == dev && b->blockno == blockno){
      acquire(&bcache.bufmap_locks[key]);
      b->refcnt++;
      release(&bcache.bufmap_locks[key]);
      release(&bcache.eviction_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  
  //仍不在缓存区，此时只有驱逐锁，不持有桶锁。因此查询所有桶中的LRU buf
  struct buf* before_lrubuf=0;                  //LRU buf的前一个块
  uint holding_bucket=-1;                       //记录当前持有的桶锁

  //循环查询所有桶
  for (int i = 0; i < NBUFMAP_BUCKET; i++)
  {
    acquire(&bcache.bufmap_locks[i]);           //获取当前遍历的桶锁，在找到下一个LRU buf或驱逐内存之前都不释放
    int newfound=0;                             //是否在当前桶找到新的LRU buf

    for ( b = &bcache.bufmap[i]; b->next ; b=b->next)
    {
      if(b->next->refcnt==0&&(!before_lrubuf||b->next->lastused<before_lrubuf->next->lastused)){
        before_lrubuf=b;
        newfound=1;
      }
    }
    if(!newfound)
      release(&bcache.bufmap_locks[i]);         //没找到新的LRU buf，释放当前桶锁
    else{
      if(holding_bucket!=-1)
        release(&bcache.bufmap_locks[holding_bucket]);  //释放当前桶锁
      holding_bucket=i;                                 //把标记holding_bucket更改为当前桶锁编号
    }
  }
  
  if(!before_lrubuf)                //没找到任何一个LRU buf，则表示没有空闲缓存块了
    panic("bgfet: no buffers");
  
  b=before_lrubuf->next;            //b为LRU buf

  if(holding_bucket!=key){          //想要偷的块如果不在key桶，就要把块从所在的桶驱逐出来
    before_lrubuf->next=b->next;    
    release(&bcache.bufmap_locks[holding_bucket]);

    //把LRU buf添加到key桶
    acquire(&bcache.bufmap_locks[key]);
    b->next=bcache.bufmap[key].next;
    bcache.bufmap[key].next=b;
  }

  //设置新buf的字段
  b->dev=dev;
  b->blockno=blockno;
  b->refcnt=1;
  b->valid=0;

  //释放相关的锁
  release(&bcache.bufmap_locks[key]);
  release(&bcache.eviction_lock);
  acquiresleep(&b->lock);
  return b;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint key=BUFMAP_HASH(b->dev,b->blockno);
  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->lastused=ticks;
  }
  
  release(&bcache.bufmap_locks[key]);
}

void
bpin(struct buf *b) {
  uint key=BUFMAP_HASH(b->dev,b->blockno);

  acquire(&bcache.bufmap_locks[key]);
  b->refcnt++;
  release(&bcache.bufmap_locks[key]);
}

void
bunpin(struct buf *b) {
  uint key=BUFMAP_HASH(b->dev,b->blockno);

  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  release(&bcache.bufmap_locks[key]);
}


