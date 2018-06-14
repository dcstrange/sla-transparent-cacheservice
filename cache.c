#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <unistd.h>

#include "timerUtils.h"
#include "cache.h"
#include "hashtable_utils.h"

#include "strategies.h"

#include "smr-simulator/smr-simulator.h"
#include "smr-simulator/simulator_logfifo.h"

#include "shmlib.h"
#include "report.h"

#include "costmodel.h"

SSDBufDespCtrl  * DespCtrl_Clean,   * DespCtrl_Dirty;//ssd_buf_desp_ctrl;
SSDBufDesp      * Desps_Clean,      * Desps_Dirty;

/* If Defined R/W Cache Space Static Allocated */
double Proportion_Dirty;
blkcnt_t Max_Dirty_Cache, Max_Clean_Cache;

static int          init_SSDDescriptorBuffer();
static int          init_StatisticObj();
static void         flushSSDBuffer(SSDBufDesp * ssd_buf_hdr);

static SSDBufDesp*  allocSSDBuf(int cache_type, SSDBufTag ssd_buf_tag);
void freeSSDBuf(int cache_type, SSDBufDesp* ssd_buf_hdr);

static SSDBufDesp*  pop_freebuf(int cache_type);
static int          push_freebuf(SSDBufDesp* freeDesp, int cache_type);

static int          initStrategySSDBuffer();
static long         Strategy_Desp_LogOut();
static int          Strategy_Desp_HitIn(SSDBufDesp* desp);
static int         Strategy_Desp_LogIn(SSDBufDesp* desp);
static long map_cache_to_strategy(int cache_type, long desp_id);
long map_strategy_to_cache(int cache_type, long desp_id);

//#define isSamebuf(SSDBufTag tag1, SSDBufTag tag2) (tag1 == tag2)
#define CopySSDBufTag(objectTag,sourceTag) (objectTag = sourceTag)
#define IsDirty(flag) ( (flag & SSD_BUF_DIRTY) != 0 )
#define IsClean(flag) ( (flag & SSD_BUF_DIRTY) == 0 )

void                _LOCK(pthread_mutex_t* lock);
void                _UNLOCK(pthread_mutex_t* lock);

static int check_hit(SSDBufTag ssd_buf_tag, int cache_type, long * despId);
/* stopwatch */
static timeval tv_start, tv_stop;
static timeval tv_bastart, tv_bastop;
static timeval tv_cmstart, tv_cmstop;
microsecond_t msec_r_hdd,msec_w_hdd,msec_r_ssd,msec_w_ssd,msec_bw_hdd=0;

/* Device I/O operation with Timer */
static int dev_pread(int fd, void* buf,size_t nbytes,off_t offset);
static int dev_pwrite(int fd, void* buf,size_t nbytes,off_t offset);
static int dev_simu_read(void* buf,size_t nbytes,off_t offset);
static int dev_simu_write(void* buf,size_t nbytes,off_t offset);

static char* ssd_buffer;

extern struct RuntimeSTAT* STT;
extern struct InitUsrInfo UsrInfo;


/*
 * init buffer hash table, strategy_control, buffer, work_mem
 */
void
CacheLayer_Init()
{
    int r_initdesp          =   init_SSDDescriptorBuffer();
    int r_initstrategybuf   =   initStrategySSDBuffer();
    int r_initbuftb         =   HashTab_Init();
    int r_initstt           =   init_StatisticObj();

    printf("init_Strategy: %d, init_table: %d, init_desp: %d, inti_Stt: %d\n",
           r_initstrategybuf, r_initbuftb, r_initdesp, r_initstt);

    if(r_initdesp==-1 || r_initstrategybuf==-1 || r_initbuftb==-1 || r_initstt==-1)
        exit(EXIT_FAILURE);

    int returnCode = posix_memalign(&ssd_buffer, 512, sizeof(char) * BLCKSZ);
    if (returnCode < 0)
    {
        printf("[ERROR] flushSSDBuffer():--------posix memalign\n");
        exit(EXIT_FAILURE);
    }

#ifdef CACHE_PROPORTIOIN_STATIC
    Max_Dirty_Cache = (blkcnt_t)((double)NBLOCK_SSD_CACHE * Proportion_Dirty);
    Max_Clean_Cache = NBLOCK_SSD_CACHE - Max_Dirty_Cache;
#endif // CACHE_PROPORTIOIN_STATIC
}

