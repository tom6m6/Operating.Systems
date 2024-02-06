                    if((pid=fork())==0){
                        //主体代码区段
                        if(execvp(argv[i][0],argv[i])<0) {  
                            fprintf(stdout,"%s: Program not found.\n",argv[i][0]);  
                            exit(0);  
                        }
                    }else{
                        close(pipegate[i][1]);
                        int state;  
                        waitpid(pid,&state,0);  
                    }