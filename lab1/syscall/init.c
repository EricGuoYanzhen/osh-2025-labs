#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>

#define SYS_HELLO 548 // 系统调用号

int main()
{
    printf("Hello, Eric Guo! PB230733\n");
    printf("Testing syscall...\n");
    // 测试缓冲区足够的情况
    printf("Testing buffer enough...\n");
    char buffer[50]; // 足够的缓冲区
    long result = syscall(SYS_HELLO, buffer, sizeof(buffer));
    if (result == 0)
    {
        printf("Success: %s", buffer);
    }
    else
    {
        printf("Unexpected Error: the buffer may be not enough.\n");
    }
    // 测试缓冲区不足的情况
    printf("Testing buffer not enough...\n");
    char bufferlow[10]; // 不足的缓冲区
    result = syscall(SYS_HELLO, bufferlow, sizeof(bufferlow));
    if (result == -1)
    {
        printf("The buffer is not enough, as expected.\n");
    }
    else
    {
        printf("Unexpected Error.\n");
    }
    while (1)
    {
    }
}