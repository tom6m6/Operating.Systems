#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<time.h>
#include<string.h>
#define MAXWRITEBUF (1024*1024)  //写缓冲最大容量
char write_buff[MAXWRITEBUF];
char RAMpath[100][64];
char rambase[] ="/root/myram/test",endtmp[]=".txt";
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
    Solve_Filename();
    for(int i=1;i<=99;i++)printf("%s\n",RAMpath[i]);
    return 0;
}