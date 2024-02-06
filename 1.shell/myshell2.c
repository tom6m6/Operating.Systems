#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
typedef unsigned long long u64_t;
#define debug(x) fprintf(stderr, "%d ", x)
#define debugl(x) fprintf(stderr, "%d\n", x)
#define MAXLINE 1024
#define MAXLEN 32
#define MAXARGS 128
#define MAXPATH 128
#define MAXHISTORY 1000
#define PROC_NAME_LEN 16
#define PROC_MAX 1024
#define MAX(a, b) (a) > (b) ? (a) : (b)
/*
 * 记录进程信息
 */ 
struct proc {
    int version;
    char type;
    int endpoint;
    char name[PROC_NAME_LEN + 1];
    char state;
    int blocked;
    int priority;
    int user_time;
    int ticks;
    unsigned long cycle_high;
    unsigned long cycle_low;
    u64_t memory;
    unsigned long effuid;
    unsigned long nice;
    u64_t cpucycles[1];
}prev[PROC_MAX], now[PROC_MAX];
/*
 * 记录历史指令
 */
char *record[MAXHISTORY];
int rec_size = -1;
void eval(char *cmdline, int inPipe);
void preparse(char *cmdline);
int parseline(const char *cmdline, char **argv);
int builtin(char **argv, int argc);
void redirect(char **argv, int *argc);
void direct_null();
void direct_restore(int STDIN_FD_COPY, int STDOUT_FD_COPY);
char* get_pipepos(char *cmdline);
void pipeline(char *cmdline1, char *cmdline2);
void mytop();
void get_meminfo(int *totSize, int *freeSize, int *cachedSize);
void get_kinfo(int *total_proc);
void get_pidinfo(struct proc *p, int *num, u64_t *total, u64_t *idle);
void parse_file(int pid, struct proc *p, u64_t *total, u64_t *idle);
static void sleep_ms(unsigned int secs);
static inline u64_t make64(unsigned long lo, unsigned long hi);
int Open(char *path, int param);
FILE* Fopen(char *path, char *mode);
void Execvp(char *path, char **params);
/*
 * 解析一行命令
 */
void eval(char *cmdline, int inPipe) {
    if (!inPipe) { // 记录history
        record[++rec_size] = (char *)malloc(sizeof(char) * (strlen(cmdline) + 1));
        strcpy(record[rec_size], cmdline);
    }
    char *argv[MAXLINE];
    cmdline[(int)strlen(cmdline) - 1] = ' '; // 把最后的回车改成空格
    preparse(cmdline);
    int argc = parseline(cmdline, argv);
    if (argc == 0) return; // 遇到空行直接返回
    char* cmdline2 = get_pipepos(cmdline);
    if (cmdline2 != NULL) {
        *cmdline2 = '\0';
        pipeline(cmdline, cmdline2 + 1);
    } else if (!builtin(argv, argc)) {
        int wait = argv[argc - 1][0] != '&';
        // fprintf(stderr, "%d\n", wait);
        if (!wait) argv[argc - 1] = NULL, argc--;
        pid_t pid;
        if ((pid = fork()) == 0) {
            if (!wait) {
                direct_null();
                signal(SIGCHLD, SIG_IGN); // 使得minix接管该进程，即不产生僵死进程
            } else redirect(argv, &argc);
            Execvp(argv[0], argv);
        }
        int status;
        if (wait) waitpid(pid, &status, 0);
    }
}
/*
 * 在重定向前后加空格，便于后续解析
 */
void preparse(char *cmdline) {
    char newcmdline[MAXLEN];
    int len = strlen(cmdline), j = 0;
    for (int i = 0; i < len; i++) {
        if (cmdline[i] == '<') newcmdline[j++] = ' ', newcmdline[j++] = '<', newcmdline[j++] = ' '; // '<'
        else if (cmdline[i] == '>') { 
            if (cmdline[i + 1] == '>') { // '>>'
                newcmdline[j++] = ' ';
                newcmdline[j++] = '>';
                newcmdline[j++] = '>';
                newcmdline[j++] = ' ';
                i++;
            } else { // '>'
                newcmdline[j++] = ' ';
                newcmdline[j++] = '>';
                newcmdline[j++] = ' ';
            }
        } else newcmdline[j++] = cmdline[i];
    }
    newcmdline[j] = '\0';
    strcpy(cmdline, newcmdline);
}
/*
 * 解析参数，把参数存入argv，并返回argc
 */
int parseline(const char *cmdline, char **argv) {
    int len = strlen(cmdline), argc = 0, temp = 0;
    for (int i = 0; i < len; i++) {
        if (cmdline[i] == ' ') {
            if (i != 0 && cmdline[i - 1] != ' ') {
                argv[argc][temp] = '\0';
                argc++;
                temp = 0;
            }
            continue;
        }
        if (!temp) argv[argc] = (char *)malloc(sizeof(char) * MAXLEN);
        argv[argc][temp++] = cmdline[i];
    }
    if (temp == 0) argc--;
    return argc + 1;
}
/*
 * 判断是否为内置命令
 */
