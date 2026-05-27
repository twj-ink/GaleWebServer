/* cmdline.cpp
 * 
 * author: GaleInk
 * date: 2026/05/21
 */

#include "include/cmdline.h"
#include <cstring>
#include <cstdlib>

Cmdline::Cmdline()
{
    Port = 8080;
    LogWrite = 0; // default 同步
    CloseLog = 0; // default 开日志

    
}

void Cmdline::parse_args(int argc, char* argv[])
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            Port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            LogWrite = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            CloseLog = atoi(argv[++i]);
        }
    }
}