# 定义编译器和编译选项
CC = g++
CFLAGS = -O2
VERSION = -std=c++11

# 定义目标文件和源文件
TARGET = bubblesort
SRCS = main.cpp bubblesort.cpp bubblesort.hpp

# 生成可执行文件
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(VERSION) -o $@ $^

# 清理文件
# clean:
# 	rm -f $(TARGET)