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

#include <minix/com.h>
#include <minix/config.h>
#include <minix/type.h>
#include <minix/endpoint.h>
#include <minix/const.h>
#include <minix/u64.h>
#include <paths.h>
#include <minix/procfs.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/times.h>
#include <sys/time.h>
#include <sys/select.h>

#define MAXCMD 1024 //最大记录的命令行数
#define MAXLINE 1024 //每行最大长度
#define MAXARG 64 //命令行最大参数数量


#define USED 0x1
#define IS_TASK 0x2
#define IS_SYSTEM 0x4
#define BLOCKED 0x8
#define PSINFO_VERSION 0

#define STATE_RUN 'R'
const char *cputimenames[] = {"user", "ipc", "kernelcall"};
#define CPUTIMENAMES ((sizeof(cputimenames)) / (sizeof(cputimenames[0]))) //恒等于3
#define CPUTIME(m, i) (m & (1 << (i)))                                    //保留第几位





int cnt=0; //命令计数
char cmd_history[MAXCMD][MAXLINE];

unsigned int nr_procs, nr_tasks;
int slot = -1;
int nr_total;

struct proc
{
    int p_flags;
    endpoint_t p_endpoint;           //端点
    pid_t p_pid;                     //进程号
    u64_t p_cpucycles[CPUTIMENAMES]; //CPU周期
    int p_priority;                  //动态优先级
    endpoint_t p_blocked;            //阻塞状态
    time_t p_user_time;              //用户时间
    vir_bytes p_memory;              //内存
    uid_t p_effuid;                  //有效用户ID
    int p_nice;                      //静态优先级
    char p_name[PROC_NAME_LEN + 1];  //名字
};
struct proc *proc = NULL, *prev_proc = NULL;
struct tp{
    struct proc *p;
    u64_t ticks;
};



void eval(char *cmdline);
int parseline(char *cmdline,char **argv);
int builtin_cmd(char **argv);
void dopipe(char *argv[],char *tmpargv[]);

void mytop();
void getkinfo();
int print_memory();
void get_procs();
void parse_dir();
void parse_file(pid_t pid);
u64_t cputicks(struct proc *p1, struct proc *p2, int timemode);
void print_procs(struct proc *proc1, struct proc *proc2, int cputimemode);


void dopipe(char *argv[],char *tmpargv[]){
    int tmpfd[2];
    pipe(&tmpfd[0]);
    pid_t pid;
    int status;
    if((pid=fork())==0){
        close(tmpfd[0]);
        close(STDOUT_FILENO);//关闭输出
        dup(tmpfd[1]);
        close(tmpfd[1]);
        execvp(argv[0],argv);//接着执行前面的指令
    }
    else{
        close(tmpfd[1]);
        close(0);
        dup(tmpfd[0]);
        close(tmpfd[0]);
        waitpid(pid,&status,0);
        execvp(tmpargv[0],tmpargv);
    }
}

int parseline(char *cmdline,char **argv){
    //我的parseline的目的:
    //解析命令行cmdline 然后将结果放到argv中
    //函数的返回值是该命令是前台(0)还是后台(1)
    static char array[MAXLINE];
    char *buf = array;
    int argc = 0;
    int bg;

    //char *strtok(char *s,char *delim) 实现原理：将分隔符出现的地方改为'\0'
    strcpy(buf, cmdline);
    buf[strlen(buf) - 1] = ' ';   //将行末尾的回车改为空格
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
        buf++;

    char *s = strtok(buf, " ");
    if (s == NULL)
    {
        exit(0);
    }
    argv[argc] = s;
    argc++;
    while ((s = strtok(NULL, " ")) != NULL)
    { //参数设置为NULL，从上一次读取的地方继续
        argv[argc] = s;
        argc++;
    }
    argv[argc] = NULL;

    if (argc == 0) /* ignore blank line */
        return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[(argc)-1] == '&')) != 0)
    {
        argv[--(argc)] = NULL;
    }
    return bg;
}

