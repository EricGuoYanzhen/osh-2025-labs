// IO
#include <iostream>
// std::string
#include <string>
// std::vector
#include <vector>
// std::string 转 int
#include <sstream>
// PATH_MAX 等常量
#include <climits>
// POSIX API
#include <unistd.h>
// wait
#include <sys/wait.h>

std::vector<std::string> split(std::string s, const std::string &delimiter);

int main()
{
    // 不同步 iostream 和 cstdio 的 buffer
    std::ios::sync_with_stdio(false);

    // 用来存储读入的一行命令
    std::string cmd;
    while (true)
    {
        // // 显示当前工作目录(自用)，将家目录替换为 ~
        // char pwd[PATH_MAX];
        // if (getcwd(pwd, sizeof(pwd)))
        // {
        //     std::string cwd_str = pwd;
        //     const char *home = getenv("HOME");
        //     if (home && cwd_str.find(home) == 0)
        //     {
        //         std::cout << "~" << cwd_str.substr(std::string(home).length());
        //     }
        //     else
        //     {
        //         std::cout << cwd_str;
        //     }
        // }
        // else
        // {
        //     std::cout << "getcwd failed\n";
        // }

        // 打印提示符
        std::cout << "$ ";

        // 读入一行。std::getline 结果不包含换行符。
        std::getline(std::cin, cmd);

        // 检查是否有管道
        if (cmd.find("|") != std::string::npos)
        {
            // 先用 | 分割命令，每段再按空格分割
            std::vector<std::string> pipe_cmds = split(cmd, " | ");
            int n = pipe_cmds.size();
            std::vector<int[2]> pipes(n - 1);

            // 创建需要的管道
            for (int i = 0; i < n - 1; ++i)
            {
                if (pipe(pipes[i]) < 0)
                {
                    std::cout << "pipe failed\n";
                    goto wait_pipe_child;
                }
            }

            for (int i = 0; i < n; ++i)
            {
                std::vector<std::string> args = split(pipe_cmds[i], " ");
                if (args.empty())
                    continue;
                pid_t pid = fork();
                if (pid < 0)
                {
                    std::cout << "fork failed\n";
                    goto wait_pipe_child;
                }
                if (pid == 0)
                {
                    // 子进程
                    if (i > 0)
                    {
                        dup2(pipes[i - 1][0], 0); // 前一个管道的读端作为输入
                    }
                    if (i < n - 1)
                    {
                        dup2(pipes[i][1], 1); // 当前管道的写端作为输出
                    }
                    // 关闭所有管道
                    for (int j = 0; j < n - 1; ++j)
                    {
                        close(pipes[j][0]);
                        close(pipes[j][1]);
                    }
                    // 构造参数
                    std::vector<char *> argv;
                    for (auto &s : args)
                        argv.push_back(&s[0]);
                    argv.push_back(nullptr);
                    execvp(argv[0], argv.data());
                    std::cout << "exec failed\n";
                    exit(255);
                }
            }
            // 父进程关闭所有管道
            for (int i = 0; i < n - 1; ++i)
            {
                close(pipes[i][0]);
                close(pipes[i][1]);
            }
        wait_pipe_child:
            // 等待所有子进程
            for (int i = 0; i < n; ++i)
                wait(nullptr);
            continue;
        }

        // 按空格分割命令为单词
        std::vector<std::string> args = split(cmd, " ");

        // 没有可处理的命令
        if (args.empty())
        {
            continue;
        }

        // 退出
        if (args[0] == "exit")
        {
            if (args.size() <= 1)
            {
                return 0;
            }

            // std::string 转 int
            std::stringstream code_stream(args[1]);
            int code = 0;
            code_stream >> code;

            // 转换失败
            if (!code_stream.eof() || code_stream.fail())
            {
                std::cout << "Invalid exit code\n";
                continue;
            }

            return code;
        }

        if (args[0] == "pwd")
        {
            char buf[PATH_MAX];
            if (getcwd(buf, sizeof(buf)) != nullptr)
            {
                std::cout << buf << "\n";
            }
            else
            {
                std::cout << "pwd failed\n";
            }
            continue;
        }

        if (args[0] == "cd")
        {
            static std::string prev_dir; // 保存上一次目录
            char cwd[PATH_MAX];
            if (!getcwd(cwd, sizeof(cwd)))
            {
                std::cout << "getcwd failed\n";
                continue;
            }

            std::string target_dir;
            if (args.size() == 1)
            {
                // 没有参数，进入家目录
                char *home = getenv("HOME");
                if (!home)
                {
                    std::cout << "No HOME env\n";
                    continue;
                }
                target_dir = home;
            }
            else if (args[1] == "-")
            {
                // cd - 切换到上一次目录
                if (prev_dir.empty())
                {
                    std::cout << "No previous directory\n";
                    continue;
                }
                target_dir = prev_dir;
            }
            else
            {
                target_dir = args[1];
            }

            if (chdir(target_dir.c_str()) != 0) // 调用 chdir 切换目录
            {
                // 切换失败
                std::cout << "cd failed\n";
                continue;
            }

            prev_dir = cwd; // 更新上一次目录
            continue;
        }

        // 处理外部命令
        pid_t pid = fork();

        // std::vector<std::string> 转 char **
        char *arg_ptrs[args.size() + 1];
        for (auto i = 0; i < args.size(); i++)
        {
            arg_ptrs[i] = &args[i][0];
        }
        // exec p 系列的 argv 需要以 nullptr 结尾
        arg_ptrs[args.size()] = nullptr;

        if (pid == 0)
        {
            // 这里只有子进程才会进入
            // execvp 会完全更换子进程接下来的代码，所以正常情况下 execvp 之后这里的代码就没意义了
            // 如果 execvp 之后的代码被运行了，那就是 execvp 出问题了
            execvp(args[0].c_str(), arg_ptrs);

            // 所以这里直接报错
            exit(255);
        }

        // 这里只有父进程（原进程）才会进入
        int ret = wait(nullptr);
        if (ret < 0)
        {
            std::cout << "wait failed";
        }
    }
}

// 改进 split 函数，支持跳过前导空白
std::vector<std::string> split(std::string s, const std::string &delimiter)
{
    std::vector<std::string> res;
    size_t pos = 0;
    std::string token;
    // 跳过前导空白
    while (pos < s.size() && isspace(s[pos]))
    {
        ++pos;
    }

    while ((pos = s.find(delimiter)) != std::string::npos)
    {
        token = s.substr(0, pos);
        res.push_back(token);
        s = s.substr(pos + delimiter.length());
    }
    res.push_back(s);
    return res;
}