int builtin(char **argv, int argc) {
    if (!strcmp(argv[0], "cd")) {
        chdir(argv[1]);
        return 1;
    }
    if (!strcmp(argv[0], "history")) {
        redirect(argv, &argc);
        if (argc == 1) {
            for (int i = 0; i <= rec_size; i++) {
                printf("  %d  %s", i + 1, record[i]);
            }
        } else {
            int number;
            // fprintf(stderr, "%s\n", argv[1]);
            sscanf(argv[1], "%d", &number);
            for (int i = MAX(0, rec_size - number + 1); i <= rec_size; i++) {
                printf("  %d  %s", i + 1, record[i]);    
            }
        }
        return 1;
    }
    if (!strcmp(argv[0], "exit")) {
        exit(0);
    }
    if (!strcmp(argv[0], "mytop")) {
        redirect(argv, &argc);
        mytop();
        return 1;
    }
    return 0;
}
/*
 * 根据>, >>, <重定向标准输入输出
 */
void redirect(char **argv, int *argc) {
    for (int i = 0; i < *argc; i++) {
        if (strcmp(argv[i], "<") == 0) {
            if (i + 1 == *argc) {
                fprintf(stderr, "REDIRECT ERROR.\n");
                exit(0);
            }
            close(STDIN_FILENO);
            int file_fd = Open(argv[i + 1], O_RDONLY);
            dup(file_fd);
            *argc -= 2; // 去掉这两个参数 并把后面的参数都往前移两位
            for (int j = i; j < *argc; j++) strcpy(argv[j], argv[j + 2]);
        }
        if (strcmp(argv[i], ">") == 0) {
            if (i + 1 == *argc) {
                fprintf(stderr, "REDIRECT ERROR.\n");
                exit(0);
            }
            close(STDOUT_FILENO);
            int file_fd = Open(argv[i + 1], O_WRONLY | O_CREAT | O_TRUNC);
            dup(file_fd);
            *argc -= 2;
            for (int j = i; j < *argc; j++) strcpy(argv[j], argv[j + 2]);
        }
        if (strcmp(argv[i], ">>") == 0) {
            if (i + 1 == *argc) {
                fprintf(stderr, "REDIRECT ERROR.\n");
                exit(0);
            }
            close(STDOUT_FILENO);
            int file_fd = Open(argv[i + 1], O_WRONLY | O_CREAT | O_APPEND);
            dup(file_fd);
            *argc -= 2;
            for (int j = i; j < *argc; j++) strcpy(argv[j], argv[j + 2]);
        }
    }
    argv[*argc] = NULL; // 保证argv以NULL为结尾
}
/*
 * 把输入输出重定向到/dev/null 
 */
void direct_null() {
    int null_fd = Open("/dev/null", O_WRONLY);
    // fprintf(stderr, "%d\n", file_fd);
    /*
    if (file_fd < 0) {
        fprintf(stderr, "OPEN /dev/null ERROR.\n");
        exit(0);
    }
    */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    dup(null_fd); 
    dup(null_fd);
    dup(null_fd);
}
/*
 * 复原重定向
 */
void direct_restore(int STDIN_FD_COPY, int STDOUT_FD_COPY) {
    close(0); close(1);
    dup(STDIN_FD_COPY); dup(STDOUT_FD_COPY);
}
/*
 * 检查是否存在管道，若存在则返回管道地址
 */
char* get_pipepos(char *cmdline) {
    return strchr(cmdline, '|');    
}
/*
 * 子进程执行cmdline1，将输出通过管道作为主进程cmdline2的输入
 */
void pipeline(char *cmdline1, char *cmdline2) {
    int fd[2];
    pipe(&fd[0]);
    if (fork() == 0) { // 子进程，重定向输出
        close(fd[0]);
        close(STDOUT_FILENO);
        dup(fd[1]);
        close(fd[1]);
        eval(cmdline1, 1);
        exit(0);
    } else { // 父进程，重定向输入
        close(fd[1]);
        close(STDIN_FILENO);
        dup(fd[0]);
        close(fd[0]);
        eval(cmdline2, 1);
    }
}
/*
 * 统计内存和cpu使用情况
 */
void mytop() {  
    int totSize, freeSize, cachedSize, total_proc, proc_num_prev = 0, proc_num_now = 0;
    u64_t total_ticks_pre = 0, total_ticks_now = 0, idle_ticks_pre = 0, idle_ticks_now = 0;
    get_meminfo(&totSize, &freeSize, &cachedSize);
    printf("main memory: ");
    printf(" %dK total,", totSize);
    printf(" %dK free,", freeSize);
    printf(" %dK cached\n", cachedSize);
    get_kinfo(&total_proc);
    printf("Total procs: %d\n", total_proc);
    get_pidinfo(prev, &proc_num_prev, &total_ticks_pre, &idle_ticks_pre);
    sleep_ms(500);
    get_pidinfo(now, &proc_num_now, &total_ticks_now, &idle_ticks_now);
    double CPU_util = 100 - (double)100 * (idle_ticks_now - idle_ticks_pre) / (total_ticks_now - total_ticks_pre);
    printf("CPU utilization : %.4f%%\n", CPU_util);
}
/*
 * 读取/proc/meminfo
 */
