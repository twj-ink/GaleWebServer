/* cmdline.h
 * 
 * author: GaleInk
 * date: 2026/05/11 21:18
 */

#ifndef CMDLINE_H
#define CMDLINE_H

class Cmdline
{
public:
    Cmdline();
    ~Cmdline(){};

    void parse_args(int argc, char* argv[]);

    int Port; // 端口号
    int LogWrite; // 日志写入方式
    int TrigMode; // 触发组合模式
    int ListenTrigMode; // listenfd触发模式
};

#endif /*CMDLINE_H*/