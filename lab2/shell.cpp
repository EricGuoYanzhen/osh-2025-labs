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
// open/close
#include <fcntl.h>
// signal
#include <signal.h>
#include <termios.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdio>
// time
#include <ctime>
#include <iomanip>
// 用户信息
#include <pwd.h>
// 错误处理
#include <errno.h>
#include <string.h>

// 全局变量，保存 shell 的 pgid 和终端 fd
pid_t shell_pgid;
int shell_terminal;
struct termios shell_tmodes;
volatile sig_atomic_t sigint_received = 0;
pid_t foreground_pgid = 0; // 存储当前前台进程组ID

// 函数声明
std::vector<std::string> split(const std::string &s, bool respect_quotes = false);
std::vector<std::string> split_by_pipe(const std::string &s);
bool handle_redirection(std::vector<std::string> &args);
bool execute_pipeline(const std::string &cmd);
bool handle_builtin_command(const std::vector<std::string> &args);
bool execute_command(const std::vector<std::string> &args, bool is_time = false);
bool builtin_cd(const std::vector<std::string> &args);
bool handle_history_command(const std::string &cmd, std::vector<std::string> &history);

// 信号处理函数
void sigint_handler(int signo)
{
    sigint_received = 1;

    // 如果有前台进程组，向其发送信号
    if (foreground_pgid > 0)
    {
        kill(-foreground_pgid, SIGINT);
    }
    else
    {
        // 否则输出提示符
        write(STDOUT_FILENO, "\n$ ", 3);
    }
}

int main()
{
    // 不同步 iostream 和 cstdio 的 buffer
    std::ios::sync_with_stdio(false);

    // 命令历史
    std::vector<std::string> history;

    // 初始化 shell 进程组和终端
    shell_terminal = STDIN_FILENO;
    shell_pgid = getpid();
    // 让 shell 成为自己的进程组长
    setpgid(shell_pgid, shell_pgid);
    // 把 shell 进程组设置为前台
    tcsetpgrp(shell_terminal, shell_pgid);
    // 保存终端属性
    tcgetattr(shell_terminal, &shell_tmodes);

    // 安装 SIGINT 处理器
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, nullptr);

    // 忽略 SIGTTOU，防止 tcsetpgrp 时被挂起
    signal(SIGTTOU, SIG_IGN);

    // 用来存储读入的一行命令
    std::string cmd;
    while (true)
    {
        // 检查 sigint_received，若收到 Ctrl-C，丢弃当前输入
        if (sigint_received)
        {
            sigint_received = 0;
            continue;
        }

        // 打印提示符
        std::cout << "$ " << std::flush;

        // 读入一行。std::getline 结果不包含换行符。
        std::getline(std::cin, cmd);

        // 检查 EOF（如 Ctrl-D），退出 shell
        if (std::cin.eof())
        {
            std::cout << "\n";
            break;
        }

        // 若输入被 Ctrl-C 打断，std::getline 会设置 failbit
        if (std::cin.fail())
        {
            std::cin.clear();
            continue;
        }

        // 去除前后空白
        size_t first = cmd.find_first_not_of(" \t\r\n");
        size_t last = cmd.find_last_not_of(" \t\r\n");
        std::string trimmed_cmd = (first == std::string::npos) ? "" : cmd.substr(first, last - first + 1);

        if (trimmed_cmd.empty())
            continue;

        // 处理历史命令
        if (handle_history_command(trimmed_cmd, history))
        {
            continue;
        }

        // 非空命令加入历史
        history.push_back(trimmed_cmd);

        // 检查是否有管道
        if (trimmed_cmd.find("|") != std::string::npos)
        {
            execute_pipeline(trimmed_cmd);
            continue;
        }

        // 解析命令参数
        std::vector<std::string> args = split(trimmed_cmd, true);
        if (args.empty())
            continue;

        // 处理内建命令
        if (handle_builtin_command(args))
        {
            continue;
        }

        // 检查 time 命令
        bool is_time = false;
        if (args[0] == "time")
        {
            is_time = true;
            args.erase(args.begin());
            if (args.empty())
            {
                std::cout << "time: missing command" << std::endl;
                continue;
            }
        }

        // 执行外部命令
        execute_command(args, is_time);
    }

    return 0;
}

