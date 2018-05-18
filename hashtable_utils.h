#ifndef _SSDBUFTABLE_H
#define _SSDBUFTABLE_H 1

#include "cache.h"

typedef struct SSDBufHashBucket
{
    SSDBufTag 			hash_key;
    long    				desp_serial_id;
    struct SSDBufHashBucket 	*next_item;
} SSDBufHashBucket;

extern SSDBufHashBucket* ssd_buf_hashtable;
extern int HashTab_Init();
extern long HashTB_Lookup(SSDBufTag ssd_buf_tag, int cache_type);
extern long HashTab_Insert(SSDBufTag ssd_buf_tag, int cache_type, long desp_serial_id);
extern long HashTab_Delete(SSDBufTag ssd_buf_tag, int cache_type);

#endif   /* SSDBUFTABLE_H */