static int
init_SSDDescriptorBuffer()
{
    int stat = 0;
    if(stat == 0)
    {
        DespCtrl_Clean = (SSDBufDespCtrl*)multi_SHM_alloc(SHM_SSDBUF_DESP_CTRL,sizeof(SSDBufDespCtrl));
        DespCtrl_Dirty = (SSDBufDespCtrl*)multi_SHM_alloc(SHM_SSDBUF_DESP_CTRL,sizeof(SSDBufDespCtrl));

        Desps_Clean = (SSDBufDesp *)multi_SHM_alloc(SHM_SSDBUF_DESPS,sizeof(SSDBufDesp) * NBLOCK_CLEAN_CACHE);
        Desps_Dirty = (SSDBufDesp *)multi_SHM_alloc(SHM_SSDBUF_DESPS,sizeof(SSDBufDesp) * NBLOCK_DIRTY_CACHE);
        DespCtrl_Clean->n_usedssd = DespCtrl_Dirty->n_usedssd = 0;
        DespCtrl_Clean->first_freessd = DespCtrl_Dirty->first_freessd = 0;
        // multi_SHM_mutex_init(&DespCtrl_Clean->lock);
        // multi_SHM_mutex_init(&Desps_Dirty->lock);

        long i;
        SSDBufDesp  *ssd_buf_hdr = Desps_Clean;
        for (i = 0; i < NBLOCK_CLEAN_CACHE; ssd_buf_hdr++, i++)
        {
            ssd_buf_hdr->serial_id = i;
            ssd_buf_hdr->ssd_buf_id = i;
            ssd_buf_hdr->ssd_buf_flag = 0;
            ssd_buf_hdr->next_freessd = i + 1;
            multi_SHM_mutex_init(&ssd_buf_hdr->lock);
        }
        Desps_Clean[NBLOCK_CLEAN_CACHE - 1].next_freessd = -1;

        ssd_buf_hdr = Desps_Dirty;
        for (i = 0; i < NBLOCK_DIRTY_CACHE; ssd_buf_hdr++, i++)
        {
            ssd_buf_hdr->serial_id = i;
            ssd_buf_hdr->ssd_buf_id = i;
            ssd_buf_hdr->ssd_buf_flag = 0;
            ssd_buf_hdr->next_freessd = i + 1;
            multi_SHM_mutex_init(&ssd_buf_hdr->lock);
        }
        Desps_Dirty[NBLOCK_DIRTY_CACHE - 1].next_freessd = -1;
    }

    return stat;
}

static int
init_StatisticObj()
{
    STT->hitnum_s = 0;
    STT->hitnum_r = 0;
    STT->hitnum_w = 0;
    STT->load_ssd_blocks = 0;
    STT->flush_ssd_blocks = 0;
    STT->load_hdd_blocks = 0;
    STT->flush_hdd_blocks = 0;
    STT->flush_clean_blocks = 0;

    STT->time_read_hdd = 0.0;
    STT->time_write_hdd = 0.0;
    STT->time_read_ssd = 0.0;
    STT->time_write_ssd = 0.0;
    STT->hashmiss_sum = 0;
    STT->hashmiss_read = 0;
    STT->hashmiss_write = 0;

    STT->wt_hit_rd = STT->rd_hit_wt = 0;
    STT->incache_n_clean = STT->incache_n_dirty = 0;
#ifdef NO_READ_CACHE
    STT->incache_n_clean = -1;
#endif
    return 0;
}

static void
flushSSDBuffer(SSDBufDesp * ssd_buf_hdr)
{
    if (IsClean(ssd_buf_hdr->ssd_buf_flag))
    {
        STT->flush_clean_blocks++;
        return;
    }

    dev_pread(ssd_dirty_fd, ssd_buffer, SSD_BUFFER_SIZE, ssd_buf_hdr->ssd_buf_id * SSD_BUFFER_SIZE);
    STT->load_ssd_blocks++;

    dev_pwrite(ram_fd, ssd_buffer, SSD_BUFFER_SIZE, 0);
    STT->flush_hdd_blocks++;
}