// 处理历史命令 (!!, !n, history)
bool handle_history_command(const std::string &cmd, std::vector<std::string> &history)
{
    // 处理 !! 和 !n
    if (cmd == "!!")
    {
        if (history.empty())
        {
            std::cout << "No commands in history" << std::endl;
            return true;
        }
        std::string bang_cmd = history.back();
        std::cout << bang_cmd << std::endl;

        // 递归处理历史命令
        std::string trimmed_cmd = bang_cmd;
        size_t first = trimmed_cmd.find_first_not_of(" \t\r\n");
        size_t last = trimmed_cmd.find_last_not_of(" \t\r\n");
        trimmed_cmd = (first == std::string::npos) ? "" : trimmed_cmd.substr(first, last - first + 1);

        if (!trimmed_cmd.empty())
        {
            // 检查是否是递归的历史命令，防止无限循环
            if (trimmed_cmd != "!!" && trimmed_cmd.substr(0, 1) != "!")
            {
                history.push_back(trimmed_cmd);

                // 处理命令
                if (trimmed_cmd.find("|") != std::string::npos)
                {
                    execute_pipeline(trimmed_cmd);
                }
                else
                {
                    std::vector<std::string> args = split(trimmed_cmd, true);
                    if (!args.empty())
                    {
                        if (!handle_builtin_command(args))
                        {
                            bool is_time = false;
                            if (args[0] == "time")
                            {
                                is_time = true;
                                args.erase(args.begin());
                                if (!args.empty())
                                {
                                    execute_command(args, is_time);
                                }
                            }
                            else
                            {
                                execute_command(args, false);
                            }
                        }
                    }
                }
            }
        }
        return true;
    }
    else if (cmd.size() > 1 && cmd[0] == '!' && isdigit(cmd[1]))
    {
        int idx = std::stoi(cmd.substr(1));
        if (idx < 1 || idx > (int)history.size())
        {
            std::cout << "No such command in history" << std::endl;
            return true;
        }
        std::string bang_cmd = history[idx - 1];
        std::cout << bang_cmd << std::endl;

        // 递归处理历史命令
        std::string trimmed_cmd = bang_cmd;
        size_t first = trimmed_cmd.find_first_not_of(" \t\r\n");
        size_t last = trimmed_cmd.find_last_not_of(" \t\r\n");
        trimmed_cmd = (first == std::string::npos) ? "" : trimmed_cmd.substr(first, last - first + 1);

        if (!trimmed_cmd.empty())
        {
            // 检查是否是递归的历史命令，防止无限循环
            if (trimmed_cmd != "!!" && trimmed_cmd.substr(0, 1) != "!")
            {
                history.push_back(trimmed_cmd);

                // 处理命令
                if (trimmed_cmd.find("|") != std::string::npos)
                {
                    execute_pipeline(trimmed_cmd);
                }
                else
                {
                    std::vector<std::string> args = split(trimmed_cmd, true);
                    if (!args.empty())
                    {
                        if (!handle_builtin_command(args))
                        {
                            bool is_time = false;
                            if (args[0] == "time")
                            {
                                is_time = true;
                                args.erase(args.begin());
                                if (!args.empty())
                                {
                                    execute_command(args, is_time);
                                }
                            }
                            else
                            {
                                execute_command(args, false);
                            }
                        }
                    }
                }
            }
        }
        return true;
    }

    // history n 命令
    if (cmd.substr(0, 7) == "history")
    {
        int n = history.size();
        // 解析 history n
        std::istringstream iss(cmd);
        std::string tmp;
        iss >> tmp; // 跳过 history
        if (iss >> n)
        {
            if (n > (int)history.size())
                n = history.size();
        }
        // 输出格式与 bash 类似
        int start = history.size() - n;
        if (start < 0)
            start = 0;
        for (int i = start; i < (int)history.size(); ++i)
        {
            std::cout << "  " << i + 1 << "  " << history[i] << std::endl;
        }
        return true;
    }

    return false;
}

