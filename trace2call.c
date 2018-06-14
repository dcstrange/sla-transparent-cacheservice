#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <math.h>

#include "global.h"
#include "statusDef.h"

#include "timerUtils.h"
#include "cache.h"
#include "strategy/lru.h"
#include "trace2call.h"
#include "report.h"
#include "sla_transparent.h"
#include "strategy/strategies.h"

#include "/home/fei/git/Func-Utils/pipelib.h"
#include "hrc.h"
extern struct RuntimeSTAT* STT;
#define REPORT_INTERVAL 50000
#define KTOG 1048576

static void do_HRC();
static void reportCurInfo();
static void report_ontime();
static void resetStatics();

static timeval  tv_trace_start, tv_trace_end, tv_period_start, tv_period_end;
static double time_period;
static double time_trace;

/** for bandwitdth statistics **/
static timeval  tv_window_start;
blkcnt_t req_r_window,req_w_window;
/** single request statistic information **/
static timeval          tv_req_start, tv_req_stop;
static microsecond_t    msec_req;
extern microsecond_t    msec_r_hdd,msec_w_hdd,msec_r_ssd,msec_w_ssd;
extern int IsHit;
char logbuf[512];

void
IO_Listening(char *trace_file_path, int isWriteOnly,off_t startLBA)
{
    if(I_AM_HRC_PROC)
    {
        do_HRC(startLBA);
        exit(EXIT_SUCCESS);
    }

    char		action;
    off_t		offset;
    char       *ssd_buffer;
    int	        returnCode;
    int         isFullSSDcache = 0;
    char        pipebuf[128];
#ifdef CG_THROTTLE
    static char* cgbuf;
    int returncode = posix_memalign(&cgbuf, 512, 4096);
#endif // CG_THROTTLE

    returnCode = posix_memalign(&ssd_buffer, 1024, 16*sizeof(char) * BLCKSZ);
    if (returnCode < 0)
    {
        error("posix memalign error\n");
        //free(ssd_buffer);
        exit(-1);
    }
    int i;
    for (i = 0; i < 16 * BLCKSZ; i++)
    {
        ssd_buffer[i] = '1';
    }

    _TimerLap(&tv_trace_start);
    _TimerLap(&tv_window_start);
    static int req_cnt = 0;
    req_r_window = req_w_window = 0;

    blkcnt_t total_n_req = isWriteOnly ? 100000000 : 200000000;
    blkcnt_t skiprows = 0; //isWriteOnly ? 50000000 : 100000000;

    FILE *trace;
    if ((trace = fopen(trace_file_path, "rt")) == NULL)
    {
        error("Failed to open the trace file!\n");
        exit(EXIT_FAILURE);
    }

    #ifdef HRC_PROCS_N
    static int max_n_batch = 8 * 1024;
    long *buf_despid_array;
    int n_evict;
    #endif // HRC_PROCS_N

    double read_cost_ssd = 0.0,read_cost_storage = 0.0;
    double write_cost_ssd = 0.0,write_cost_storage = 0.0;
    blkcnt_t hit_r_record = 0,hit_w_record = 0;
    blkcnt_t req_r_record = 0,req_w_record = 0;

    _TimerLap(&tv_period_start);

    while (!feof(trace))//  && STT->reqcnt_s < total_n_req) // 84340000
    {
	#ifdef HRC_PROCS_N
        char new_cachesize[128];
        int ret=read(read_fifo,new_cachesize,128);
        if(ret==128)
        {
            _TimerLap(&tv_period_end);
            time_period = Mirco2Sec(TimerInterval_MICRO(&tv_period_start,&tv_period_end));
            tv_period_start = tv_period_end;
            blkcnt_t hitmiss_period = STT->reqcnt_r - req_r_record - (STT->hitnum_r - hit_r_record);
            double miss_r_ratio = (STT->reqcnt_r - req_r_record == 0) ? 0: 1 - (STT->hitnum_r - hit_r_record)/(double)(STT->reqcnt_r - req_r_record);

            read_cost_ssd += (P0 * BG0 * STT->cacheLimit_Clean * 4/KTOG + P(read_bw[UserId]*miss_r_ratio,STT->cacheLimit_Clean * 4.0/KTOG))*time_period;
            if(isnan(read_cost_ssd))
            {
                printf("bw converted to P is %lf,miss_r_ratio = %lf,bw*miss = %lf\n",read_bw[UserId],miss_r_ratio,read_bw[UserId]*miss_r_ratio);
                printf("%lf,%lf,%lf\n",P0 * BG0 * STT->cacheLimit_Clean * 4/KTOG,P(read_bw[UserId]*miss_r_ratio,STT->cacheLimit_Clean * 4.0/KTOG),(P0 * BG0 * STT->cacheLimit_Clean * 4/KTOG + P(read_bw[UserId]*miss_r_ratio,STT->cacheLimit_Clean * 4.0/KTOG))*time_period);
                exit(-1);
            }
            printf("read_cost_ssd is %lf\n",read_cost_ssd);
            req_r_record = STT->reqcnt_r;
            hit_r_record = STT->hitnum_r;


            read_cost_storage += hitmiss_period * 4 * BW_R/KTOG;
            STT->cacheLimit_Clean = atoi(new_cachesize);
            if(STT->cacheLimit_Clean < STT->incache_n_clean)
            {
                buf_despid_array = (long *)malloc(sizeof(long)*(STT->incache_n_clean - STT->cacheLimit_Clean + 10));
                int n_evict = Unload_Buf_LRU_rw(buf_despid_array, max_n_batch,ENUM_B_Clean,STT->incache_n_clean - STT->cacheLimit_Clean);
                SSDBufDesp * ssd_buf_desps = Desps_Clean;
                int k = 0;
                while(k < n_evict)
                {
                    long out_despId = buf_despid_array[k];
                    out_despId = map_strategy_to_cache(ENUM_B_Clean, out_despId);
                    SSDBufDesp *ssd_buf_hdr = &ssd_buf_desps[out_despId];

                    freeSSDBuf(ENUM_B_Clean, ssd_buf_hdr);
                    k++;
                }
                STT->cacheUsage -= k;
                STT->incache_n_clean -= k;
                free(buf_despid_array);
            }
            printf("STT->cacheLimit_Clean was resized to %d.\n",STT->cacheLimit_Clean);
        }

        ret=read(write_fifo,new_cachesize,128);
        if(ret==128)
        {
            blkcnt_t hitmiss_period = STT->reqcnt_w - req_w_record - (STT->hitnum_w - hit_w_record);
            req_w_record = STT->reqcnt_w;
            hit_w_record = STT->hitnum_w;

            write_cost_ssd += (P0 * BG0 * STT->cacheLimit_Dirty * 4/KTOG + P(write_bw[UserId],STT->cacheLimit_Dirty * 4.0/KTOG))*time_period;
            write_cost_storage += hitmiss_period * 4 * BW_W/KTOG;

            STT->cacheLimit_Dirty = atoi(new_cachesize);
            if(STT->cacheLimit_Dirty < STT->incache_n_dirty)
            {
                buf_despid_array = (long *)malloc(sizeof(long)*(STT->incache_n_dirty - STT->cacheLimit_Dirty + 10));
                n_evict = Unload_Buf_LRU_rw(buf_despid_array, max_n_batch,ENUM_B_Dirty,STT->incache_n_dirty - STT->cacheLimit_Dirty);
                SSDBufDesp * ssd_buf_desps = Desps_Dirty;
                int k = 0;
                while(k < n_evict)
                {
                    long out_despId = buf_despid_array[k];
                    out_despId = map_strategy_to_cache(ENUM_B_Dirty, out_despId);
                    SSDBufDesp *ssd_buf_hdr = &ssd_buf_desps[out_despId];

                    freeSSDBuf(ENUM_B_Dirty, ssd_buf_hdr);
                    k++;
                }
                STT->cacheUsage -= k;
                STT->incache_n_dirty -= k;
                free(buf_despid_array);
            }
            printf("STT->cacheLimit_Dirty was resized to %d.\n",STT->cacheLimit_Dirty);
        }


	#endif //HRC_PROCS_N
        returnCode = fscanf(trace, "%c %d %lu\n", &action, &i, &offset);
        if (returnCode < 0)
        {
            error("error while reading trace file.");
            break;
        }
        if(skiprows > 0)
        {
            skiprows -- ;
            continue;
        }
#ifdef CG_THROTTLE
        if(pwrite(ram_fd,cgbuf,1024,0) <= 0)
        {
            printf("write ramdisk error:%d\n",errno);
            exit(1);
        }
#endif // CG_THROTTLE


        offset = (offset + startLBA) * BLCKSZ;

//        if(!isFullSSDcache && (STT->flush_clean_blocks + STT->flush_hdd_blocks) > 0)
//        {
//            reportCurInfo();
//            resetStatics();        // Because we do not care about the statistic while the process of filling SSD cache.
//            isFullSSDcache = 1;
//        }
#ifdef T_SWITCHER_ON
        static int tk = 1;
        if(tk && (STT->flush_clean_blocks + STT->flush_hdd_blocks) >= TS_StartSize)
        {   /* When T-Switcher start working */
            info("T-Switcher start working...");
            reportCurInfo();
            tk = 0;
        }
#endif // T_SWITCHER_ON

#ifdef LOG_SINGLE_REQ
        _TimerLap(&tv_req_start);
#endif // TIMER_SINGLE_REQ
        sprintf(pipebuf,"%c,%lu\n",action,offset);
        if (action == ACT_WRITE) // Write = 1
        {
            STT->reqcnt_w ++;
            STT->reqcnt_s ++;
            write_block(offset, ssd_buffer);
            #ifdef HRC_PROCS_N
            int i;
            for(i = 0; i < HRC_PROCS_N; i++)
            {
                pipe_write(PipeEnds_of_MAIN[i],pipebuf,64);
            }
            #endif // HRC_PROCS_N
        }
        else if (!isWriteOnly && action == ACT_READ)    // read = 9
        {
            STT->reqcnt_r ++;
            STT->reqcnt_s ++;
            read_block(offset,ssd_buffer);
            #ifdef HRC_PROCS_N
            int i;
            for(i = 0; i < HRC_PROCS_N; i++)
            {
                pipe_write(PipeEnds_of_MAIN[i],pipebuf,64);
            }
            #endif // HRC_PROCS_N
        }
//        else if (action != ACT_READ)
//        {
//            printf("Trace file gets a wrong result: action = %c.\n",action);
//            exit(-1);
//        }
#ifdef LOG_SINGLE_REQ
        _TimerLap(&tv_req_stop);
        msec_req = TimerInterval_MICRO(&tv_req_start,&tv_req_stop);
        /*
            print log
            format:
            <req_id, r/w, ishit, time cost for: one request, read_ssd, write_ssd, read_smr, write_smr>
        */
        //sprintf(logbuf,"%lu,%c,%d,%ld,%ld,%ld,%ld,%ld\n",STT->reqcnt_s,action,IsHit,msec_req,msec_r_ssd,msec_w_ssd,msec_r_hdd,msec_w_hdd);
       // WriteLog(logbuf);
        msec_r_ssd = msec_w_ssd = msec_r_hdd = msec_w_hdd = 0;
#endif // TIMER_SINGLE_REQ

        if (STT->reqcnt_s % REPORT_INTERVAL == 0)
        {
            report_ontime();
        }


        //ResizeCacheUsage();
    }

    _TimerLap(&tv_period_end);
    time_period = Mirco2Sec(TimerInterval_MICRO(&tv_period_start,&tv_period_end));
    tv_period_start = tv_period_end;

    report_ontime();
    _TimerLap(&tv_trace_end);
    time_trace = Mirco2Sec(TimerInterval_MICRO(&tv_trace_start,&tv_trace_end));
    reportCurInfo();

    blkcnt_t hitmiss_period = STT->reqcnt_r - req_r_record - (STT->hitnum_r - hit_r_record);
    double miss_r_ratio = (STT->reqcnt_r - req_r_record == 0) ? 0: 1 - (STT->hitnum_r - hit_r_record)/(double)(STT->reqcnt_r - req_r_record);

    read_cost_ssd += (P0 * BG0 * STT->cacheLimit_Clean * 4/KTOG + P(read_bw[UserId]*miss_r_ratio,STT->cacheLimit_Clean * 4.0/KTOG))*time_period;
    req_r_record = STT->reqcnt_r;
    hit_r_record = STT->hitnum_r;

    read_cost_storage += hitmiss_period * 4 * BW_R/KTOG;

    hitmiss_period = STT->reqcnt_w - req_w_record - (STT->hitnum_w - hit_w_record);
    req_w_record = STT->reqcnt_w;
    hit_w_record = STT->hitnum_w;
    write_cost_ssd += (P0 * BG0 * STT->cacheLimit_Dirty * 4/KTOG + P(write_bw[UserId],STT->cacheLimit_Dirty * 4.0/KTOG))*time_period;
    write_cost_storage += hitmiss_period * 4 * BW_W/KTOG;

    printf("The read cost on ssd is %lf, the read cost on storage is %lf, total cost is %lf\n",read_cost_ssd,read_cost_storage,read_cost_ssd + read_cost_storage);
    printf("The write cost on ssd is %lf, the write cost on storage is %lf, total cost is %lf\n",write_cost_ssd,write_cost_storage,write_cost_ssd + write_cost_storage);

    #ifdef HRC_PROCS_N
    for(i = 0; i < HRC_PROCS_N; i++)
    {
        sprintf(pipebuf,"EOF\n");
        pipe_write(PipeEnds_of_MAIN[i],pipebuf,strlen(pipebuf));
    }
    #endif // HRC_PROCS_N
    free(ssd_buffer);
    fclose(trace);
}

