#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char* fmtname(char *path){
    static char buf[DIRSIZ+1];
    char *p;

    // 找最后一节的起始位置
    for(p=path+strlen(path); p >= path && *p != '/'; p--);

    p++;

    if(strlen(p) >= DIRSIZ)
        return p;
    memmove(buf, p, strlen(p));
    
    buf[strlen(p)] = '\0';
    return buf;
}

void find(char* path, char* file_name){
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(path, 0)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    if(st.type == T_FILE){
        if(strcmp(fmtname(path), file_name) == 0){
            printf("%s\n", path);
        }
    }
    else if(st.type == T_DIR){
        if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
            printf("ls: path too long\n");
            return;
        }
        if(strcmp(fmtname(path), file_name) == 0){
            printf("%s\n", path);
        }
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';
        while(read(fd, &de, sizeof(de)) == sizeof(de)){
            if(de.inum == 0 || strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                continue;
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            find(buf, file_name);
        }
    }
    close(fd);
}

int main(int argc, char* argv[]){
    if(argc < 3){
        fprintf(2, "usage find <path> <file_name>");
        exit(0);
    }
    find(argv[1], argv[2]);
    exit(0);
}