void get_meminfo(int *totSize, int *freeSize, int *cachedSize) {
    FILE *fp;
    fp = Fopen("/proc/meminfo", "r");
    int pagesize, total, free, largest, cached;
    fscanf(fp, "%d %d %d %d %d", &pagesize, &total, &free, &largest, &cached);
    *totSize = pagesize * total / 1024;
    *freeSize = pagesize * free / 1024;
    *cachedSize = pagesize * cached / 1024;
    fclose(fp);
}
/*
 * 读取/proc/kinfo
 */
void get_kinfo(int *total_proc) {
    FILE *fp;
    fp = Fopen("/proc/kinfo", "r");
    int proc, task;
    fscanf(fp, "%d %d", &proc, &task);
    *total_proc = proc + task;
    fclose(fp);
}
/*
 * 读取/proc下所有文件夹
 */
void get_pidinfo(struct proc *p, int *num, u64_t *total, u64_t *idle) {
    DIR *p_dir;
    char *end;
    if ((p_dir = opendir("/proc/")) == NULL) {
        fprintf(stderr, "OPENDIR ERROR.\n");
        exit(1);
    }
    struct dirent *p_ent;
    *num = 0;
    for (p_ent = readdir(p_dir); p_ent != NULL; p_ent = readdir(p_dir)) {
        int pid = strtol(p_ent->d_name, &end, 10);
        if (!pid) continue;
        (*num)++;
        parse_file(pid, &p[*num], total, idle);
    }
    closedir(p_dir);
}
/*
 * 读取/proc/pid/psinfo
 */
void parse_file(int pid, struct proc *p, u64_t *total, u64_t *idle) {
    char path[MAXPATH];
    sprintf(path, "/proc/%d/psinfo", pid);
    FILE *fp = Fopen(path, "r");
    fscanf(fp, "%d", &(p->version));
    fscanf(fp, " %c", &(p->type));
    fscanf(fp, " %d", &(p->endpoint));
    fscanf(fp, " %s", p->name);
    fscanf(fp, " %c", &(p->state));
    fscanf(fp, " %d", &(p->blocked));
    fscanf(fp, " %d", &(p->priority));
    fscanf(fp, " %d", &(p->user_time));
    fscanf(fp, " %d", &(p->ticks));
    fscanf(fp, " %lu", &(p->cycle_high));
    fscanf(fp, " %lu", &(p->cycle_low));
    fscanf(fp, " %llu", &(p->memory));
    fscanf(fp, " %lu %lu", &(p->effuid), &(p->nice));
    p->cpucycles[0] = make64(p->cycle_low, p->cycle_high);
    *total += p->cpucycles[0];
    if (p->endpoint == -4) 
        *idle = p->cpucycles[0];
    // printf("PID : %d  CYCLE : %llu  low : %lu  high : %lu  ticks : %d  %c  %c\n", pid, p->cpucycles[0], p->cycle_low, p->cycle_high, p->ticks, p->type, p->state);
    fclose(fp);
}
/*
 * 合并高低周期
 */
static inline u64_t make64(unsigned long lo, unsigned long hi) {
    return ((u64_t)hi << 32 | (u64_t)lo);
}
/*
 * 实现毫秒级的sleep
 */
static void sleep_ms(unsigned int secs) {
    struct timeval tval;
    tval.tv_sec=secs/1000;
    tval.tv_usec=(secs*1000)%1000000;
    select(0,NULL,NULL,NULL,&tval);
}
/*
 * 封装带有错误处理的open函数
 */
int Open(char *path, int param) {
    int file_fd;
    if ((file_fd = open(path, param)) < 0) {
        fprintf(stderr, "OPEN FILE ERROR.\n");
        exit(1);
    } else return file_fd;
}
/*
 * 封装带有错误处理的fopen函数
 */
FILE* Fopen(char *path, char *mode) {
    FILE* fp;
    if ((fp = fopen(path, mode)) == NULL) {
        fprintf(stderr, "FOPEN ERROR.\n");
        exit(1);
    }
    return fp;
}
/*
 * 封装带有错误处理的execvp函数
 */
void Execvp(char *path, char **params) {
    if (execvp(path, params) < 0) {
        fprintf(stderr, "EXECVP ERROR.\n");
        exit(1);
    }
}
int main() {
    char cmdline[MAXLINE];
    int STDIN_FD_COPY = dup(STDIN_FILENO); //保存标准输入输出文件符，用于复原
    int STDOUT_FD_COPY = dup(STDOUT_FILENO);
    while(1) {
        printf(" myshell> ");
        fgets(cmdline, MAXLINE, stdin);
        eval(cmdline, 0);
        direct_restore(STDIN_FD_COPY, STDOUT_FD_COPY);
    }
    return 0;
}