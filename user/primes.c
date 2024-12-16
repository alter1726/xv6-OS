#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void sieve(int pleft[2]);

int main(int argc, char **argv)
{
    int ipp[2];
    pipe(ipp);

    if (fork() == 0) // 右邻居--子进程
    {
        close(ipp[1]);
        sieve(ipp);
        exit(0);
    }
    else // 父进程
    {
        close(ipp[0]); // 父进程只会向管道中给右邻居写数据，关闭父进程的管道读文件描述符
        int i;
        for (i = 2; i <= 35; i++)
        {
            write(ipp[1], &i, sizeof(i)); // 向管道中写入2～35的整数
        }
        i = -1; // 写入结束
        write(ipp[1], &i, sizeof(i));
    }
    wait(0); // 等子进程结束
    exit(0); // 退出进程
}

void sieve(int pleft[2])
{
    int p;
    read(pleft[0], &p, sizeof(p));
    if (p == -1)
    {
        exit(-1);
    }
    printf("prime %d\n", p);

    int pright[2]; //创建新管道
    pipe(pright);

    if (fork() == 0)  //右邻居
    {
        close(pright[1]);//右邻居用不到该管道的写端，关闭
        close(pleft[0]);//右邻居用不到该管道的读端，关闭
        sieve(pright);//递归调用筛选函数
    }
    else
    {
        close(pright[0]);//当前进程用不到该管道的读端，关闭
        int buf;
        while (read(pleft[0], &buf, sizeof(buf)) && buf != -1)
        {
            if (buf % p != 0)
            {
                //如果接收到的数字不是第一次接受到数字的倍数,才往管道中给右邻居写入该数字
                write(pright[1], &buf, sizeof(buf));
            }
        }

        buf = -1;//接收到了左邻居传来的-1,要给右邻居也传-1,结束右邻居进程
        write(pright[1], &buf, sizeof(buf));
        wait(0);
        exit(0);
    }
}