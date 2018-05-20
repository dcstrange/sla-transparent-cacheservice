/*
 * main.c
 */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>

#include "report.h"
#include "shmlib.h"
#include "global.h"
#include "cache.h"
#include "smr-simulator/smr-simulator.h"
#include "smr-simulator/simulator_logfifo.h"
#include "smr-simulator/simulator_v2.h"
#include "trace2call.h"
#include "daemon.h"
#include "timerUtils.h"
#include "/home/fei/git/Func-Utils/pipelib.h"

//static char str_program_config[];

unsigned int INIT_PROCESS = 0;
void ramdisk_iotest()
{
    int fdram = open("/mnt/ramdisk/ramdisk",O_RDWR | O_DIRECT);
    printf("fdram=%d\n",fdram);

    char* buf;
    posix_memalign(&buf, 512, 4096);
    size_t count = 512;
    off_t offset = 0;

    int r;
    while(1)
    {
        r = pwrite(fdram,buf,count,offset);
        if(r<=0)
        {
            printf("write ramdisk error:%d\n",r);
            exit(1);
        }
    }
}

char* tracefile[] = {"/home/trace/src1_2.csv.req",
                     "/home/trace/wdev_0.csv.req",
                     "/home/trace/hm_0.csv.req",
                     "/home/trace/mds_0.csv.req",
                     "/home/trace/prn_0.csv.req",       //1 1 4 0 0 106230 5242880 0
                     "/home/trace/rsrch_0.csv.req",
                     "/home/trace/stg_0.csv.req",
                     "/home/trace/ts_0.csv.req",
                     "/home/trace/usr_0.csv.req",
                     "/home/trace/web_0.csv.req",
                     "/home/trace/production-LiveMap-Backend-4K.req", // --> not in used.
                     "/home/trace/merged_traceX18.req"  // default set: cache size = 8M*blksize; persistent buffer size = 1.6M*blksize.
                     //"/home/trace/merged_trace_x1.req.csv"
                    };

blksize_t trace_req_total[] = {14024860,2654824,8985487,2916662,17635766,3254278,6098667,4216457,12873274,9642398,1,1481448114};

int
main(int argc, char** argv)
{

    //FunctionalTest();
    //ramdisk_iotest();

// 1 1 1 0 0 100000 100000
// 1 1 0 0 0 100000 100000

// 1 4 0 0 500000 106230 5242880 LRU 0

// trace11: 0 0 11 1 0 8000000 30 PV3
    if (argc == 4)
    {
        NBLOCK_MAX_CACHE_SIZE = atol(argv[1]);
        NBLOCK_CLEAN_CACHE = NBLOCK_DIRTY_CACHE = NTABLE_CLEAN_CACHE = NTABLE_DIRTY_CACHE = atol(argv[2]);

        TraceId = atoi(argv[3]);
        EvictStrategy = LRU_rw;
    }
    else
    {
        printf("parameters are wrong %d\n", argc);
        exit(EXIT_FAILURE);
    }

#ifdef HRC_PROCS_N
    int forkcnt = 0;
    while(forkcnt < HRC_PROCS_N)
    {
        int pipefd[2];
        int fpid = fork_pipe_create(pipefd);
        if(fpid > 0)
        {   /* MAIN Process*/
            printf("pipefd = %d,%d\n",pipefd[0],pipefd[1]);
            Fork_Pid = 0;
            close(pipefd[0]);
            PipeEnds_of_MAIN[forkcnt] = pipefd[1];
            forkcnt ++ ;
            continue;
        }
        else if(fpid == 0)
        {   /* Child HRC Process */
            Fork_Pid = forkcnt + 1;
            close(pipefd[1]);
            PipeEnd_of_HRC = pipefd[0];
            break;
        }
        else
        {
            perror("fork_pipe\n");
            exit(EXIT_FAILURE);
        }
    }
#endif // HRC_PROCS_N

    if(!I_AM_HRC_PROC)
    {   /* MAIN process to do*/
        initLog();

        /* Cache Layer Device */
        ssd_clean_fd = open(ssd_clean_dev, O_RDWR | O_DIRECT);
        ssd_dirty_fd = open(ssd_dirty_dev, O_RDWR | O_DIRECT);
        /* High Speed Disttibuted Storage Device */
        ram_fd = open(ram_device, O_RDWR);
        if(ram_fd < 0){
		perror("opern ramdisk error.");
		exit(-1);
	}
        printf("Device ID: ram=%d, ssd_clean=%d, ssd_dirty=%d\n",ram_fd,ssd_clean_fd,ssd_dirty_fd);
    }
    else
    {   /* HRC processes to do*/
        #ifdef HRC_PROCS_N
        NBLOCK_CLEAN_CACHE
        = NTABLE_CLEAN_CACHE
        = NBLOCK_CLEAN_CACHE
        = NTABLE_CLEAN_CACHE
        = NBLOCK_MAX_CACHE_SIZE / HRC_PROCS_N * Fork_Pid;
        #endif // HRC_PROCS_N
    }


    initRuntimeInfo();
    //STT->trace_req_amount = trace_req_total[TraceId];
    CacheLayer_Init();



//#ifdef DAEMON_PROC
//    pthread_t tid;
//    int err = pthread_create(&tid, NULL, daemon_proc, NULL);
//    if (err != 0)
//    {
//        printf("[ERROR] initSSD: fail to create thread: %s\n", strerror(err));
//        exit(-1);
//    }
//#endif // DAEMON

    WriteOnly = 0;
    StartLBA = 0;
    IO_Listening(tracefile[TraceId], WriteOnly, StartLBA);

    /* Only MAIN to do */
    close(ram_fd);
    close(ssd_clean_fd);
    close(ssd_dirty_fd);
    CloseLogFile();
    wait(NULL);
    exit(EXIT_SUCCESS);
}

int initRuntimeInfo()
{
    char str_STT[50];
    sprintf(str_STT,"STAT_b%d_u%d_t%d",BatchId,UserId,TraceId);
    STT = (struct RuntimeSTAT*)multi_SHM_alloc(str_STT,sizeof(struct RuntimeSTAT));
    if(STT == NULL)
        return errno;

    STT->batchId = BatchId;
    STT->userId = UserId;
    STT->traceId = TraceId;
    STT->startLBA = StartLBA;
    STT->isWriteOnly = WriteOnly;
    STT->cacheUsage = 0;
    STT->cacheLimit = 0x7fffffffffffffff;

    STT->wtrAmp_cur = 0;
    STT->WA_sum = 0;
    STT->n_RMW = 0;
    return 0;
}

int initLog()
{
    char logpath[50];
    sprintf(logpath,"%s/bw-limited.log",PATH_LOG,TraceId);
    int rt = 0;
    if((rt = OpenLogFile(logpath)) < 0)
    {
        error("open log file failure.\n");
        exit(1);
    }
    return rt;
}
