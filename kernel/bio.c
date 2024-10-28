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

#define NBUCKETS 13

struct bcache {
  struct spinlock lock[NBUCKETS];     // 每个桶的锁
  struct buf buf[NBUF];               // 缓存块数组
  struct buf bucket[NBUCKETS];        // 哈希桶头节点
  int freelist[NBUCKETS];            // 每个桶的空闲块数量
} bcache;

// 链表操作辅助函数
static void
buf_insert_head(struct buf *head, struct buf *b)
{
  b->next = head->next;
  b->prev = head;
  head->next->prev = b;
  head->next = b;
}

static void
buf_insert_tail(struct buf *head, struct buf *b)
{
  b->next = head;
  b->prev = head->prev;
  head->prev->next = b;
  head->prev = b;
}

static struct buf*
buf_remove_lru(struct buf *head)
{
  struct buf *b;
  for(b = head->prev; b != head; b = b->prev) {
    if(b->refcnt == 0) {
      b->next->prev = b->prev;
      b->prev->next = b->next;
      return b;
    }
  }
  return 0;
}

static uint
hash(uint dev, uint blockno)
{
  return blockno % NBUCKETS;
}

// 在文件开头，其他函数之前添加这些声明
static int find_richest_bucket(void);
static int steal_buffers(int target, int donor);

void
binit(void)
{
  struct buf *b;

  // 初始化每个桶的锁和链表
  for(int i = 0; i < NBUCKETS; i++) {
    initlock(&bcache.lock[i], "bcache");
    bcache.bucket[i].next = &bcache.bucket[i];
    bcache.bucket[i].prev = &bcache.bucket[i];
    bcache.freelist[i] = 0;
  }

  // 将缓存块均匀分配到各个桶中
  for(b = bcache.buf; b < bcache.buf + NBUF; b++) {
    int i = (b - bcache.buf) % NBUCKETS;
    initsleeplock(&b->lock, "buffer");
    buf_insert_head(&bcache.bucket[i], b);
    bcache.freelist[i]++;
  }
}

// 在 bget 函数之前定义这些辅助函数
static int
find_richest_bucket(void)
{
  int max_free = 0;
  int donor = -1;
  
  for(int i = 0; i < NBUCKETS; i++) {
    if(bcache.freelist[i] > max_free) {
      max_free = bcache.freelist[i];
      donor = i;
    }
  }
  
  return donor;
}

static int
steal_buffers(int target, int donor)
{
  if(donor < 0) return 0;
  
  acquire(&bcache.lock[donor]);
  int steal_count = bcache.freelist[donor] / 2;
  
  if(steal_count > 0) {
    bcache.freelist[target] += steal_count;
    bcache.freelist[donor] -= steal_count;
    
    for(int i = 0; i < steal_count; i++) {
      struct buf *stolen = buf_remove_lru(&bcache.bucket[donor]);
      if(stolen)
        buf_insert_tail(&bcache.bucket[target], stolen);
    }
  }
  
  release(&bcache.lock[donor]);
  return steal_count;
}

// 然后是 bget 函数的定义
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int bucket = hash(dev, blockno);
  
  acquire(&bcache.lock[bucket]);

  // 在目标桶中查找块
  for(b = bcache.bucket[bucket].next; b != &bcache.bucket[bucket]; b = b->next) {
    if(b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      bcache.freelist[bucket] -= (b->refcnt == 1);
      release(&bcache.lock[bucket]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 如果当前桶没有空闲块，从其他桶偷一半
  if(bcache.freelist[bucket] == 0) {
    int donor = find_richest_bucket();
    if(donor < 0 || steal_buffers(bucket, donor) == 0) {
      panic("bget: no buffers");
    }
  }

  // 分配一个空闲块
  for(b = bcache.bucket[bucket].prev; b != &bcache.bucket[bucket]; b = b->prev) {
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      bcache.freelist[bucket]--;
      release(&bcache.lock[bucket]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  panic("bget: no buffers");
}

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

void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int bucket = hash(b->dev, b->blockno);
  acquire(&bcache.lock[bucket]);
  
  b->refcnt--;
  if(b->refcnt == 0) {
    // 将释放的块移到链表头部（最近使用）
    b->next->prev = b->prev;
    b->prev->next = b->next;
    buf_insert_head(&bcache.bucket[bucket], b);
    bcache.freelist[bucket]++;
  }
  
  release(&bcache.lock[bucket]);
}

void
bpin(struct buf *b)
{
  int bucket = hash(b->dev, b->blockno);
  acquire(&bcache.lock[bucket]);
  b->refcnt++;
  release(&bcache.lock[bucket]);
}

void
bunpin(struct buf *b)
{
  int bucket = hash(b->dev, b->blockno);
  acquire(&bcache.lock[bucket]);
  b->refcnt--;
  release(&bcache.lock[bucket]);
}
