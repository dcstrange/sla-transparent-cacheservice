#include <stdio.h>
#include <stdlib.h>
#include "shmlib.h"
#include "cache.h"
#include "hashtable_utils.h"
#include "global.h"
#include "mcheck.h"

#define isSameTag(tag1,tag2) (tag1.offset == tag2.offset)
extern void _LOCK(pthread_mutex_t* lock);
extern void _UNLOCK(pthread_mutex_t* lock);

SSDBufHashBucket* hashtb_clean;
SSDBufHashBucket* hashtb_dirty;

static SSDBufHashBucket* hashitem_freelist;
static SSDBufHashBucket* topfree_ptr;
static SSDBufHashBucket* buckect_alloc();

static long insertCnt,deleteCnt;
static SSDBufHashBucket * getSSDBufHashBucket(SSDBufTag ssd_buf_tag, int cache_type);

static void freebucket(SSDBufHashBucket* bucket);
int HashTab_Init()
{
    insertCnt = deleteCnt = 0;
    hashtb_clean = (SSDBufHashBucket*)malloc(sizeof(SSDBufHashBucket)*NTABLE_CLEAN_CACHE);
    hashtb_dirty = (SSDBufHashBucket*)malloc(sizeof(SSDBufHashBucket)*NTABLE_DIRTY_CACHE);

    hashitem_freelist = (SSDBufHashBucket*)malloc(sizeof(SSDBufHashBucket)*(NTABLE_CLEAN_CACHE + NTABLE_DIRTY_CACHE));
    topfree_ptr = hashitem_freelist;

    if(hashtb_clean == NULL || hashtb_dirty == NULL || hashitem_freelist == NULL)
        return -1;

    SSDBufHashBucket* bucket_clean = hashtb_clean;
    SSDBufHashBucket* bucket_dirty = hashtb_dirty;
    SSDBufHashBucket* freebucket = hashitem_freelist;

    size_t i = 0;
    for(i = 0; i < NTABLE_CLEAN_CACHE; bucket_clean++, i++)
    {
        bucket_clean->desp_serial_id = -1;
        bucket_clean->hash_key.offset = -1;
        bucket_clean->next_item = NULL;
    }
    for(i = 0; i < NTABLE_DIRTY_CACHE; bucket_dirty++, i++)
    {
        bucket_dirty->desp_serial_id = -1;
        bucket_dirty->hash_key.offset = -1;
        bucket_dirty->next_item = NULL;
    }
    for(i = 0; i < NTABLE_CLEAN_CACHE + NTABLE_DIRTY_CACHE; freebucket ++, i++)
    {
        freebucket->desp_serial_id = -1;
        freebucket->hash_key.offset = -1;
        freebucket->next_item = freebucket + 1;
    }
    hashitem_freelist[NTABLE_CLEAN_CACHE + NTABLE_DIRTY_CACHE - 1].next_item = NULL;
    return 0;
}

static
SSDBufHashBucket * getSSDBufHashBucket(SSDBufTag ssd_buf_tag, int cache_type){

    unsigned long hashcode;
    SSDBufHashBucket * bucket;
    if(cache_type == 0){
        hashcode = (ssd_buf_tag.offset / SSD_BUFFER_SIZE) % NTABLE_CLEAN_CACHE;
        bucket = hashtb_clean + hashcode;
        return bucket;
    }
    else if (cache_type == 1){
        hashcode = (ssd_buf_tag.offset / SSD_BUFFER_SIZE) % NTABLE_DIRTY_CACHE;
        bucket = hashtb_dirty + hashcode;
        return bucket;
    }
    return NULL;
}

long HashTB_Lookup(SSDBufTag ssd_buf_tag, int cache_type)
{
    if (DEBUG)
        printf("[INFO] Lookup ssd_buf_tag: %lu\n",ssd_buf_tag.offset);

    SSDBufHashBucket *nowbucket = getSSDBufHashBucket(ssd_buf_tag,cache_type);
    while (nowbucket != NULL)
    {
        if (isSameTag(nowbucket->hash_key, ssd_buf_tag))
        {
            return nowbucket->desp_serial_id;
        }
        nowbucket = nowbucket->next_item;
    }

    return -1;
}

long HashTab_Insert(SSDBufTag ssd_buf_tag, int cache_type, long desp_serial_id)
{
    if (DEBUG)
        printf("[INFO] Insert buf_tag: %lu\n",ssd_buf_tag.offset);

    insertCnt++;
    //printf("hashitem alloc times:%d\n",insertCnt);

    SSDBufHashBucket *nowbucket = getSSDBufHashBucket(ssd_buf_tag, cache_type);
    if(nowbucket == NULL)
    {
        printf("[ERROR] Insert HashBucket: Cannot get HashBucket.\n");
        exit(1);
    }
    while (nowbucket->next_item != NULL)
    {
        nowbucket = nowbucket->next_item;
    }

    SSDBufHashBucket* newitem;
    if((newitem  = buckect_alloc()) == NULL)
    {
        printf("hash bucket alloc failure\n");
        exit(-1);
    }
    newitem->hash_key = ssd_buf_tag;
    newitem->desp_serial_id = desp_serial_id;
    newitem->next_item = NULL;

    nowbucket->next_item = newitem;
    return 0;
}

long HashTab_Delete(SSDBufTag ssd_buf_tag, int cache_type)
{
    if (DEBUG)
        printf("[INFO] Delete buf_tag: %lu\n",ssd_buf_tag.offset);

    deleteCnt++;
    //printf("hashitem free times:%d\n",deleteCnt++);

    long del_id;
    SSDBufHashBucket *delitem;
    SSDBufHashBucket *nowbucket = getSSDBufHashBucket(ssd_buf_tag, cache_type);

    while (nowbucket->next_item != NULL)
    {
        if (isSameTag(nowbucket->next_item->hash_key, ssd_buf_tag))
        {
            delitem = nowbucket->next_item;
            del_id = delitem->desp_serial_id;
            nowbucket->next_item = delitem->next_item;
            freebucket(delitem);
            return del_id;
        }
        nowbucket = nowbucket->next_item;
    }
    return -1;
}

static SSDBufHashBucket* buckect_alloc()
{
    if(topfree_ptr == NULL)
        return NULL;
    SSDBufHashBucket* freebucket = topfree_ptr;
    topfree_ptr = topfree_ptr->next_item;
    return freebucket;
}

static void freebucket(SSDBufHashBucket* bucket)
{
    bucket->next_item = topfree_ptr;
    topfree_ptr = bucket;
}