int builtin_cmd(char **argv){
    //判断是不是内置命令，如果是就执行，不是就返回0
    if(!strcmp(argv[0],"exit"))exit(0);

    if (!strcmp(argv[0],"cd")){
        if (!argv[1])argv[1] = "."; //什么路径都没输入
        if (chdir(argv[1])< 0){
            printf("Error occurs when cd\n");
        }
        return 1;
    }
    else if(!strcmp(argv[0],"history")){
        if(!argv[1]){
            //只是单纯地输入了一个history
            for(int i=0;i<cnt;i++){
                fprintf(stdout,"%s",cmd_history[i]);
            }
        }
        else{
            int num=atoi(argv[1]);
            if(num>cnt){
                printf("Error occurs when history\n");
            }
            else{
                for(int i=cnt-num;i<cnt;i++){
                    fprintf(stdout,"%s",cmd_history[i]);
                }
            }
        }
        return 1;
    }
    else if(!strcmp(argv[0],"mytop")){
        mytop();
        return 1;
    }
    return 0;
}

void eval(char *cmdline){
    //来执行命令啦
    char *argv[MAXARG];
    int bg; //前台还是后台？bg为1则为后台，否则是前台
    pid_t pid;

    char *filename; 
    int fd;
    int status;

    int choice; //哪一种情况？0正常 1> 2< 3| 4&(后台) 5>>

    char tmp[MAXLINE];
    strcpy(tmp,cmdline);
    bg=parseline(tmp,argv);

    if(argv[0]==NULL)return; //结束了
    if(bg==1)choice=4;
    if(builtin_cmd(argv)){
        //内置命令直接返回
        return;
    }
    for(int i=0;argv[i];i++){
        if(!strcmp(argv[i],">")){
            if(!strcmp(argv[i+1],">")){
                choice=5;
                break;
            }
            choice=1;
            filename=argv[i+1];
            argv[i]=NULL;
            break;
        }
    }
    for(int i=0;argv[i];i++){
        if(!strcmp(argv[i],"<")){
            choice=2;
            filename=argv[i+1];
            argv[i]=NULL;
            break;
        }
    }
    char *tmpargv[MAXARG];
    for(int i=0;argv[i];i++){
        if(!strcmp(argv[i],"|")){
            choice=3;
            argv[i]=NULL;
            int tmp;

            //这里可以简化的，用strcpy写
            for(tmp=i+1;argv[tmp];tmp++){
                tmpargv[tmp-i-1]=argv[tmp];
            }
            tmpargv[tmp-i-1]=NULL;
            break;
        }
    }

    switch(choice){
        case 0:
            if((pid=fork())==0){
                execvp(argv[0],argv);
                exit(0); //赶紧结束了
            }
            if(waitpid(pid,&status,0)==-1){
                //父进程等待子进程的结束
                printf("Error occurs when waitpid\n");
            }
            break;
        case 1:
            if((pid=fork())==0){
                fd=open(filename,O_RDWR|O_CREAT|O_TRUNC,0644);
                dup2(fd,1);//映射到 1标准输出
                close(fd);
                execvp(argv[0],argv);
                exit(0);
            }
            if(waitpid(pid,&status,0)==-1){
                //父进程等待子进程的结束
                printf("Error occurs when waitpid\n");
            }
            break;
        case 2:
            if((pid=fork())==0){
                fd=open(filename,O_RDONLY);
                dup2(fd,0); //映射到 0标准输入
                close(fd);
                execvp(argv[0],argv);
                exit(0);
            }
            if(waitpid(pid,&status,0)==-1){
                //父进程等待子进程的结束
                printf("Error occurs when waitpid\n");
            }
            break;
        case 3:
            if((pid=fork())==0){
                dopipe(argv,tmpargv);
            }
            if(waitpid(pid,&status,0)==-1){
                //父进程等待子进程的结束
                printf("Error occurs when waitpid\n");
            }
            break;
        case 4:
            signal(SIGCHLD,SIG_IGN);
            if((pid=fork())==0){
                fd=open("/dev/null",O_RDWR);

                close(STDIN_FILENO);
                dup(fd);
                close(STDOUT_FILENO);
                dup(fd);

                execvp(argv[0],argv);
                exit(0);
            }
            break;//因为是后台运行，直接break即可
        default:
            break;
    }
    return;
}






void mytop(){
    int cputimemode = 1;//计算CPU的时钟周期
    getkinfo();
    print_memory();
    //得到prev_proc
    get_procs();
    if (prev_proc == NULL){
        get_procs();//得到proc
    }
    print_procs(prev_proc, proc, cputimemode);
}

