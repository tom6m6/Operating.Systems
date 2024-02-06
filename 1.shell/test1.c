void eval(char *cmdline){  
 struct cmd_feature line_feature; //Feature of the cmdline  
 pid_t pid; //PID of the latest child  
 //Other codes omitted  
 if (!builtin_cmd(argv[0])) {  
     if(line_feature.bg){  
         signal(SIGCHLD, SIG_IGN);  
     }  
     else{  
         signal(SIGCHLD, SIG_DFL);  
     }  
     if ((pid = fork()) == 0) {  
         for (int i = 0; i < line_feature.prog_num; i++) {  
             //Other codes omitted  
             if ((pid = fork()) == 0) {  
                 //Other codes omitted  
                 //Background command  
                 if (i == (line_feature.prog_num - 1) && line_feature.bg) {  
                     int fd = open("/dev/null", O_RDWR);  
                     close(STDIN_FILENO);  
                     dup(fd);  
                     close(STDOUT_FILENO);  
                     dup(fd);  
                 }  
                 //Other codes omitted  
             }  
             //Other codes omitted  
         }  
         //Other codes omitted  
     } else {  
         if (!line_feature.bg) {  
             waitfg(pid);  
         }  
     }  
 }