static void
do_HRC()
{
#ifdef HRC_PROCS_N
    char	action;
    off_t   offset;
    int     i;
    int strlen = 64;
    char buf[128];
    int  ret;
    while((ret = read(PipeEnd_of_HRC, buf, strlen)) == strlen)
    {
        if(sscanf(buf,"%c,%lu\n", &action, &offset) < 0)
        {
            perror("HRC: sscanf string");
            exit(EXIT_FAILURE);
        }

        if (action == ACT_WRITE) // Write = 1
        {
            STT->reqcnt_w ++;
            STT->reqcnt_s ++;
            write_block(offset, NULL);
        }
        else if (action == ACT_READ)    // read = 9
        {
            STT->reqcnt_r ++;
            STT->reqcnt_s ++;
            read_block(offset,NULL);
        }

        if(STT->reqcnt_s % 10000== 0)
        {
            hrc_report();
        }
    }

    exit(EXIT_SUCCESS);
#endif
}

static void reportCurInfo()
{
    printf(" totalreqNum:%lu\n read_req_count: %lu\n write_req_count: %lu\n",
           STT->reqcnt_s,STT->reqcnt_r,STT->reqcnt_w);

    printf(" hit num:%lu\n hitnum_r:%lu\n hitnum_w:%lu\n",
           STT->hitnum_s,STT->hitnum_r,STT->hitnum_w);

    printf(" read_ssd_blocks:%lu\n flush_ssd_blocks:%lu\n read_hdd_blocks:%lu\n flush_hdd_blocks:%lu\n flush_clean_blocks:%lu\n",
           STT->load_ssd_blocks, STT->flush_ssd_blocks, STT->load_hdd_blocks, STT->flush_hdd_blocks, STT->flush_clean_blocks);

//    printf(" hash_miss:%lu\n hashmiss_read:%lu\n hashmiss_write:%lu\n",
//           STT->hashmiss_sum, STT->hashmiss_read, STT->hashmiss_write);

    printf(" total run time (s): %lf\n time_read_ssd : %lf\n time_write_ssd : %lf\n time_read_smr : %lf\n time_write_smr : %lf\n",
           time_trace, STT->time_read_ssd, STT->time_write_ssd, STT->time_read_hdd, STT->time_write_hdd);
    printf(" Batch flush HDD time:%lu\n",msec_bw_hdd);

    printf(" Cache Proportion(R/W): [%ld/%ld]\n", STT->incache_n_clean,STT->incache_n_dirty);
    printf(" wt_hit_rd: %lu\n rd_hit_wt: %lu\n",STT->wt_hit_rd, STT->rd_hit_wt);
}

