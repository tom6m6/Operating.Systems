#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <curses.h>
#include <limits.h>
#include <termcap.h>
#include <termios.h>
#include <time.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <dirent.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/times.h>
#include <sys/time.h>
#include <sys/select.h>

#include <minix/com.h>
#include <minix/config.h>
#include <minix/type.h>
#include <minix/endpoint.h>
#include <minix/const.h>
#include <minix/u64.h>
#include <paths.h>
#include <minix/procfs.h>

//开始宏定义
#define MAXCMD 1024 //最大记录的命令行数
#define MAXLINE 1024 //每行最大长度

#define MAXARG 64 //命令行中某命令最大参数数量
#define MAXPIPE 64 //通过管道连接的最大程序数量

#define MAXBUF 1024 //Max buffer size for an file read operation

const char *cputimenames[]={ "user", "ipc","kernelcall" }; //CPU cycle types
#define CPUTIMENAMES (sizeof(cputimenames)/sizeof(cputimenames[0]))

#define CPUTIME(m, i) (m & (1L << (i)))
#define USED 0x1
#define IS_TASK 0x2
#define IS_SYSTEM 0x4
#define BLOCKED 0x8

/* Process types. */
#define TYPE_TASK 'T'
#define TYPE_SYSTEM 'S'
#define TYPE_USER 'U'

/* General process states. */
#define STATE_SLEEP 'S'
#define STATE_WAIT 'W'
#define STATE_ZOMBIE 'Z'
#define STATE_RUN 'R'
#define STATE_STOP 'T'

/* Kernel tasks. These all run in the same address space. */
#define ASYNCM ((int) -5) /* notifies about finished async sends */
#define IDLE ((int) -4) /* runs when no one else can run */
#define CLOCK ((int) -3) /* alarms and other clock functions */
#define SYSTEM ((int) -2) /* request system functionality */
#define KERNEL ((int) -1) /* pseudo-process for IPC and scheduling */
#define HARDWARE KERNEL /* for hardware interrupt handlers */



//全局变量和数据类型
int cnt=0;//当前读到了第几条命令
char cmd_history[MAXCMD][MAXLINE];
struct Detail{
    //这个结构体是在记录每一行的详细信息
    int bg;
    int pipenum; //通过管道连接的程序的数量
    int input,output; //输入输出模式
    //input 0标准输入 1文件输入
    //output 0标准输出 1文件输出(覆盖) 2文件输出(追加)
    char *input_file;//输入文件地址
    char *output_file;//输出文件地址
};
struct proc{
    int p_flags;
    int p_endpoint;
    pid_t p_pid;
    uint64_t p_cpucycles[CPUTIMENAMES];
    int p_priority;
    int p_blocked;
    time_t p_user_time;
    long unsigned int p_memory;
    uid_t p_effuid;
    int p_nice;
    char p_name[17];
};
struct tp {
    struct proc* p;
    u64_t ticks;
};

int slot;
unsigned int nr_procs, nr_tasks;
int nr_total; //Number of process + task
struct proc *proc = NULL, *prev_proc = NULL;

//开始各种函数名
void eval(char *cmdline);
void parseline(char *cmdline,char *argv[MAXPIPE][MAXARG],struct Detail *detail);
int builtin_cmd(char **argv);

//打印内存和CPU信息模块的函数名
void mytop();
void get_memory(int *total_size, int *free_size, int *cached_size);
void getkinfo();
void parse_file(pid_t pid);
void parse_dir();
void get_procs();
uint64_t make_cycle(unsigned long lo, unsigned long hi);
uint64_t cputicks(struct proc *p1, struct proc *p2, int timemode);
float print_procs(struct proc *proc1, struct proc *proc2, int cputimemode);