//int ResizeCacheUsage()
//{
//    blksize_t needEvictCnt = STT->cacheUsage - STT->cacheLimit;
//    if(needEvictCnt <= 0)
//        return 0;
//
//    while(needEvictCnt-- > 0)
//    {
//        long unloadId = Strategy_Desp_LogOut();
//        SSDBufDesp* ssd_buf_hdr = &ssd_buf_desps[unloadId];
//
//        // TODO Flush
//        _LOCK(&ssd_buf_hdr->lock);
//        flushSSDBuffer(ssd_buf_hdr);
//
//        ssd_buf_hdr->ssd_buf_flag &= ~(SSD_BUF_VALID | SSD_BUF_DIRTY);
//        ssd_buf_hdr->ssd_buf_tag.offset = -1;
//        _UNLOCK(&ssd_buf_hdr->lock);
//
//        _LOCK(&ssd_buf_desp_ctrl->lock);
//        ssd_buf_hdr->next_freessd = ssd_buf_desp_ctrl->first_freessd;
//        ssd_buf_desp_ctrl->first_freessd = ssd_buf_hdr->serial_id;
//        _UNLOCK(&ssd_buf_desp_ctrl->lock);
//    }
//    return 0;
//}

static void flagOp(SSDBufDesp * ssd_buf_hdr, int opType)
{
    ssd_buf_hdr->ssd_buf_flag |= SSD_BUF_VALID;
    if(opType)
    {
        // write operation
        ssd_buf_hdr->ssd_buf_flag |= SSD_BUF_DIRTY;
    }

}

/* Lookup if already cached. */
static int check_hit(SSDBufTag ssd_buf_tag, int cache_type, long * despId)
{
    long id = HashTB_Lookup(ssd_buf_tag, cache_type);
    if(id >= 0)
    {
        *(despId) = id;
        STT->hitnum_s++;
        return 1;
    }

    *(despId) = -1;
    return 0;
}
static SSDBufDesp *
allocSSDBuf(int cache_type, SSDBufTag ssd_buf_tag)
{
    /* Cache MISS */
    SSDBufDesp * ssd_buf_hdr = pop_freebuf(cache_type);

    if (ssd_buf_hdr == NULL)
    {
        /** When there is NO free SSD space for cache **/
        // TODO Choose a buffer by strategy/

        static int max_n_batch = 8 * 1024;
        long buf_despid_array[max_n_batch];
        int n_evict;
        enum_t_vict suggest_type = (cache_type == 0) ? ENUM_B_Clean : ENUM_B_Dirty;
        n_evict = Unload_Buf_LRU_rw(buf_despid_array, max_n_batch,suggest_type,64);

        SSDBufDesp * ssd_buf_desps = (cache_type == 0) ? Desps_Clean : Desps_Dirty;
        int k = 0;
        while(k < n_evict)
        {
            long out_despId = buf_despid_array[k];
            out_despId = map_strategy_to_cache(cache_type, out_despId);
            ssd_buf_hdr = &ssd_buf_desps[out_despId];

            freeSSDBuf(cache_type, ssd_buf_hdr);
            k++;
        }
//        printf("%d.\n",k);
        STT->cacheUsage -= k;
        if(cache_type == 0)
            STT->incache_n_clean -= k;
        else
            STT->incache_n_dirty -=k;

        ssd_buf_hdr = pop_freebuf(cache_type);
    }

    flagOp(ssd_buf_hdr,cache_type);
    CopySSDBufTag(ssd_buf_hdr->ssd_buf_tag,ssd_buf_tag);

    HashTab_Insert(ssd_buf_tag, cache_type, ssd_buf_hdr->serial_id);

    long strategy_id = map_cache_to_strategy(cache_type, ssd_buf_hdr->serial_id);
    insertBuffer_LRU_rw(strategy_id, ssd_buf_hdr->ssd_buf_flag);
    IsDirty(ssd_buf_hdr->ssd_buf_flag) ? STT->incache_n_dirty ++ : STT->incache_n_clean ++ ;

    return ssd_buf_hdr;
}

void
freeSSDBuf(int cache_type, SSDBufDesp* ssd_buf_hdr)
{
    HashTab_Delete(ssd_buf_hdr->ssd_buf_tag, cache_type);
    // TODO Flush
    flushSSDBuffer(ssd_buf_hdr);
    (cache_type == 0) ? STT->incache_n_clean -- : STT->incache_n_dirty -- ;
    ssd_buf_hdr->ssd_buf_flag &= ~(SSD_BUF_VALID | SSD_BUF_DIRTY);
    // Push back to free list
    push_freebuf(ssd_buf_hdr,cache_type);
}

static int
initStrategySSDBuffer()
{
    switch(EvictStrategy)
    {
    case LRU_rw:
        return initSSDBufferFor_LRU_rw();
//        case Most:              return initSSDBufferForMost();
    }
    return -1;
}

