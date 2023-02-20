#include "sql_connection_pool.h"
#include<mysql/mysql.h>
#include<stdio.h>
#include<string>
#include<stdlib.h>
#include<list>
#include<pthread.h>
#include<iostream>


using namespace std;

Connection_Pool::Connection_Pool()  //构造函数（私有）
{
    Max_Conn=0;
    Cur_Conn=0;
}

Connection_Pool* Connection_Pool::GetInstance()  //单例模式返回唯一对象
{
         static Connection_Pool conn_pool;
         return &conn_pool;
} 

void Connection_Pool::Init(string Url,string User,string Password,string DataBaseName,int Port,int MaxConn,int CloseLog)
{
   C_Url=Url;
   C_User=User;
   C_Password=Password;
   C_DataBase_Name=DataBaseName;
   C_Port=Port;
   //C_Close_log=CloseLog;

   for(int i=0;i<MaxConn;++i)
   {
      MYSQL* conn=NULL;
      conn=mysql_init(conn);

    //   if(conn==NULL)
    //   {
    //     LOG_ERROR("Mysql Error");
    //     exit(1);
    //   }

     conn=mysql_real_connect(conn,Url.c_str(),User.c_str(),Password.c_str(),DataBaseName.c_str(),Port,NULL,0); //如果连接成功，返回MYSQL*句柄，失败返回NULL。对于成功的连接，返回值与第一个参数相同。
     
    //   if(conn==NULL)
    //   {
    //     LOG_ERROR("Mysql Error");
    //     exit(1);
    //   }

    Conn_List.push_back(conn);   //添加到连表队列
    ++Free_Conn;
   }
   
   reserv=sem(Free_Conn);  //资源量信号量
   Max_Conn=Free_Conn;
}


//当有请求时，从数据库连接池中返回一个可用的连接，并且更新可用的连接数量
MYSQL* Connection_Pool::GetConnection()
{
   MYSQL* conn=NULL;

   if(Conn_List.size()==0) 
   return NULL;

   reserv.wait();  //等待可用资源大于0
   lock.lock() ;
    
   conn=Conn_List.front();
   Conn_List.pop_back();
   --Free_Conn;
   ++Cur_Conn;

   lock.unlock();

   return conn;
}

//释放数据库连接，更新信号量可可用连接量
bool Connection_Pool::ReleaseConnection(MYSQL* conn)
{
    if(conn=NULL)
    return false;

    lock.lock();

    Conn_List.push_back(conn);
    ++Free_Conn;
    --Cur_Conn;

    lock.unlock();

    reserv.post();
    return true;
}

//销毁数据库连接池
void Connection_Pool::DestryoPool()
{
    lock.lock();

    if(Conn_List.size()>0)
    {
        for(auto i=Conn_List.begin();i!=Conn_List.end();++i)
        {
            MYSQL* conn=*i;
            mysql_close(conn);
        }
    }

    Free_Conn=0;
    Cur_Conn=0;
}

int Connection_Pool::GetFreeConnection()
{
    return this->Free_Conn;
}

Connection_Pool::~Connection_Pool()
{
    DestryoPool();
}
//获得一个SQL连接
ConnectionRAII::ConnectionRAII(MYSQL** Conn,Connection_Pool* Pool)
{
  *Conn=Pool->GetConnection();

  Conn_RAII=*Conn;
  Pool_RAII=Pool;

}
//释放连接，但是不销毁连接池
ConnectionRAII::~ConnectionRAII()
{
    Pool_RAII->ReleaseConnection(Conn_RAII);
}


