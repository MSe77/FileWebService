#include "config.h"

Config::Config()
{
    port=24567;

    trigmode=0;

    listentrigmode=0;

    connTrigmode=0;

    OptLinger=0;

    SqlNum=8;

    ThreadNum=8;

    ActorModel=0;
}

//
void Config::parse_arg(int argc,char* argv[])
{

}