int main(int argc,char **argv){
    char cmdline[MAXLINE];
    fprintf(stdout,"Hello there!I'm tommy and this is the project of OS in ECNU\n");
    while(1){
        printf("tommyshell  %s >",getcwd(NULL,NULL));
        if(fgets(cmdline,MAXLINE,stdin)==NULL){
            //通过标准输入将命令行读入到cmdline中
            printf("Error occurs when input\n"); 
            continue;
        }
        strcpy(cmd_history[cnt],cmdline); //备份
        ++cnt;
        eval(cmdline);
    }
    return 0;
}

void eval(char *cmdline){
    char *argv[MAXPIPE][MAXARG];struct Detail detail; pid_t pid;   
    if(strlen(cmdline)==0)return;
    parseline(cmdline,argv,&detail);//解析
    int pipegate[MAXPIPE][2];



    if(!builtin_cmd(argv[0])){
        if(detail.bg)signal(SIGCHLD, SIG_IGN);
        else signal(SIGCHLD, SIG_DFL); 

        if(detail.pipenum>1){
            //如果有管道
            if((pid=fork())==0){
                for(int i=0;i<detail.pipenum;i++){
                    pipe(pipegate[i]);
                    if((pid=fork())==0){
                        if(i==0){
                            if(detail.input==1){
                                int fd=open(detail.input_file,O_RDONLY);  
                                close(STDIN_FILENO); dup(fd);  
                            }
                            close(STDOUT_FILENO);
                            close(pipegate[i][0]);  
                            dup(pipegate[i][1]); 
                        }
                        else if (i==detail.pipenum-1){
                            if(detail.output== 1){
                                int fd=open(detail.output_file,O_CREAT|O_WRONLY|O_TRUNC,S_IREAD|S_IWRITE);  
                                close(STDOUT_FILENO);dup(fd);  
                            }
                            else if(detail.output== 2){
                                int fd=open(detail.output_file,O_CREAT|O_WRONLY|O_APPEND,S_IREAD|S_IWRITE);  
                                close(STDOUT_FILENO);dup(fd);  
                            }
                            close(STDIN_FILENO);  
                            close(pipegate[i-1][1]);  
                            dup(pipegate[i-1][0]); 

                            if(detail.bg){
                                int fd=open("/dev/null", O_RDWR);  
                                close(STDIN_FILENO);dup(fd);
                                close(STDOUT_FILENO);dup(fd);
                            }
                        }
                        else{
                            close(pipegate[i-1][1]);
                            close(STDIN_FILENO);   
                            dup(pipegate[i-1][0]); 

                            close(pipegate[i][0]);
                            close(STDOUT_FILENO); 
                            dup(pipegate[i][1]); 
                        }

                        if(execvp(argv[i][0], argv[i]) < 0) {  
                            fprintf(stdout,"%s: Program not found.\n",argv[i][0]);  
                            exit(0);  
                        }

                    }else{
                        close(pipegate[i][1]);
                        int state;  
                        waitpid(pid,&state,0);  
                    }
                }
                exit(0);
            }else{
                if(detail.bg==0){
                    int state;  
                    waitpid(pid, &state, 0);  
                }
            }
        }
        else{
            //如果没管道
            if((pid=fork())==0){
                if(detail.input==1){
                    int fd = open(detail.input_file, O_RDONLY);  
                    close(STDIN_FILENO);  
                    dup(fd);  
                }

                if(detail.output==1){
                    int fd = open(detail.output_file, O_WRONLY | O_CREAT);
                    close(STDOUT_FILENO);
                    dup(fd);
                }
                else if(detail.output==2){
                    int fd = open(detail.output_file, O_WRONLY | O_APPEND);
                    close(STDOUT_FILENO);
                    dup(fd);
                }

                if(detail.bg){
                    int fd = open("/dev/null",O_RDWR);
                    close(STDIN_FILENO);dup(fd);
                    close(STDOUT_FILENO);dup(fd);
                }

                
                if (execvp(argv[0][0],argv[0]) < 0) {
                    fprintf(stdout,"%s: Program not found.\n", argv[0][0]); 
                    exit(0);
                }
            }else{
                if(detail.bg==0){
                    int state;  
                    waitpid(pid, &state, 0);  
                }
            }
        }
    }
    return;
}

