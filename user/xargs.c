#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void run(char *, char **);

int main(int argc, char *argv[])
{
    char buf[2048];
    char *p = buf, *last_p = buf;
    char *argsbuf[128];
    char **args = argsbuf;

    for (int i = 1; i < argc; i++)
    {
        *args = argv[i]; // 首先将xargs的参数复制到argsbuf中
        args++;
    }
    char **pa = args; // 记录当前参数位置

    while (read(0, p, 1) != 0)
    {
        if (*p == ' ' || *p == '\n')
        {
            *p = '\0';
            *(pa++) = last_p; // 将参数添加到参数缓冲区argsbuf中
            last_p = p + 1;
            if (*p == '\n')
            {
                *pa = 0;
                run(argv[1], argsbuf);
                pa = args;
            }
        }
        p++; // 继续读取数据
    }

    if (pa != args)
    {
        *p = '\0';
        *(pa++) = last_p;
        *pa = 0;
        run(argv[1], argsbuf);
    }

    while (wait(0) != -1){}
    exit(0);
}

void run(char *prog, char **args)
{ // 运行指定程序，接收参数
    if (fork() == 0)
    { // 创建子进程，在子进程中执行指定程序
        exec(prog, args);
        exit(0);
    }
    return;
}