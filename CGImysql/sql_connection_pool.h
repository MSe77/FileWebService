#ifndef SQL_CONNECTION_POOL
#define SQL_CONNECTION_POOL

#include<stdio.h>
#include<string.h>
#include<list>
#include<errno.h>
#include<iostream>
#include "../lock/locker.h"
#include<mysql/mysql.h>

using namespace std;

class Connection_Pool
{
    public:
    MYSQL* GetConnection();
    bool ReleaseConnection(MYSQL* Conn);
    int GetFreeConnection();
    void DestryoPool();

    //单例模式
    static Connection_Pool* GetInstance();

    void Init(string Url,string User,string Password,string DataBaseName,int Port,int MaxConn,int CloseLog);
    
    private:
    Connection_Pool();
    ~Connection_Pool();

    int Max_Conn;
    int Cur_Conn;
    int Free_Conn;
    locker lock;
    list<MYSQL*> Conn_List;
    sem reserv;

    public:
    string C_Url;
    string C_User;
    string C_Password;
    string C_DataBase_Name;
    int C_Port;
    int C_Close_log;
};

//为了自动控制可用连接的数量
class ConnectionRAII
{
public:
ConnectionRAII(MYSQL** Conn,Connection_Pool* Pool);
~ConnectionRAII();
private:
MYSQL* Conn_RAII;
Connection_Pool* Pool_RAII;
};

#endif