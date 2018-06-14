#include <stdio.h>
#include <string.h>
#include <error.h>
#include <fcntl.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    int result;
    if(argc!=2)         //判断是否有两个参数
    {
        printf("input error\n");
        exit(1);
    }
    else
    {
        result=access(argv[1],F_OK);//判断该文件是否存在
        if(result==0)               //存在
        {
            unlink(argv[1]);        //删除同名文件
        }
    }
    int ret;
    ret=mkfifo(argv[1],0644);       //创建有名管道
    if(ret!=0)
    {
        perror("mkfifo error");
        exit(1);
    }
}