static long
Strategy_Desp_LogOut(unsigned flag)
{
    STT->cacheUsage--;
    return -1;
}

static int
Strategy_Desp_HitIn(SSDBufDesp* desp)
{
    switch(EvictStrategy)
    {

    case LRU_rw:
        return hitInBuffer_LRU_rw(desp->serial_id, desp->ssd_buf_flag);
    }
    return -1;
}

static int
Strategy_Desp_LogIn(SSDBufDesp* desp)
{
    STT->cacheUsage++;
    switch(EvictStrategy)
    {
//        case LRU_global:        return insertLRUBuffer(serial_id);
    case LRU_rw:
        return insertBuffer_LRU_rw(desp->serial_id, desp->ssd_buf_flag);
    }
    return -1;
}
/*
 * read--return the buf_id of buffer according to buf_tag
 */

void
read_block(off_t offset, char *ssd_buffer)
{
    static SSDBufTag ssd_buf_tag;
    static SSDBufDesp* ssd_buf_hdr;
    ssd_buf_tag.offset = offset;
    long despId;

    if(check_hit(ssd_buf_tag, 0, &despId))
    {
        /* Cache Hitin */
        ssd_buf_hdr = Desps_Clean + despId;

        STT->hitnum_r++;
        STT->load_ssd_blocks++;

        long strategy_id = map_cache_to_strategy(0, despId);
        hitInBuffer_LRU_rw(strategy_id, ssd_buf_hdr->ssd_buf_flag);

        dev_pread(ssd_clean_fd, ssd_buffer, SSD_BUFFER_SIZE, ssd_buf_hdr->ssd_buf_id * SSD_BUFFER_SIZE);
        return;
    }


    if (check_hit(ssd_buf_tag, 1, &despId))
    {
        /* read hitin write */
        STT->rd_hit_wt ++;

        ssd_buf_hdr = Desps_Dirty + despId;
        dev_pread(ssd_dirty_fd, ssd_buffer, SSD_BUFFER_SIZE, ssd_buf_hdr->ssd_buf_id * SSD_BUFFER_SIZE);

        long strategy_id = map_cache_to_strategy(1, despId);
        Del_Buf_LRU_rw(strategy_id, 1);
        freeSSDBuf(1, ssd_buf_hdr);
    }
    else
    {   /* Read MISS */
        dev_pread(ram_fd, ssd_buffer, SSD_BUFFER_SIZE, 0);
        STT->load_hdd_blocks++;
    }

    ssd_buf_hdr = allocSSDBuf(0, ssd_buf_tag);

    dev_pwrite(ssd_clean_fd, ssd_buffer, SSD_BUFFER_SIZE, ssd_buf_hdr->ssd_buf_id * SSD_BUFFER_SIZE);
    STT->flush_ssd_blocks++;
    return ;
}

/*
 * write--return the buf_id of buffer according to buf_tag
 */
void
write_block(off_t offset, char *ssd_buffer)
{
    static SSDBufDesp* ssd_buf_hdr;
    static SSDBufTag ssd_buf_tag;
    ssd_buf_tag.offset = offset;
    long despId;

    if(check_hit(ssd_buf_tag, 1, &despId))
    {
        /* Cache Hitin */
        STT->hitnum_w ++;

        ssd_buf_hdr = Desps_Dirty + despId;
        dev_pwrite(ssd_dirty_fd, ssd_buffer, SSD_BUFFER_SIZE, ssd_buf_hdr->ssd_buf_id * SSD_BUFFER_SIZE);

        long strategy_id = map_cache_to_strategy(1, despId);
        hitInBuffer_LRU_rw(strategy_id, ssd_buf_hdr->ssd_buf_flag);

        return ;
    }

    /* MISS */
    if (check_hit(ssd_buf_tag, 0, &despId))
    {
        /* write hitin read */
        STT->wt_hit_rd ++;

        ssd_buf_hdr = Desps_Clean + despId;

        long strategy_id = map_cache_to_strategy(0, despId);
        Del_Buf_LRU_rw(strategy_id, 0);
        freeSSDBuf(0, ssd_buf_hdr);
    }

    ssd_buf_hdr = allocSSDBuf(1, ssd_buf_tag);

    dev_pwrite(ssd_dirty_fd, ssd_buffer, SSD_BUFFER_SIZE, ssd_buf_hdr->ssd_buf_id * SSD_BUFFER_SIZE);
    return ;
}

/******************
**** Utilities*****
*******************/

