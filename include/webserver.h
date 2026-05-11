/* webserver.h
 * 
 * author: GaleInk
 * date: 2026/04/03 22:05
 */

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <string>

class WebServer 
{
public:
    WebServer();
    ~WebServer();

    void init(int port, std::string user, std::string password, 
              std::string databaseName, int log_write, int opt_linger,
              int trigmode, int sql_num, int thread_num, int close_log, 
              int actor_model);
};


#endif /*WEBSERVER_H*/