static void report_ontime()
{
//    _TimerLap(&tv_checkpoint);
//    double timecost = Mirco2Sec(TimerInterval_SECOND(&tv_trace_start,&tv_checkpoint));

//     printf("totalreq:%lu, readreq:%lu, hit:%lu, readhit:%lu, flush_ssd_blk:%lu flush_hdd_blk:%lu, hashmiss:%lu, readhassmiss:%lu writehassmiss:%lu\n",
//           STT->reqcnt_s,STT->reqcnt_r, STT->hitnum_s, STT->hitnum_r, STT->flush_ssd_blocks, STT->flush_hdd_blocks, STT->hashmiss_sum, STT->hashmiss_read, STT->hashmiss_write);
        printf("totalreq:%lu, readreq:%lu, wrtreq:%lu, hit:%lu, readhit:%lu, flush_ssd_blk:%lu flush_hdd_blk:%lu\n",
           STT->reqcnt_s, STT->reqcnt_r, STT->reqcnt_w, STT->hitnum_s, STT->hitnum_r, STT->flush_ssd_blocks, STT->flush_hdd_blocks);
        _TimerLap(&tv_trace_end);
        int timecost = Mirco2Sec(TimerInterval_MICRO(&tv_trace_start,&tv_trace_end));
        printf("current run time: %d\n",timecost);
        double window = Mirco2Sec(TimerInterval_MICRO(&tv_window_start,&tv_trace_end));
        printf("this window, read bandwidth is %lf, write bandwidth is %lf\n",(STT->reqcnt_r-req_r_window)*4/window,(STT->reqcnt_w-req_w_window)*4/window);
        tv_window_start = tv_trace_end;
        req_r_window = STT->reqcnt_r;
        req_w_window = STT->reqcnt_w;
}

static void resetStatics()
{

//    STT->hitnum_s = 0;
//    STT->hitnum_r = 0;
//    STT->hitnum_w = 0;
    STT->load_ssd_blocks = 0;
    STT->flush_ssd_blocks = 0;

    STT->time_read_hdd = 0.0;
    STT->time_write_hdd = 0.0;
    STT->time_read_ssd = 0.0;
    STT->time_write_ssd = 0.0;
    STT->hashmiss_sum = 0;
    STT->hashmiss_read = 0;
    STT->hashmiss_write = 0;
    msec_bw_hdd = 0;
}

