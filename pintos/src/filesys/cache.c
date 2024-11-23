#include "filesys/cache.h"
#include "devices/block.h"
#include <string.h>
#include "threads/malloc.h"
#include "filesys/filesys.h"

int cache_cnt;
struct list_elem *cache_pointer;
struct list buffer_cache;

void cache_init()
{
    list_init(&buffer_cache);
    lock_init(&cache_lock);
    cache_cnt = 0;
    cache_pointer = NULL;
}

struct buffer_head *find_bce(block_sector_t idx)
{
    struct buffer_head *bh;
    struct list_elem *e;

    for (e = list_begin(&buffer_cache); e != list_end(&buffer_cache); e = list_next(e))
    {
        bh = list_entry(e, struct buffer_head, cache_elem);
        if (bh->sector == idx)
        {
            return bh;
        }
    }

    return NULL;
}

void cache_read(uint8_t *buffer, off_t bytes_read, int sector_ofs, block_sector_t sector_idx, int chunk_size)
{
    lock_acquire(&cache_lock);

    struct buffer_head *bh = find_bce(sector_idx);

    if (bh == NULL)
    {
        if (cache_cnt >= 64) /*flush*/
        {
            flush_victim();
        }
        /*make new buffer head*/
        bh = (struct buffer_head *)malloc(sizeof(struct buffer_head));
        bh->dirty = false;
        bh->access = false;
        bh->sector = sector_idx;
        bh->data = malloc(BLOCK_SECTOR_SIZE);
        insert_bce(bh);

        block_read(fs_device, sector_idx, bh->data);
        memcpy(buffer + bytes_read, bh->data + sector_ofs, chunk_size);
    }
    else
    {
        bh->access = true;
        memcpy(buffer + bytes_read, bh->data + sector_ofs, chunk_size);
    }

    lock_release(&cache_lock);
    return;
}

void cache_write(const uint8_t *buffer, off_t bytes_written, int sector_ofs, block_sector_t sector_idx, int chunk_size, int sector_left)
{
    lock_acquire(&cache_lock);
    struct buffer_head *bh = find_bce(sector_idx);

    if (bh == NULL)
    {
        if (cache_cnt >= 64)
        {
            flush_victim();
        }
        bh = (struct buffer_head *)malloc(sizeof(struct buffer_head));
        bh->dirty = true;
        bh->access = false;
        bh->sector = sector_idx;
        bh->data = malloc(BLOCK_SECTOR_SIZE);
        insert_bce(bh);

        if (sector_ofs > 0 || chunk_size < sector_left)
            block_read(fs_device, sector_idx, bh->data);
        else
            memset(bh->data, 0, BLOCK_SECTOR_SIZE);

        memcpy(bh->data + sector_ofs, buffer + bytes_written, chunk_size);
    }
    else
    {
        bh->dirty = true;
        bh->access = true;
        memcpy(bh->data + sector_ofs, buffer + bytes_written, chunk_size);
    }

    lock_release(&cache_lock);
    return;
}

struct list_elem *find_cache_pointer()
{
    if (list_empty(&buffer_cache))
        return NULL;
    if (cache_pointer == NULL || cache_pointer == list_end(&buffer_cache))
        return list_begin(&buffer_cache);
    return list_next(cache_pointer);
}

void flush_victim()
{
    cache_pointer = find_cache_pointer();

    struct buffer_head *victim;
    victim = list_entry(cache_pointer, struct buffer_head, cache_elem);

    while (victim->access)
    {
        victim->access = false;
        cache_pointer = find_cache_pointer();
        victim = list_entry(cache_pointer, struct buffer_head, cache_elem);
    }

    if (victim->dirty)
    {
        block_write(fs_device, victim->sector, victim->data);
        victim->dirty = false;
    }

    delete_bce(victim);
}

void cache_shutdown()
{
    lock_acquire(&cache_lock);
    struct list_elem *e = list_begin(&buffer_cache);
    struct list_elem *tmp;
    struct buffer_head *bh;

    while (e != list_end(&buffer_cache))
    {
        tmp = list_next(e);

        bh = list_entry(e, struct buffer_head, cache_elem);
        if (bh->dirty)
            block_write(fs_device, bh->sector, bh->data);

        delete_bce(bh);

        e = tmp;
    }

    lock_release(&cache_lock);
}

void insert_bce(struct buffer_head *bh)
{
    list_push_back(&buffer_cache, &bh->cache_elem);
    cache_cnt++;
}

void delete_bce(struct buffer_head *bh)
{
    if (cache_pointer == &bh->cache_elem)
        cache_pointer = find_cache_pointer();
    list_remove(&bh->cache_elem);
    free(bh->data);
    free(bh);
    cache_cnt--;
}