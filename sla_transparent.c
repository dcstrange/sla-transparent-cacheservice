#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>

#define MAX_USER 10
#define KTOG 1024*1024

int total_ssd = 100000;
int step_size = 10000;
int user_num = 4;
double read_bw[] = {10.0/1024,10.0/1024,20.0/1024,20.0/1024};
double write_bw[] = {10.0/1024,10.0/1024,20.0/1024,20.0/1024};
int reqcnt_r[MAX_USER];
int reqcnt_w[MAX_USER];
int hitnum_r[MAX_USER];
int hitnum_w[MAX_USER];
double hitratio_r[MAX_USER];
double hitratio_w[MAX_USER];
double P0 = 122.0/(150*1024);
double BG0 = 150*1024.0/78840000000; // 78840000000 = 5*365*24*3600*500
double BW_W = 0.2043936;
double BW_R = 0.2043936;

char hrc_dir[] = "/tmp";

int cmp(const void *a,const void *b)
{
    return *(int *)a-*(int *)b;//From small to large
}

double hrc(int userid,int cache_size,char rw)
{
    if(cache_size == 0)
        return 0.0;
  //  printf("hrc cache_size is %d\n",cache_size);
    void* str;
    posix_memalign(&str,512,512);

    char path[64];
    sprintf(path,"%s/hrc_user%d_%d_%c",hrc_dir,userid,cache_size/step_size,rw);
    int file = open(path, O_RDONLY);
//    printf("file is %s\n",path);
    if(file<0)
    {
        printf("hrc() open file %s error. errno = %d\n",path,errno);
        printf("error: %s\n", strerror(errno));
        exit(-1);
    }
    int fork_pid,reqcnt,hitnum;
    double hitratio;

    int ret = read(file,str,512);
//    printf("file = %d,ret = %d,errno = %d\n",file,ret,errno);
//    printf("str is %s\n",str);
    sscanf(str,"%d,%d,%d,%d\n", &fork_pid, &reqcnt, &hitnum, &hitratio);
//    printf("%s %d %d\n",path,reqcnt,hitnum);


    close(file);
    if (rw == 'r')
    {
        if(reqcnt == reqcnt_r[userid])
            return hitratio_r[userid];
        hitratio = (hitnum - hitnum_r[userid])/(double)(reqcnt - reqcnt_r[userid]);
        hitnum_r[userid] = hitnum;
        reqcnt_r[userid] = reqcnt;
        hitratio_r[userid] = hitratio;
    }
    else if(rw == 'w')
    {
        if(reqcnt == reqcnt_w[userid])
            return hitratio_w[userid];
        hitratio = (hitnum - hitnum_w[userid])/(double)(reqcnt - reqcnt_w[userid]);
        hitnum_w[userid] = hitnum;
        reqcnt_w[userid] = reqcnt;
        hitratio_w[userid] = hitratio;
    }
    else
    {
        printf("parameter error.\n");
        exit(-1);
    }
    return hitratio;
}

double P(double bw,double cache_size)
{
    if(isnan(bw))
        printf("bw is nan\n");
    if(cache_size == 0)
        return 0.0;
    if(bw <= BG0 * cache_size)
        return 0;
    double cost = (bw - BG0*cache_size)*P0;
    if(isnan(cost))
        printf("bw = %lf,cache_size = %lf,bw - BG0*cache_size = %lf,(bw - BG0*cache_size)*P0 = %lf\n",bw,cache_size,bw - BG0*cache_size,(bw - BG0*cache_size)*P0);
    return cost;
}

double cal_write_cost(int *cache_array)
{
//    int tmp = 0;
//    for(tmp = 0;tmp < user_num;tmp++)
//        printf("%d ",cache_array[tmp]);
//    printf("\n");
    int i = 0;
    double cost = 0.0;
    printf("write cost details for user 0 at %d: ",cache_array[0]*step_size);
    cost += P0 * BG0 * cache_array[0] * step_size * 4.0/KTOG + P(write_bw[0],cache_array[0] * step_size * 4.0/KTOG) + write_bw[0]*(1-hrc(0,cache_array[0] * step_size,'w'))*BW_W;
    printf("%lf %lf %lf %lf\n",P0 * BG0 * cache_array[0] * step_size,P(write_bw[0],cache_array[0] * step_size * 4.0/KTOG),write_bw[0]*(1-hrc(0,cache_array[0] * step_size,'w'))*BW_W,hrc(0,cache_array[0] * step_size,'w'));
//    printf("cost updating : %lf \n",cost);
    for(i = 1;i < user_num;i++)
    {
        int cache = (cache_array[i] - cache_array[i-1]) * step_size;

        cost += P0 * BG0 * cache * 4.0/KTOG + P(write_bw[i],cache * 4.0/KTOG) + write_bw[i]*(1-hrc(i,cache,'w'))*BW_W;
//        printf("now user %d\n",i);
//        printf("%lf \n",cost);
//        printf("write cost details for user %d at %d: ",i,cache);
//        printf("%lf %lf %lf\n",P0 * BG0 * cache * 4.0/KTOG,P(write_bw[i],cache * 4.0/KTOG),write_bw[i]*(1-hrc(i,cache,'w'))*BW_W);
    }
    printf("\n");

    return cost;
}

