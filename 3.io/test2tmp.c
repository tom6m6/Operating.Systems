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
#define MAX_FILESIZE (300*1024*1024) //大块 300MB最大
#define Blocksize (8*1024)

char write_buff[MAXWRITEBUF];
char RAMpath[100][64];
char rambase[] ="/root/myram/test",endtmp[]=".txt";

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

double calc_time(struct timeval t1, struct timeval t2) {
    return (double)(t2.tv_sec - t1.tv_sec)*1000+(t2.tv_usec-t1.tv_usec)/1000;
}

void Solve_Filename(){
    char test[120][4]={"1","2","3","4","5","6","7","8","9","10",
        "11","12","13","14","15","16","17","18","19","20",
        "21","22","23","24","25","26","27","28","29","30",
        "31","32","33","34","35","36","37","38","39","40",
        "41","42","43","44","45","46","47","48","49","50",
        "51","52","53","54","55","56","57","58","59","60",
        "61","62","63","64","65","66","67","68","69","70",
        "71","72","73","74","75","76","77","78","79","80",
        "81","82","83","84","85","86","87","88","89","90",
        "91","92","93","94","95","96","97","98","99"};
    for(int i=1;i<=99;i++){
        strcpy(RAMpath[i],rambase);
        strcat(RAMpath[i],test[i]);
        strcat(RAMpath[i],endtmp);
    }
    return;
}

int main(){
    srand((unsigned)time(NULL));

    Solve_Filename();

    double Time;
    struct timeval t1,t2;

    for (int i=0;i<MAXWRITEBUF;i+=26)strcat(write_buff,"abcdefghijklmnopqrstuvwxyz");
    struct timeval t1,t2;
    double Time;
    for (int num=1;num<=80;num++) {
        gettimeofday(&t1, NULL);
        for (int i=0;i<num;i++) {
            int pid=fork();
            if (pid==0) {
                write_file(Blocksize,0,RAMpath[i]);
                exit(0);
            }
        }
        while (wait(NULL)!=-1);
        gettimeofday(&t2,NULL);
        Time=calc_time(t1,t2)/1000.0;
        int sumsize=num*MAXITER*Blocksize;
        printf("%d,%lf,%lf\n",num,((double)sumsize/Time/1024.0/1024.0),Time);
    }
    return 0;
}