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
    int LogWrite; // 0=同步 1=异步
    int CloseLog; // 0=开日志 1=关日志 
};

#endif /*CMDLINE_H*/