void parseline(char *cmdline,char *argv[MAXPIPE][MAXARG],struct Detail *detail){
    //parseline读取cmdline然后把结果放到argv和detail中
    char *locate;//用于定位
    int argc; //一个命令后面所接参数数量
    int cnt1; //目前记录了的命令数量
    
    if(strlen(cmdline)==0)return;
    cmdline[strlen(cmdline) - 1] = ' '; // Replace trailing '\n' with space  
    while (*cmdline && (*cmdline == ' '))cmdline++;
 
    detail->input=0;detail->output=0;detail->bg=0;
    cnt1=0;argc=0;  
    locate=strchr(cmdline,' ');  
    while (locate) {  
        char *arg_tmp=strtok(cmdline," ");  
        if(!strcmp(arg_tmp, "<")){ 
            detail->input= 1; 
            *locate='\0';  
            cmdline=locate+1;  
            while (*cmdline && (*cmdline ==' '))cmdline++;  
            locate=strchr(cmdline,' ');  
            if(!locate){  
                printf("Where is the file?!");  
                return;  
            }  
            detail->input_file=strtok(cmdline," ");  
        }
        else if(!strcmp(arg_tmp,">")){  
            detail->output= 1;
            *locate='\0';  
            cmdline=locate+1;  
            while (*cmdline && (*cmdline==' '))cmdline++;  
            locate=strchr(cmdline,' ');  
            if(!locate){  
                printf("Where is the file?!");  
                return;  
            } 
            detail->output_file=strtok(cmdline," ");  
        }  
        else if(!strcmp(arg_tmp, ">>")){ 
            detail->output=2;
            *locate='\0';  
            cmdline=locate + 1;  
            while(*cmdline && (*cmdline == ' '))cmdline++;  
            locate=strchr(cmdline,' ');  
            if(!locate){ 
                printf("Where is the file?!");   
                return;  
            }
            detail->output_file=strtok(cmdline," ");  
        }  
        else if(!strcmp(arg_tmp, "|")){  
            ++argc;  
            argv[cnt1][argc]=NULL;  
            ++cnt1;  
            argc=0;  
        }  
        else{  
            argv[cnt1][argc]=arg_tmp;  
            argc++; 
        } 
        *locate='\0';  
        cmdline=locate+1;  
        while (*cmdline && (*cmdline == ' '))cmdline++;  
        locate=strchr(cmdline,' ');  
    }  
    argv[cnt1][argc]=NULL;  
    detail->pipenum=cnt1+1;  
 
    if(argc == 0){
        //如果最后一个命令压根就没有参数 
        //如果不加这个，会出错，因为后面的else if判断可能越界
        detail->bg = 0;  
    } 
    else if(!strcmp(argv[cnt1][argc-1],"&"))detail->bg=1;  


    if (detail->bg==1){  
        argc--;  
        argv[cnt1][argc] = NULL;  
    }  
}

int builtin_cmd(char **argv){
    if(!strcmp(argv[0],"cd")){
        if(chdir(argv[1])<0){
            printf("Error when changing working directory!\n");
        }
        return 1;
    }
    else if(!strcmp(argv[0],"history")){
        int n;
        if(!argv[1])n=cnt;
        else n=atoi(argv[1]);
        for(int i=cnt-n;i<cnt;i++)fprintf(stdout,"%s\n",cmd_history[i]);
        return 1;
    }
    else if(!strcmp(argv[0],"mytop")){
        //too complex!
        mytop();
        return 1;
    }
    else if(!strcmp(argv[0],"exit"))exit(0);

    return 0;
}


void mytop(){
    int total_size,free_size,cached_size; //主存信息
    get_memory(&total_size,&free_size,&cached_size);
    fprintf(stdout, "Total: %dK, Free: %dK, Cached: %dK\n", total_size, free_size, cached_size);
    getkinfo();
    get_procs();
    if (prev_proc == NULL)get_procs();
    float idle = print_procs(prev_proc, proc, 1);
    fprintf(stdout, "CPU Usage: %f%%\n", idle);
}

