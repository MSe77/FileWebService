#include "config.h"

int main()
{
    string user="root";
    string passwd="root";
    string databasename="yourdb";
    
    Config config;
    WebServer server;

    server.init(config.port,user,passwd,databasename,0,config.OptLinger,config.trigmode,config.SqlNum,config.ThreadNum,1,config.ActorModel);
    
    server.sql_pool();
    
    server.thread_pool();

    server.trig_mode();

    server.eventListen();

    server.eventLoop();

    return 0;

}