double cal_read_cost(int *cache_array)
{
    int i = 0;
    double cost = 0.0;
    cost += P0 * BG0 * cache_array[0] * step_size * 4.0/KTOG + P(read_bw[0]*(1-hrc(0,cache_array[0] * step_size,'r')),cache_array[0] * step_size * 4.0/KTOG) + read_bw[0]*(1-hrc(0,cache_array[0] * step_size,'r'))*BW_R;
    for(i = 1;i < user_num;i++)
    {
        int cache = (cache_array[i] - cache_array[i-1]) * step_size;
        cost += P0 * BG0 * cache * 4.0/KTOG + P(read_bw[i]*(1-hrc(i,cache,'r')),cache * 4.0/KTOG) + read_bw[i]*(1-hrc(i,cache,'r'))*BW_R;
    }
    return cost;
}

void adjust(int *solution,int m,char rw)
{
    int i = 0;
    char path[64];
    char output[128];
    for (i = 0; i < m; i++)
    {
        sprintf(path,"/tmp/fifo_user%d_%c",i,rw);
        int write_fd = open(path,O_RDWR);
        if(write_fd<0)
        {
            printf("%s open error\n",path);
            perror("open error");
            exit(1);
        }
        int cache = 0;
        if (i == 0)
        {
            cache = solution[0] * step_size;
        }
        else
        {
            cache = (solution[i] - solution[i-1]) * step_size;
        }
        sprintf(output,"%d\n",cache);
        int ret = write(write_fd,output,128);
        close(write_fd);
        if(ret==-1)
        {
            perror("write error");
            exit(1);
        }
        printf("user %d send %d success on direction %c\n",i,cache,rw);
    }
}


//select M-1 number from 0~n,and Mth must be n
int combine(int n,int m)
{//p[x]=y 取到的第x个元素，是a中的第y个元素
    int index,i,p[MAX_USER],solution_r[MAX_USER],solution_w[MAX_USER];
    int j = 0;
    double min_cost_r = 4294967295U;
    double min_cost_w = 4294967295U;

    if(p==NULL)
    {
        return 0;
    }
    index=0;
    p[index]=0;//get the first element
    int loop=0;
    printf("initial min_cost_w = %lf, min_cost_r = %lf\n",min_cost_w,min_cost_r);
    while(1)
    {
        if(p[index]>=n)
        {
            if(index==0)
            {
                break;
            }
            index--;//go back to the former
            p[index]++;//replace element
        }
        else if(index==m-1)
        {//get enough elements
            qsort(p,m,sizeof(int),cmp);
            double cost_w = cal_write_cost(p);
            double cost_r = cal_read_cost(p);

	    int tmp = 0;
	    printf("try: ");
	    for(tmp = 0;tmp < m;tmp++)
	    {
		printf("%d ",p[tmp]);
	    }
	    printf("\n");
	    printf("corresponding cost_w is %lf, cost_r is %lf, current min_cost_w is %lf, min_cost_r is %lf.\n",cost_w,cost_r,min_cost_w,min_cost_r);

            if (cost_w < min_cost_w)
            {
                for(j = 0;j<user_num;j++)
                {
                    solution_w[j] = p[j];
                }
		min_cost_w = cost_w;
            }
            if (cost_r < min_cost_r)
            {
                for(j = 0;j<user_num;j++)
                {
                    solution_r[j] = p[j];
                }
		min_cost_r = cost_r;
            }

            p[index]++; //replace element
        }
        else
        {// get another element
            index++;
            p[index]=p[index-1]+1;
        }

    }
    printf("now breaking out.\n");

    for(j = 0;j<user_num;j++)
    {
        printf("best solution for user %d on read is %d, on write is %d\n",j,solution_r[j],solution_w[j]);
    }

    adjust(solution_r,m,'r');
    adjust(solution_w,m,'w');

    return 1;
}

/*int main()
{
    while(1)
    {
        combine(total_ssd/step_size + 1,user_num);
        sleep(10);
    }
    return 0;
}*/