// 处理内建命令
bool handle_builtin_command(const std::vector<std::string> &args)
{
    if (args.empty())
        return false;

    // exit 命令
    if (args[0] == "exit")
    {
        if (args.size() <= 1)
        {
            exit(0);
        }

        try
        {
            int code = std::stoi(args[1]);
            exit(code);
        }
        catch (const std::exception &e)
        {
            std::cerr << "exit: invalid exit code: " << args[1] << std::endl;
        }
        return true;
    }

    // cd 命令
    if (args[0] == "cd")
    {
        builtin_cd(args);
        return true;
    }

    // pwd 命令
    if (args[0] == "pwd")
    {
        char buf[PATH_MAX];
        if (getcwd(buf, sizeof(buf)) != nullptr)
        {
            std::cout << buf << std::endl;
        }
        else
        {
            std::cerr << "pwd: " << strerror(errno) << std::endl;
        }
        return true;
    }

    return false; // 不是内建命令
}

// 增强的 cd 命令支持
bool builtin_cd(const std::vector<std::string> &args)
{
    static std::string prev_dir; // 保存上一次目录
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd)))
    {
        std::cerr << "getcwd: " << strerror(errno) << std::endl;
        return false;
    }

    std::string target_dir;
    if (args.size() == 1)
    {
        // 没有参数，进入家目录
        char *home = getenv("HOME");
        if (!home)
        {
            std::cerr << "cd: HOME environment variable not set" << std::endl;
            return false;
        }
        target_dir = home;
    }
    else if (args[1] == "-")
    {
        // cd - 切换到上一次目录
        if (prev_dir.empty())
        {
            std::cerr << "cd: OLDPWD not set" << std::endl;
            return false;
        }
        target_dir = prev_dir;
        std::cout << target_dir << std::endl; // cd - 会打印目标目录
    }
    else if (args[1].size() > 0 && args[1][0] == '~')
    {
        // 处理 ~ 和 ~user
        if (args[1].size() == 1)
        {
            // 仅 ~
            char *home = getenv("HOME");
            if (!home)
            {
                std::cerr << "cd: HOME environment variable not set" << std::endl;
                return false;
            }
            target_dir = home;
        }
        else
        {
            // ~user
            struct passwd *pw = getpwnam(args[1].substr(1).c_str());
            if (!pw)
            {
                std::cerr << "cd: user " << args[1].substr(1) << " not found" << std::endl;
                return false;
            }
            target_dir = pw->pw_dir;
        }
    }
    else
    {
        target_dir = args[1];
    }

    if (chdir(target_dir.c_str()) != 0)
    {
        std::cerr << "cd: " << target_dir << ": " << strerror(errno) << std::endl;
        return false;
    }

    prev_dir = cwd; // 更新上一次目录
    return true;
}

