# Makefile
# 
# author: GaleInk
# date: 2026/04/03 22:05

# https://twj-ink.github.io/os/makefile_tutorial/
# 对于项目中的基础配置、工具路径等等优先用:=防止意外发生
CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra
LDFLAGS := -lpthread

CPPS = main.cpp cmdline.cpp webserver.cpp \
	 epoller.cpp http_conn.cpp timer.cpp log.cpp

server: $(CPPS)
	$(CXX) $(CXXFLAGS) -o server $^ $(LDFLAGS)

clean:
	rm -f server