static int dev_pread(int fd, void* buf,size_t nbytes,off_t offset)
{
    if(I_AM_HRC_PROC)
        return nbytes;
#ifdef NO_REAL_DISK_IO
    return nbytes;
#else
    int r;
    _TimerLap(&tv_start);
    r = pread(fd,buf,nbytes,offset);
    _TimerLap(&tv_stop);
    if (r < 0)
    {
        printf("[ERROR] read():-------read from device: fd=%d, errorcode=%d, offset=%lu\n", fd, r, offset);
        exit(-1);
    }
    return r;
#endif
}

static int dev_pwrite(int fd, void* buf,size_t nbytes,off_t offset)
{
    if(I_AM_HRC_PROC)
        return nbytes;
#ifdef NO_REAL_DISK_IO
    return nbytes;
#else
    int w;
    _TimerLap(&tv_start);
    w = pwrite(fd,buf,nbytes,offset);
    _TimerLap(&tv_stop);
    if (w < 0)
    {
        printf("[ERROR] read():-------write to device: fd=%d, errorcode=%d, offset=%lu\n", fd, w, offset);
        exit(-1);
    }
    return w;
#endif
}

static int dev_simu_write(void* buf,size_t nbytes,off_t offset)
{
    if(I_AM_HRC_PROC)
        return nbytes;

    int w;
    _TimerLap(&tv_start);
    w = simu_smr_write(buf,nbytes,offset);
    _TimerLap(&tv_stop);
    return w;
}

static int dev_simu_read(void* buf,size_t nbytes,off_t offset)
{
    if(I_AM_HRC_PROC)
        return nbytes;

    int r;
    _TimerLap(&tv_start);
    r = simu_smr_read(buf,nbytes,offset);
    _TimerLap(&tv_stop);
    return r;
}

static SSDBufDesp*
pop_freebuf(int cache_type)
{
    SSDBufDespCtrl * ssd_buf_desp_ctrl = (cache_type == 0) ? DespCtrl_Clean : DespCtrl_Dirty;
    SSDBufDesp  * ssd_buf_desps = (cache_type == 0) ? Desps_Clean : Desps_Dirty;
    int usage = (cache_type == 0) ? STT->incache_n_clean : STT->incache_n_dirty;
    int limit = (cache_type == 0) ? STT->cacheLimit_Clean : STT->cacheLimit_Dirty;
    if(ssd_buf_desp_ctrl->first_freessd < 0 || usage >= limit)
    {
//        if(ssd_buf_desp_ctrl->first_freessd < 0)
//            printf("pop func, return NULL, ssd_buf_desp_ctrl->first_freessd < 0.\n");
//        else
//            printf("pop func, return NULL, usage = %d, limit = %d.\n",usage,limit);
        return NULL;
    }
    SSDBufDesp* ssd_buf_hdr = &ssd_buf_desps[ssd_buf_desp_ctrl->first_freessd];
    ssd_buf_desp_ctrl->first_freessd = ssd_buf_hdr->next_freessd;
    ssd_buf_hdr->next_freessd = -1;
    ssd_buf_desp_ctrl->n_usedssd++;
//    printf("pop func, now desp_ctrl->first_freessd = %d.\n",ssd_buf_desp_ctrl->first_freessd);
    return ssd_buf_hdr;
}

static int
push_freebuf(SSDBufDesp* freeDesp, int cache_type)
{
    SSDBufDespCtrl * desp_ctrl = (cache_type == 0) ? DespCtrl_Clean : DespCtrl_Dirty;
    freeDesp->next_freessd = desp_ctrl->first_freessd;
    desp_ctrl->first_freessd = freeDesp->serial_id;
//    printf("push func, now desp_ctrl->first_freessd = %d.\n",desp_ctrl->first_freessd);
    return desp_ctrl->first_freessd;
}

void
_LOCK(pthread_mutex_t* lock)
{
#ifdef MULTIUSER
    SHM_mutex_lock(lock);
#endif // MULTIUSER
}

void
_UNLOCK(pthread_mutex_t* lock)
{
#ifdef MULTIUSER
    SHM_mutex_unlock(lock);
#endif // MULTIUSER
}


/* cache_type: 0 for clean, 1 for dirty */
static long map_cache_to_strategy(int cache_type, long desp_id)
{
    return cache_type * NBLOCK_CLEAN_CACHE + desp_id;
}

long map_strategy_to_cache(int cache_type, long desp_id)
{
    return desp_id - (cache_type * NBLOCK_CLEAN_CACHE) ;
}
