#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<sys/wait.h>
#include<fcntl.h>
#include<time.h>
#include<string.h>
#include<errno.h>
#define MAXITER 1000
#define MAXWRITEBUF (1024*1024)  //写缓冲最大容量
#define MAXREADBUF (1024*1024) //读缓冲最大容量
#define MAX_FILESIZE (300*1024*1024) //大块 300MB最大
#define CONCURRENT 8 //并发数

char RAMpath[32][64],DISKpath[32][64];
char rambase[] ="/root/myram/test",diskbase[]="/usr/test",endtmp[]=".txt";
char write_buff[MAXWRITEBUF];
char read_buff[MAXREADBUF];

void read_file(int blocksize, int isrand, char* filepath){
    int fd = open(filepath,O_RDWR|O_SYNC|O_CREAT,0755);
    if (fd == -1) {
        fprintf(stderr, "FILE OPEN ERROR\n");
        return;
    }
    for (int i=0;i<MAXITER;i++) {
        if (read(fd,read_buff,blocksize) != blocksize) {
            fprintf(stderr, "FILE READ ERROR\n");
            return;
        }
        if (isrand) {
            lseek(fd,(MAXITER-1)*(rand() % blocksize),SEEK_SET);
        }
    }
    lseek(fd, 0, SEEK_SET);
}

void write_file(int blocksize, int isrand, char* filepath) {
    int fd = open(filepath,O_RDWR|O_SYNC|O_CREAT,0755);
    if (fd == -1){
        fprintf(stderr, "FILE OPEN ERROR\n");
        return;
    }
    for (int i=0;i<MAXITER;i++) {
        if(write(fd,write_buff,blocksize)!= blocksize){
            fprintf(stderr,"FILE WRITE ERROR\n");
            return;
        }
        if(isrand){
            lseek(fd,rand() % (MAX_FILESIZE-blocksize),SEEK_SET);
        }
    }
    lseek(fd, 0, SEEK_SET);
}

void Solve_Filename(){
    char test[20][3]={"0","1","2","3","4","5","6","7","8",
    "9","10","11","12","13","14","15","16"};
    for(int i=1;i<=16;i++){
        strcpy(RAMpath[i],rambase);
        strcat(RAMpath[i],test[i]);
        strcat(RAMpath[i],endtmp);
        strcpy(DISKpath[i],diskbase);
        strcat(DISKpath[i],test[i]);
        strcat(DISKpath[i],endtmp);
    }
    return;
}

double calc_time(struct timeval t1, struct timeval t2) {
    return (double)(t2.tv_sec - t1.tv_sec)*1000+(t2.tv_usec-t1.tv_usec)/1000;
}

int main(int argc, char* argv[]){
    srand((unsigned)time(NULL));

    Solve_Filename();

    double Time;
    struct timeval t1,t2;

    for (int i=0;i<MAXWRITEBUF;i+=26)strcat(write_buff,"abcdefghijklmnopqrstuvwxyz");
    printf("BlockSize(KB),Speed(MB/s)\n");

    for(int blocksize=64;blocksize<=64*1024;blocksize*=2){
        gettimeofday(&t1,NULL);
        for(int i=0;i<CONCURRENT;i++){
            if(fork()==0){
                if(!strcmp(argv[1],"W")){
                    //写
                    if(!strcmp(argv[2],"R")){
                        //随机
                        if(!strcmp(argv[3],"R")){
                            //ram盘
                            write_file(blocksize,1,RAMpath[i]);
                        }else{
                            //磁盘
                            write_file(blocksize,1,DISKpath[i]);
                        }
                    }else{
                        //顺序
                        if(!strcmp(argv[3],"R")){
                            //ram盘
                            write_file(blocksize,0,RAMpath[i]);
                        }else{
                            //磁盘
                            write_file(blocksize,0,DISKpath[i]);
                        }
                    }
                }else{
                    //读
                    if(!strcmp(argv[2],"R")){
                        //随机
                        if(!strcmp(argv[3],"R")){
                            //ram盘
                            read_file(blocksize,1,RAMpath[i]);
                        }else{
                            //磁盘
                            read_file(blocksize,1,DISKpath[i]);
                        }
                    }else{
                        //顺序
                        if(!strcmp(argv[3],"R")){
                            //ram盘
                            read_file(blocksize,0,RAMpath[i]);
                        }else{
                            //磁盘
                            read_file(blocksize,0,DISKpath[i]);
                        }
                    }
                }
                exit(0);
            }
        }
        while(wait(NULL)!=-1);
        gettimeofday(&t2,NULL);
        Time=calc_time(t1,t2)/1000.0;
        int sumsize=CONCURRENT*MAXITER*blocksize;
        printf("%lf,%lf\n",((double)blocksize)/1024,((double)sumsize/Time/1024.0/1024));
    }
    return 0;
}