// /proc/kinfo查看进程和任务数量
void getkinfo(){
    FILE *fp;
    if ((fp = fopen("/proc/kinfo", "r")) == NULL)
    {
        fprintf(stderr, "opening /proc/kinfo failed\n");
        exit(1);
    }

    if (fscanf(fp, "%u %u", &nr_procs, &nr_tasks) != 2)
    {
        fprintf(stderr, "reading from /proc/kinfo failed");
        exit(1);
    }

    fclose(fp);

    //nr_total是一个全局变量
    nr_total = (int)(nr_procs + nr_tasks);
}

// /proc/meminfo查看内存信息*/
int print_memory(){
    FILE *fp;
    unsigned long pagesize, total, free, largest, cached;

    if ((fp = fopen("/proc/meminfo", "r")) == NULL)
    {
        return 0;
    }

    if (fscanf(fp, "%lu %lu %lu %lu %lu", &pagesize, &total, &free, &largest, &cached) != 5)
    {
        fclose(fp);
        return 0;
    }

    fclose(fp);

    printf("main memory: %ldk total,%ldk free,%ldk contig free,%ldk cached\n", (pagesize * total) / 1024,
           (pagesize * free) / 1024, (pagesize * largest) / 1024, (pagesize * cached) / 1024);

    return 1;
}

void get_procs()
{
    struct proc *p;
    int i;
    //交换了prev_proc&proc
    p = prev_proc;
    prev_proc = proc;
    proc = p;

    if (proc == NULL)
    {
        //proc是struct proc的集合，申请了
        //nr_total个proc的空间
        proc = malloc(nr_total * sizeof(proc[0])); //struct proc的大小
        if (proc == NULL)
        {
            fprintf(stderr, "Out of memory!\n");
            exit(0);
        }
    }

    for (i = 0; i < nr_total; i++)
    {
        proc[i].p_flags = 0; 
    }

    parse_dir();
}

void parse_dir()
{
    DIR *p_dir;
    struct dirent *p_ent; 
    pid_t pid;
    char *end; //指向第一个不可转换的字符位置的指针

    if ((p_dir = opendir("/proc")) == NULL)
    {
        perror("opendir on /proc");
        exit(0);
    }

    //读取目录下的每一个文件信息
    for (p_ent = readdir(p_dir); p_ent != NULL; p_ent = readdir(p_dir))
    {
        //long int strtol (const char* str, char** endptr, int base);
        //将字符串转化为长整数，endptr第一个不可转换的位置的字符指针，base要转换的进制
        //合法字符为0x1-0x9
        pid = strtol(p_ent->d_name, &end, 10);
        //由文件名获取进程号
        //pid由文件名转换得来
        //ASCII码对照表，NULL的值为0
        if (!end[0] && pid != 0)
        {
            parse_file(pid);
        }
    }
    closedir(p_dir);
}

void parse_file(pid_t pid)
{
    //PATH_MAX定义在头文件<limits.h>，对路径名长度的限制
    char path[PATH_MAX], name[256], type, state;
    int version, endpt, effuid;         //版本，端点，有效用户ID
    unsigned long cycles_hi, cycles_lo; //高周期，低周期
    FILE *fp;
    struct proc *p;
    int i;
    //将proc/pid/psinfo路径写入path
    sprintf(path, "/proc/%d/psinfo", pid);

    if ((fp = fopen(path, "r")) == NULL)
    {
        return;
    }

    if (fscanf(fp, "%d", &version) != 1)
    {
        fclose(fp);
        return;
    }

    if (version != PSINFO_VERSION)
    {
        fputs("procfs version mismatch!\n", stderr);
        exit(1);
    }

    if (fscanf(fp, " %c %d", &type, &endpt) != 2)
    {
        fclose(fp);
        return;
    }

    slot++; //顺序取出每个proc让所有task的slot不冲突

    if (slot < 0 || slot >= nr_total)
    {
        fprintf(stderr, "mytop:unreasonable endpoint number %d\n", endpt);
        fclose(fp);
        return;
    }

    p = &proc[slot]; //取得对应的struct proc

    if (type == TYPE_TASK)
    {
        p->p_flags |= IS_TASK; //0x2 倒数第二位标记为1
    }
    else if (type == TYPE_SYSTEM)
    {
        p->p_flags |= IS_SYSTEM; //0x4 倒数第三位标记为1
    }
    p->p_endpoint = endpt;
    p->p_pid = pid;
    //%*u添加了*后表示文本读入后不赋给任何变量
    if (fscanf(fp, " %255s %c %d %d %lu %*u %lu %lu",
               name, &state, &p->p_blocked, &p->p_priority,
               &p->p_user_time, &cycles_hi, &cycles_lo) != 7)
    {
        fclose(fp);
        return;
    }

    //char*strncpy(char*dest,char*src,size_tn);
    //复制src字符串到dest中，大小由tn决定
    strncpy(p->p_name, name, sizeof(p->p_name) - 1);
    p->p_name[sizeof(p->p_name) - 1] = 0;

    if (state != STATE_RUN)
    {
        p->p_flags |= BLOCKED; //0x8 倒数第四位标记为1
    }

    //user的CPU周期
    p->p_cpucycles[0] = make64(cycles_lo, cycles_hi);
    p->p_flags |= USED; //最低位标记位1

    fclose(fp);
}