// 执行外部命令
bool execute_command(const std::vector<std::string> &args, bool is_time)
{
    struct timespec ts_start, ts_end;
    if (is_time)
    {
        clock_gettime(CLOCK_MONOTONIC, &ts_start);
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork");
        return false;
    }

    if (pid == 0)
    {
        // 子进程
        setpgid(0, 0);
        tcsetpgrp(shell_terminal, getpid());

        // 恢复默认信号处理
        signal(SIGINT, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);

        // 处理重定向
        std::vector<std::string> cmd_args = args;
        if (!handle_redirection(cmd_args))
        {
            exit(1);
        }

        // 转换参数格式
        char *arg_ptrs[cmd_args.size() + 1];
        for (size_t i = 0; i < cmd_args.size(); i++)
        {
            arg_ptrs[i] = &cmd_args[i][0];
        }
        arg_ptrs[cmd_args.size()] = nullptr;

        execvp(arg_ptrs[0], arg_ptrs);
        std::cerr << arg_ptrs[0] << ": " << strerror(errno) << std::endl;
        exit(255);
    }

    // 父进程
    foreground_pgid = pid; // 记录前台进程组
    setpgid(pid, pid);
    tcsetpgrp(shell_terminal, pid);

    int status = 0;
    waitpid(pid, &status, 0);
    foreground_pgid = 0; // 重置前台进程组

    // 恢复shell为前台
    tcsetpgrp(shell_terminal, shell_pgid);

    if (is_time)
    {
        clock_gettime(CLOCK_MONOTONIC, &ts_end);
        double elapsed = (ts_end.tv_sec - ts_start.tv_sec) + (ts_end.tv_nsec - ts_start.tv_nsec) / 1e9;
        std::cout << args[0] << "    " << std::fixed << std::setprecision(2) << elapsed << "s" << std::endl;
    }

    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// 执行管道命令
bool execute_pipeline(const std::string &cmd)
{
    // 使用增强的管道分割函数
    std::vector<std::string> pipe_cmds = split_by_pipe(cmd);
    int n = pipe_cmds.size();

    // 检查管道命令是否为空
    for (int i = 0; i < n; ++i)
    {
        if (pipe_cmds[i].empty())
        {
            std::cerr << "Syntax error: empty command in pipeline" << std::endl;
            return false;
        }
    }

    // 检查是否是后台命令
    bool is_background = false;
    if (pipe_cmds.back().back() == '&')
    {
        is_background = true;
        // 移除 &
        pipe_cmds.back().pop_back();
        // 重新整理空格
        size_t last = pipe_cmds.back().find_last_not_of(" \t\r\n");
        if (last != std::string::npos)
        {
            pipe_cmds.back() = pipe_cmds.back().substr(0, last + 1);
        }
    }

    int pipes[n - 1][2]; // 创建管道数组
    std::vector<pid_t> pids(n);

    // 创建需要的管道
    for (int i = 0; i < n - 1; ++i)
    {
        if (pipe(pipes[i]) < 0)
        {
            perror("pipe");
            // 关闭已创建的管道
            for (int j = 0; j < i; ++j)
            {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return false;
        }
    }

    // 创建子进程
    for (int i = 0; i < n; ++i)
    {
        std::vector<std::string> args = split(pipe_cmds[i], true);
        if (args.empty())
        {
            continue; // 理论上前面已经检查过，这里再做一次保险
        }

        pids[i] = fork();
        if (pids[i] < 0)
        {
            perror("fork");
            // 关闭所有管道
            for (int j = 0; j < n - 1; ++j)
            {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            // 杀死已创建的子进程
            for (int j = 0; j < i; ++j)
            {
                kill(pids[j], SIGTERM);
            }
            return false;
        }

        if (pids[i] == 0)
        {
            // 子进程
            // 设置进程组
            if (i == 0)
            {
                // 第一个进程作为进程组长
                setpgid(0, 0);
                // 如果不是后台命令，将进程组设为前台
                if (!is_background)
                {
                    tcsetpgrp(shell_terminal, getpid());
                }
            }
            else
            {
                // 其他进程加入第一个进程的组
                setpgid(0, pids[0]);
            }

            // 重置信号处理为默认
            signal(SIGINT, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            // 设置重定向
            if (i > 0)
            {
                dup2(pipes[i - 1][0], STDIN_FILENO); // 读取上一个命令的输出
            }
            if (i < n - 1)
            {
                dup2(pipes[i][1], STDOUT_FILENO); // 输出到下一个命令
            }

            // 关闭所有管道文件描述符
            for (int j = 0; j < n - 1; ++j)
            {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            // 处理重定向
            if (!handle_redirection(args))
            {
                exit(1);
            }

            // 转换为exec参数格式并执行
            char *arg_ptrs[args.size() + 1];
            for (size_t j = 0; j < args.size(); j++)
            {
                arg_ptrs[j] = &args[j][0];
            }
            arg_ptrs[args.size()] = nullptr;

            execvp(args[0].c_str(), arg_ptrs);
            std::cerr << args[0] << ": " << strerror(errno) << std::endl;
            exit(255);
        }
    }

    // 父进程
    // 关闭所有管道
    for (int i = 0; i < n - 1; ++i)
    {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // 确保所有进程在相同进程组
    for (int i = 0; i < n; ++i)
    {
        setpgid(pids[i], pids[0]);
    }

    // 如果不是后台命令，将进程组设置为前台并等待
    if (!is_background)
    {
        foreground_pgid = pids[0];
        tcsetpgrp(shell_terminal, pids[0]);

        // 等待所有子进程结束
        for (int i = 0; i < n; ++i)
        {
            int status;
            waitpid(pids[i], &status, 0);

            // 可以在这里记录或输出退出状态
            if (i == n - 1 && WIFEXITED(status))
            {
                int exit_code = WEXITSTATUS(status);
                if (exit_code != 0)
                {
                    std::cerr << "Command exited with status " << exit_code << std::endl;
                }
            }
        }

        foreground_pgid = 0;
        // 恢复shell为前台
        tcsetpgrp(shell_terminal, shell_pgid);
    }
    else
    {
        // 后台进程，只打印进程组ID
        std::cout << "[" << pids[0] << "] " << pipe_cmds[0] << " &" << std::endl;
    }

    return true;
}

// 支持引号和转义符的 split 函数
std::vector<std::string> split(const std::string &s, bool respect_quotes)
{
    std::vector<std::string> tokens;
    std::string current_token;
    bool in_quotes = false;
    char quote_char = '\0';
    bool escaped = false;

    for (size_t i = 0; i < s.length(); ++i)
    {
        char c = s[i];

        if (escaped)
        {
            // 处理转义字符
            current_token += c;
            escaped = false;
            continue;
        }

        if (c == '\\' && respect_quotes)
        {
            escaped = true;
            continue;
        }

        if (respect_quotes && (c == '"' || c == '\''))
        {
            if (!in_quotes)
            {
                in_quotes = true;
                quote_char = c;
            }
            else if (c == quote_char)
            {
                in_quotes = false;
            }
            else
            {
                current_token += c;
            }
            continue;
        }

        if (isspace(c) && !in_quotes)
        {
            if (!current_token.empty())
            {
                tokens.push_back(current_token);
                current_token.clear();
            }
        }
        else
        {
            current_token += c;
        }
    }

    if (!current_token.empty())
    {
        tokens.push_back(current_token);
    }

    return tokens;
}

// 专用于分割管道
std::vector<std::string> split_by_pipe(const std::string &s)
{
    std::vector<std::string> result;
    std::string current;
    bool in_quotes = false;
    char quote_char = '\0';
    bool escaped = false;

    for (size_t i = 0; i < s.length(); ++i)
    {
        char c = s[i];

        if (escaped)
        {
            current += c;
            escaped = false;
            continue;
        }

        if (c == '\\')
        {
            escaped = true;
            continue;
        }

        if (c == '"' || c == '\'')
        {
            if (!in_quotes)
            {
                in_quotes = true;
                quote_char = c;
            }
            else if (c == quote_char)
            {
                in_quotes = false;
            }
            current += c;
            continue;
        }

        if (c == '|' && !in_quotes)
        {
            // 找到管道符号，且不在引号内
            result.push_back(current);
            current.clear();
        }
        else
        {
            current += c;
        }
    }

    if (!current.empty() || result.empty())
    {
        result.push_back(current);
    }

    // 去除每个命令首尾空格
    for (auto &cmd : result)
    {
        size_t start = cmd.find_first_not_of(" \t\r\n");
        size_t end = cmd.find_last_not_of(" \t\r\n");

        if (start == std::string::npos)
        {
            cmd = "";
        }
        else
        {
            cmd = cmd.substr(start, end - start + 1);
        }
    }

    return result;
}

// 增强的重定向处理函数
bool handle_redirection(std::vector<std::string> &args)
{
    for (size_t i = 0; i < args.size();)
    {
        std::string &arg = args[i];
        std::string file;
        int fd_src = -1;
        int fd_dest = -1;
        bool is_redirect = false;

        // 处理形如"2>"的情况
        if (arg.size() >= 2 && isdigit(arg[0]) && arg[1] == '>')
        {
            fd_src = arg[0] - '0';
            if (arg.size() == 2)
            {
                // 2> file
                if (i + 1 >= args.size())
                {
                    std::cerr << "Syntax error: missing file for redirection" << std::endl;
                    return false;
                }
                file = args[i + 1];
                args.erase(args.begin() + i, args.begin() + i + 2);
            }
            else
            {
                // 2>file
                file = arg.substr(2);
                args.erase(args.begin() + i);
            }

            fd_dest = open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (fd_dest < 0)
            {
                perror(("open " + file).c_str());
                return false;
            }

            if (dup2(fd_dest, fd_src) < 0)
            {
                perror("dup2");
                close(fd_dest);
                return false;
            }
            close(fd_dest);
            is_redirect = true;
        }
        // 处理 2>&1 等情况
        else if (arg.size() >= 3 && isdigit(arg[0]) && arg[1] == '>' && arg[2] == '&' && arg.size() >= 4 && isdigit(arg[3]))
        {
            fd_src = arg[0] - '0';
            int target_fd = arg[3] - '0';

            if (dup2(target_fd, fd_src) < 0)
            {
                perror("dup2");
                return false;
            }

            args.erase(args.begin() + i);
            is_redirect = true;
        }
        // 标准重定向 >, >>, <
        else if (arg == ">" || arg == ">>" || arg == "<")
        {
            if (i + 1 >= args.size())
            {
                std::cerr << "Syntax error: missing file for redirection" << std::endl;
                return false;
            }
            file = args[i + 1];

            if (arg == ">")
            {
                fd_dest = open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
                fd_src = STDOUT_FILENO;
            }
            else if (arg == ">>")
            {
                fd_dest = open(file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0666);
                fd_src = STDOUT_FILENO;
            }
            else
            { // <
                fd_dest = open(file.c_str(), O_RDONLY);
                fd_src = STDIN_FILENO;
            }

            if (fd_dest < 0)
            {
                perror(("open " + file).c_str());
                return false;
            }

            if (dup2(fd_dest, fd_src) < 0)
            {
                perror("dup2");
                close(fd_dest);
                return false;
            }

            close(fd_dest);
            args.erase(args.begin() + i, args.begin() + i + 2);
            is_redirect = true;
        }
        // 处理无空格的 >file, >>file, <file
        else if (arg.size() > 1 && (arg[0] == '>' || arg[0] == '<'))
        {
            if (arg[0] == '>' && arg.size() > 1 && arg[1] == '>')
            {
                // >>file
                file = arg.substr(2);
                fd_dest = open(file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0666);
                fd_src = STDOUT_FILENO;
            }
            else if (arg[0] == '>')
            {
                // >file
                file = arg.substr(1);
                fd_dest = open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
                fd_src = STDOUT_FILENO;
            }
            else if (arg[0] == '<')
            {
                // <file
                file = arg.substr(1);
                fd_dest = open(file.c_str(), O_RDONLY);
                fd_src = STDIN_FILENO;
            }

            if (fd_dest < 0)
            {
                perror(("open " + file).c_str());
                return false;
            }

            if (dup2(fd_dest, fd_src) < 0)
            {
                perror("dup2");
                close(fd_dest);
                return false;
            }

            close(fd_dest);
            args.erase(args.begin() + i);
            is_redirect = true;
        }

        if (!is_redirect)
        {
            ++i;
        }
    }
    return true;
}
