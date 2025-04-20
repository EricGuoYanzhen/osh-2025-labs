# Shell 编写

## Task0

贴了一下实例代码，会用 `make` 和 `make clean`

## Task1

- `pwd` 命令  
  直接调 `getcwd()` 输出就可以了

- `cd` 命令  
  用 `prev_dir` 记录一下上一次的目录，再调用 `chdir()` 即可  
  实现了 `cd` 在没有第二个参数时，默认进入家目录及 `cd -` 可以切换为上一次所在的目录的功能

**遗留问题：** 无法处理多空格，应对 `getline()` 和 `split()` 进行改进
