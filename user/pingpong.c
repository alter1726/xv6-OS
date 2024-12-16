#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc,char **argv){
    //创建两个管道：pfs用于父进程到子进程的通信  psf用于父进程到子进程的通信
    int pfs[2],psf[2];
    pipe(pfs);
    pipe(psf);

    if(fork()!=0){
        //父进程向子进程发送一个字符
        write(pfs[1],"q",1);
        close(pfs[1]);
        //父进程从子进程读取一个字符
        char buf;
        read(psf[0],&buf,1);
        printf("%d: receive pong\n",getpid());
        wait(0);
    }else
    {
        //子进程从父进程读取一个字符
        char buf;
        read(pfs[0],&buf,1);
        printf("%d: recieved ping\n",getpid());

        //子进程向父进程发送一个字符
        write(psf[1],&buf,1);
        close(psf[1]);
    }
    close(pfs[0]);
    close(psf[0]);
    
    exit(0);
}