void get_memory(int *total_size, int *free_size, int *cached_size){
    int mem_f; //File descriptor of memory info
    int bufsize; //Actual size of bytes read
    char buf[MAXBUF]; //Buffer for reading from file
    int page_size, total_page, free_page, largest_page, cached_page;

    mem_f = open("/proc/meminfo", O_RDONLY); //Open memory info file
    bufsize = read(mem_f, buf, sizeof(buf)); //Read memory info
    if(bufsize == -1){
        printf("Error reading memory info!");
    }
    else{
        page_size = atoi(strtok(buf, " "));
        total_page = atoi(strtok(NULL, " "));
        free_page = atoi(strtok(NULL, " "));
        largest_page = atoi(strtok(NULL, " "));
        cached_page = atoi(strtok(NULL, " "));
        *total_size = (page_size * total_page) / 1024;
        *free_size = (page_size * free_page) / 1024;
        *cached_size = (page_size * cached_page) / 1024;
    }
}

void getkinfo(){
    int fd; //File descriptor of kinfo
    int bufsize; //Actual buffer size
    char buf[MAXBUF], pathbuf[MAXBUF]; //Buffer for file reading

    fd = open("/proc/kinfo", O_RDONLY);
    if (fd == -1) {
        printf("Reading kinfo file error!");
        exit(1);
    }
    bufsize = read(fd, buf, sizeof(buf)); //Read process info
    if(bufsize == -1){
        printf("Error reading total process info!");
    }
    else {
        nr_procs = (unsigned int) atoi(strtok(buf, " ")); //Number of process
        nr_tasks = (unsigned int) atoi(strtok(NULL, " ")); //Number of tasks
        close(fd);
        nr_total = (int) (nr_procs + nr_tasks);
    }
}

void parse_file(pid_t pid)  {  
    char path[MAXBUF], name[256], type, state;  
    int version, endpt, effuid;  
    unsigned long cycles_hi, cycles_lo;  
    FILE *fp;  
    struct proc *p;  
    int i;  
  
    sprintf(path, "/proc/%d/psinfo", pid);  
  
    if ((fp = fopen(path, "r")) == NULL)  
        return;  
  
    if (fscanf(fp, "%d", &version) != 1) {  
        fclose(fp);  
        return;  
    }  
  
    if (fscanf(fp, " %c %d", &type, &endpt) != 2) {  
        fclose(fp);  
        return;  
    }  
  
    slot++;  
  
    if(slot < 0 || slot >= nr_total) {  
        fprintf(stderr, "Unreasonable endpoint number %d\n", endpt);  
        fclose(fp);  
        return;  
    }  
  
    p = &proc[slot];  
  
    if (type == TYPE_TASK)  
        p->p_flags |= IS_TASK;  
    else if (type == TYPE_SYSTEM)  
        p->p_flags |= IS_SYSTEM;  
  
    p->p_endpoint = endpt;  
    p->p_pid = pid;  
  
    if (fscanf(fp, " %255s %c %d %d %lu %*u %lu %lu",  
               name, &state, &p->p_blocked, &p->p_priority,  
               &p->p_user_time, &cycles_hi, &cycles_lo) != 7) {  
  
        fclose(fp);  
        return;  
    }  
  
    strncpy(p->p_name, name, sizeof(p->p_name)-1);  
    p->p_name[sizeof(p->p_name)-1] = 0;  
  
    if (state != STATE_RUN)  
        p->p_flags |= BLOCKED;  
    p->p_cpucycles[0] = make_cycle(cycles_lo, cycles_hi);  
    p->p_memory = 0L;  
  
    if (!(p->p_flags & IS_TASK)) {  
        int j;  
        if ((j=fscanf(fp, " %lu %*u %*u %*c %*d %*u %u %*u %d %*c %*d %*u",  
                      &p->p_memory, &effuid, &p->p_nice)) != 3) {  
  
            fclose(fp);  
            return;  
        }  
  
        p->p_effuid = effuid;  
    } else p->p_effuid = 0;  
  
    for(i = 1; i < CPUTIMENAMES; i++) {  
        if(fscanf(fp, " %lu %lu",  
                  &cycles_hi, &cycles_lo) == 2) {  
            p->p_cpucycles[i] = make_cycle(cycles_lo, cycles_hi);  
        } else  {  
            p->p_cpucycles[i] = 0;  
        }  
    }  
  
    if ((p->p_flags & IS_TASK)) {  
        if(fscanf(fp, " %lu", &p->p_memory) != 1) {  
            p->p_memory = 0;  
        }  
    }  
  
    p->p_flags |= USED;  
  
    fclose(fp);  
}  

