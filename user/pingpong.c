#include "kernel/types.h"
#include "user/user.h"
#include "kernel/stat.h"


int main(int argc, char *argv[]) { 
    char buf[10];
    int p_parent2child[2];
    int p_child2parent[2];
    int pid;

    pipe(p_parent2child);
    pipe(p_child2parent);

    pid = fork();

    if(pid < 0){
        fprintf(2, "fork error\n");
        exit(-1);
     }
    else if(pid == 0){
     //child
        int self_pid = getpid();
        close(p_child2parent[0]);
        close(p_parent2child[1]);
        read(p_parent2child[0], buf, 4);
        printf("%d: received %s ", self_pid, buf);
        read(p_parent2child[0], buf, 4);
        printf("from pid %s\n", buf);
        close(p_parent2child[0]);
        write(p_child2parent[1], "pong", 4);
        itoa(self_pid, buf);
        write(p_child2parent[1], buf, 4);
        close(p_child2parent[1]);
        exit(0);
    }
    else{
        //parent
        int self_pid = getpid();
        close(p_parent2child[0]);
        close(p_child2parent[1]);
        write(p_parent2child[1], "ping", 4);
        itoa(self_pid, buf);
        write(p_parent2child[1], buf, 4);
        close(p_parent2child[1]);
        read(p_child2parent[0], buf, 4);
        printf("%d: received %s ", self_pid, buf);
        read(p_child2parent[0], buf, 4);
        printf("from pid %s\n", buf);
        close(p_child2parent[0]);
        exit(0);
    }
    exit(0); // never reached
}