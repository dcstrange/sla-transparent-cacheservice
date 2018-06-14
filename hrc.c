#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "hrc.h"
extern int Fork_Pid;
char hrc_report_dir[] = "/tmp";

void hrc_error(const char* str)
{
    if(!I_AM_HRC_PROC){
        perror("hrc_report\n");
        exit(EXIT_FAILURE);
    }
    printf("HRC proc [%d] error: %s\n", Fork_Pid, str);
}


void hrc_report()
{
    if(!I_AM_HRC_PROC){
        perror("hrc_report\n");
        exit(EXIT_FAILURE);
    }
    void* str;
    posix_memalign(&str,512,512);

    char path_r[64],path_w[64];
    sprintf(path_r,"%s/hrc_user%d_%d_%c",hrc_report_dir,UserId,Fork_Pid,'r');
    sprintf(path_w,"%s/hrc_user%d_%d_%c",hrc_report_dir,UserId,Fork_Pid,'w');
    int file_r = open(path_r, O_CREAT | O_RDWR | O_DIRECT, S_IRWXU | S_IRWXG | S_IRWXO);
    int file_w = open(path_w, O_CREAT | O_RDWR | O_DIRECT, S_IRWXU | S_IRWXG | S_IRWXO);
    int file_r_record = open(strcat(path_r,"_record"), O_CREAT | O_RDWR | O_DIRECT, S_IRWXU | S_IRWXG | S_IRWXO);
//    if(file_r_record < 0)
//    {
//        printf("open error %s\n",strcat(path_r,"_record"));
//        exit(-1);
//    }
//    lseek(file_r_record,0,SEEK_END);
    int file_w_record = open(strcat(path_w,"_record"), O_CREAT | O_RDWR | O_DIRECT, S_IRWXU | S_IRWXG | S_IRWXO);
    lseek(file_w_record,0,SEEK_END);
    int hitratio_r = (STT->reqcnt_r == 0) ? 0:(int)((float)STT->hitnum_r / STT->reqcnt_r * 100);
    int hitratio_w = (STT->reqcnt_w == 0) ? 0:(int)((float)STT->hitnum_w / STT->reqcnt_w * 100);

    sprintf(str, "%d,%ld,%ld,%d\n", Fork_Pid, STT->reqcnt_r, STT->hitnum_r, hitratio_r);
//    printf("%s", (char*)str);
    ssize_t ret = pwrite(file_r,str,512,0);
    if(ret < 512)
        hrc_error("report error");
//    sprintf(str, "%d,%ld,%ld,%d\n", Fork_Pid, STT->reqcnt_r, STT->hitnum_r, hitratio_r);
//    printf("%s %d\n",str,strlen(str));
//    ret = pwrite(file_r_record,str,strlen(str),0);
//    printf("%d write ret\n",ret);

    sprintf(str, "%d,%d,%ld,%d\n", Fork_Pid, STT->reqcnt_w, STT->hitnum_w, hitratio_w);
    ret = pwrite(file_w,str,512,0);
    if(ret < 512)
        hrc_error("report error");
//    sprintf(str, "%d,%d,%ld,%d\n", Fork_Pid, STT->reqcnt_w, STT->hitnum_w, hitratio_w);
//    pwrite(file_w_record,str,strlen(str),0);

//    STT->hitnum_s = STT->reqcnt_s = 0;
//    STT->hitnum_r = STT->reqcnt_r = 0;
//    STT->hitnum_w = STT->reqcnt_w = 0;

    close(file_r);
    close(file_w);
    close(file_r_record);
    close(file_w_record);
}