void parse_dir(){
    DIR *p_dir; //Pointer of directory
    struct dirent *p_ent; //Info of the directory
    pid_t pid; //Name of sub directory(PID)
    char *end;

    if ((p_dir = opendir("/proc")) == NULL) {
        exit(1);
    }

    //Traverse the directory
    for (p_ent = readdir(p_dir); p_ent != NULL; p_ent = readdir(p_dir)) {
        pid = strtol(p_ent->d_name, &end, 10); //Get the name of sub directory
        if (!end[0] && pid != 0)parse_file(pid);
    }
    closedir(p_dir);
}

void get_procs(){
    struct proc *p;
    int i;
    p = prev_proc;
    prev_proc = proc;
    proc = p;

    if (proc == NULL) {
        proc = malloc(nr_total * sizeof(proc[0])); //Allocate a new process structure
        //Allocate failed
        if (proc == NULL) {
            fprintf(stderr, "Out of memory!\n");
            exit(1);
        }
    }
    //Initialize all the entry ranging in the total process+task num
    for (i = 0; i < nr_total; i++)proc[i].p_flags = 0;
    parse_dir();
}

uint64_t make_cycle(unsigned long lo, unsigned long hi){
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

uint64_t cputicks(struct proc *p1, struct proc *p2, int timemode){
    int i;
    uint64_t t = 0;
    for(i = 0; i < CPUTIMENAMES; i++) {
        if(!CPUTIME(timemode, i))continue;
        if(p1->p_endpoint == p2->p_endpoint) {
            t = t + p2->p_cpucycles[i] - p1->p_cpucycles[i];
        } else {
            t = t + p2->p_cpucycles[i];
        }
    }
    return t;
}


float print_procs(struct proc *proc1, struct proc *proc2, int cputimemode){
    int p, nprocs;
    uint64_t systemticks = 0;
    uint64_t userticks = 0;
    uint64_t total_ticks = 0;
    static struct tp *tick_procs = NULL;

    if (tick_procs == NULL) {
        tick_procs = malloc(nr_total * sizeof(tick_procs[0]));
        if (tick_procs == NULL) {
            fprintf(stderr, "Out of memory!\n");
            exit(1);
        }
    }

    for (p = nprocs = 0; p < nr_total; p++) {
        uint64_t uticks;
        if (!(proc2[p].p_flags & USED))continue;
        tick_procs[nprocs].p = proc2 + p;
        tick_procs[nprocs].ticks = cputicks(&proc1[p], &proc2[p], cputimemode);
        uticks = cputicks(&proc1[p], &proc2[p], 1);
        total_ticks = total_ticks + uticks;
        if(!(proc2[p].p_flags & IS_TASK)) {
            if(proc2[p].p_flags & IS_SYSTEM)systemticks = systemticks + tick_procs[nprocs].ticks;
            else userticks = userticks + tick_procs[nprocs].ticks;
        }
        nprocs++;
    }

    if (total_ticks == 0)return 0.0;
    return 100.0 * (systemticks + userticks) / total_ticks;
}