u64_t cputicks(struct proc *p1, struct proc *p2, int timemode)
{
    int i;
    u64_t t = 0;
    for (i = 0; i < CPUTIMENAMES; i++)
    {
        //printf("i=%d,%d\n",i,CPUTIME(timemode,i));
        if (!CPUTIME(timemode, i))
        {
            continue;
        }
        //printf("run\n");
        //timemode==1只有i等于0是CPUTIME才等于1
        //只有i=0时会执行后面的，即只计算了CPU的时钟周期不会对另外两个做计算
        //p_cpucycles第二个值为ipc，第三个值为kernelcall的数量
        //如果两个相等则作差求时间差
        if (p1->p_endpoint == p2->p_endpoint)
        {
            t = t + p2->p_cpucycles[i] - p1->p_cpucycles[i];
        }
        else
        { //否则t直接加上p2
            t = t + p2->p_cpucycles[i];
        }
    }
    return t;
}

void print_procs(struct proc *proc1, struct proc *proc2, int cputimemode)
{
    int p, nprocs;
    u64_t systemticks = 0;
    u64_t userticks = 0;
    u64_t total_ticks = 0;
    u64_t idleticks = 0;
    u64_t kernelticks = 0;
    int blockedseen = 0;
    //创建了一个struct tp的结构体数组
    static struct tp *tick_procs = NULL;
    if (tick_procs == NULL)
    {
        tick_procs = malloc(nr_total * sizeof(tick_procs[0]));
        if (tick_procs == NULL)
        {
            fprintf(stderr, "Out of memory!\n");
            exit(1);
        }
    }
    for (p = nprocs = 0; p < nr_total; p++)
    {
        u64_t uticks;
        if (!(proc2[p].p_flags & USED))
        { //查看USED位是否被标记
            continue;
        }
        tick_procs[nprocs].p = proc2 + p;//初始化
        //tickprocs的第np个结构体的struct proc *p
        //为proc2第p个文件的struct proc
        tick_procs[nprocs].ticks = cputicks(&proc1[p], &proc2[p], cputimemode);
        uticks = cputicks(&proc1[p], &proc2[p], 1);
        total_ticks = total_ticks + uticks;
        if (p - NR_TASKS == IDLE)
        {
            idleticks = uticks;
            continue;
        }
        if (p - NR_TASKS == KERNEL)
        {
            kernelticks = uticks;
        }
        if (!(proc2[p].p_flags & IS_TASK))
        {
            //如果是进程，则看是system还是user
            if (proc2[p].p_flags & IS_SYSTEM)
            {
                systemticks = systemticks + tick_procs[nprocs].ticks;
            }
            else
            {
                userticks = userticks + tick_procs[nprocs].ticks;
            }
        }
        nprocs++;
    }
    if (total_ticks == 0)
    {
        return;
    }
    printf("CPU states: %6.2f%% user, ", 100.0 * userticks / total_ticks);
    printf("%6.2f%% system, ", 100.0 * systemticks / total_ticks);
    printf("%6.2f%% kernel, ", 100.0 * kernelticks / total_ticks);
    printf("%6.2f%% idle\n", 100.0 * idleticks / total_ticks);
}


int main(int argc,char **argv){
    char cmdline[MAXLINE];
    while(1){
        printf("%s myshell>",getcwd(NULL,NULL));
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