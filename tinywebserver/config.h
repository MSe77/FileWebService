#ifndef CONFIG_H
#define CONFIG_H


#include"webserver.h"
using namespace std;

class Config
{
public:
Config();
~Config(){};

void parse_arg(int argc,char* argv[]);

int port;
int trigmode;
int listentrigmode;
int connTrigmode;
int OptLinger;
int SqlNum;
int ThreadNum;
int ActorModel;
};
#endif