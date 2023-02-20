#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include<unistd.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<assert.h>
#include<sys/stat.h>
#include<string.h>
#include<pthread.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/mman.h>
#include<stdarg.h>
#include<errno.h>
#include<sys/wait.h>
#include<sys/uio.h>
#include<map>
#include<fstream>
#include<vector>
#include"dirent.h"

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include  "../log/log.h"


class http_conn
{
public:
static const int FILENAME_LEN=200;
static const int READ_BUFFER_SIZE =2048;
static const int WRITE_BUFFER_SIZE =2048;

enum METHOD
{
    GET=0,
    POST,
    HEAD,
    PUT,
    DELETE,
    TRACE,
    OPTIONS,
    CONNECT,
    PATH
};

enum CHECK_STATE
{
    CHECK_STATE_REQUESTLINE=0,
    CHECK_STATE_HEADER,
    CHECK_STATE_CONNTENT,
    FILE_UPLOAD
};

enum HTTP_CODE
{
    NO_REQUEST,
    GET_REQUEST,
    BAD_REQUEST,
    NO_RESOURCE,
    FORBIDDEN_REQUEST,
    FILE_REQUEST,
    INTERNAL_ERROR,
    CLOSED_CONNECTION,
};
enum LINE_STATUS
{
    LINE_OK=0,
    LINE_BAD,
    LINE_OPEN
};

public:
http_conn(){};
~http_conn(){};

public:
void init(int sockfd,const sockaddr_in&addr,char* ,int ,int ,string user,string passwd,string sqlname);
void close_conn(bool real_conn=true);
void process();
bool read_once();
bool write();
bool check_mydir(string path);
sockaddr_in* get_address()
{
    return &m_address;
}
void initmysql_result(Connection_Pool* connPool);
int timer_flag;
int improv;


private:
void init();
HTTP_CODE process_read();
bool process_write(HTTP_CODE ret);
HTTP_CODE parse_request_line(char* text);
HTTP_CODE parse_headers(char* text);
HTTP_CODE parse_content(char* text);
HTTP_CODE upload_file(); //解析上传文件内容
HTTP_CODE do_request();
char* get_line(){return m_read_buffer+m_start_line;};
LINE_STATUS parse_line();
void umap();
bool add_response(const char*format,...);
bool add_content(const char* content);
bool add_status_line(int status,const char* title);
bool add_headers(int content_length);
bool add_contnet_type();
bool add_content_length(int content_length);
bool add_linger();
bool add_blank_line();
void get_myfiles();


public:
static int m_epollfd;    //静态变量，所有对象共享，标志了epollfd和用户数量
static int m_user_count;
MYSQL* mysql;
int m_state;

private:
int m_sockfd;
sockaddr_in m_address;
char m_read_buffer[READ_BUFFER_SIZE];
int m_read_idx;
int m_checked_idx;
int m_start_line;
char m_wirte_buffer[WRITE_BUFFER_SIZE];
int m_write_idx;
CHECK_STATE m_check_state;
METHOD m_method;
char m_real_file[FILENAME_LEN];
char* m_url;
char* m_version;
char* m_host;
int m_content_length;
bool m_linger;
void* m_file_address;
struct stat m_file_stat;
struct iovec m_iv[2];//用于分散读或者写的数据结构  用于将需要返回的http信息和需要发送的视频文件一起发送
int m_iv_count;
int cgi;
char* m_string;
int bytes_to_send;
int bytes_have_send;
char* doc_root;

map<string,string> m_users;
int m_TRIGMode;
int m_close_log;

string m_name;      //用户名字
string m_file_addr; //用户文件的保存地址
string m_boundary;  //文件分割符
string m_filename;  //用户上传文件时文件名字
ofstream ofs; 
string m_filestring; //文件传输流字符串
int m_isupload=0;    //是否在上传文件
bool m_download=false; //是否在下载文件
vector<string> m_files;//用户文件

char sql_user[100];
char sql_passwd[100];
char sql_name[100];
};



#endif