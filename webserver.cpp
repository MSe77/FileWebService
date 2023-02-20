#include"webserver.h"
WebServer::WebServer()
{
    //http_conn对象
    users=new http_conn[MAX_FD];

    //root文件夹路径
    char server_path[200];
    getcwd(server_path,200); //将当前工作目录的绝对路径复制到buffer中
    char root[6]="/root";
    m_root=(char*)malloc(strlen(server_path)+strlen(root)+1);
    strcpy(m_root,server_path);
    strcat(m_root,root);

    //定时器
    users_timer=new client_data[MAX_FD];
}



WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[0]);
    close(m_pipefd[1]);

    delete[] users;
    delete[] users_timer;
    delete m_pool;
}


void  WebServer::init(int port,string user,string passWord,string databaseName,
          int log_write,int opt_linger,int trigmode,int sql_num,
          int thread_num,int close_log,int actor_model)
{
m_port=port;
m_user=user;
m_passWord=passWord;
m_databaseName=databaseName;
m_log_wirte=log_write;
m_OPT_LINGER=opt_linger;
m_TRIGMode=trigmode;
m_sql_num=sql_num;
m_thread_num=thread_num;
m_close_log=close_log;
m_actormodel=actor_model;
}

void WebServer::trig_mode()
{   
    //LT :当检测到epoll_wait 上有就绪事件的时候，可以不立即执行，当下次再次调用epoll_wait时还会提醒
    //ET :当检测到epoll_wait 上有就绪的事件的时候，就必须处理该事件，否则下次调用epoll_wait 的时候，也不会提醒。
    if(m_TRIGMode==0)  //LT+LT
    {
        m_LISTENTrigmode=0;
        m_CONNTrigmode=0;
    }
    else if(m_TRIGMode==1) //LT+ET
    {
        m_LISTENTrigmode=0;
        m_CONNTrigmode=1;
    }
    else if(m_TRIGMode==2) //ET+LT
    {
        m_LISTENTrigmode=1;
        m_CONNTrigmode=0;
    }
    else if(m_TRIGMode==3) //ET+ET
    {
        m_LISTENTrigmode=1;
        m_CONNTrigmode=1;
    }
}

void WebServer::sql_pool()
{
    //初始化数据库连接池
    m_connPool=Connection_Pool::GetInstance();
    m_connPool->Init("localhost",m_user,m_passWord,m_databaseName,3306,m_sql_num,m_close_log);

    //初始化数据库读取表
    users->initmysql_result(m_connPool);
    
}


void WebServer::thread_pool()
{
    //线程池
    m_pool=new threadpool<http_conn>(m_actormodel,m_connPool,m_thread_num);
}

void WebServer::eventListen()
{
    m_listenfd=socket(PF_INET,SOCK_STREAM,0);
    assert(m_listenfd>=0);


    //优雅断开
    if(m_OPT_LINGER==0)
    {
        linger tmp={0,1};
        setsockopt(m_listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
    }
    else if(m_OPT_LINGER==1)
    {
        linger tmp={1,1};
        setsockopt(m_listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
    }

    int ret=0;
    sockaddr_in address;
    bzero(&address,sizeof(address));

    address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_ANY);
    address.sin_port=htons(m_port);

    int flag=1;
    setsockopt(m_listenfd,SOL_SOCKET,SO_REUSEADDR,&flag,sizeof(flag));
    ret=bind(m_listenfd,(sockaddr*)&address,sizeof(address));
    assert(ret>=0);
    ret=listen(m_listenfd,5);
    assert(ret>=0);

    utils.init(TIMESLOT);

    //epoll创建内核事件表
    epoll_event evens[MAX_EVENT_NUMBER];
    m_epollfd=epoll_create(5);
    assert(m_epollfd!=-1);

    utils.addfd(m_epollfd,m_listenfd,false,m_LISTENTrigmode);
    http_conn::m_epollfd=m_epollfd;

    ret=socketpair(PF_UNIX,SOCK_STREAM,0,m_pipefd);
    assert(ret!=-1);

    utils.set_noblocking(m_pipefd[1]);
    utils.addfd(m_epollfd,m_pipefd[0],false,0);


    utils.addsig(SIGPIPE,SIG_IGN);
    utils.addsig(SIGALRM,utils.sig_handler,false);
    utils.addsig(SIGTERM,utils.sig_handler,false);

    alarm(TIMESLOT);
    
    Utils::u_pipefd=m_pipefd;
    Utils::u_epollfd=m_epollfd;

}


void WebServer::timer(int connfd,sockaddr_in client_address)
{
users[connfd].init(connfd,client_address,m_root,m_CONNTrigmode,m_close_log,m_user,m_passWord,m_databaseName);


//初始化client_data数据
//创建定时器，设置回调函数和超时时间,绑定用户数据，将定时器添加到连表中
users_timer[connfd].addr=client_address;
users_timer[connfd].sockfd=connfd;
util_timer* timer=new util_timer;
timer->userdata=&users_timer[connfd];
timer->cb_func=cb_func;
time_t cur=time(NULL);
timer->expire=cur+3*TIMESLOT;
users_timer[connfd].timer=timer;
utils.m_timer_list.add_timer(timer);

}

//有数据传输时，对timer重新进行调整
void WebServer::adjust_timer(util_timer* timer)
{
    time_t cur=time(NULL);
    timer->expire=cur+3*TIMESLOT;
    utils.m_timer_list.add_timer(timer);
    
}

void WebServer::deal_timer(util_timer* timer,int sockfd)
{
timer->cb_func(&users_timer[sockfd]);
if(timer)
{
    utils.m_timer_list.del_timer(timer);
}
}

bool WebServer::dealclientdata()
{
    sockaddr_in client_address;
    socklen_t client_addrlen=sizeof(client_address);

    if(m_LISTENTrigmode==0)
    {
        int connfd=accept(m_listenfd,(sockaddr*)&client_address,&client_addrlen);
        if(connfd<0)
        {
            return false;
        }
        
        if(http_conn::m_user_count>=MAX_FD)
        {
            utils.show_error(connfd,"Internal server busy");
            return false;
        }
        timer(connfd,client_address);
    }
    else
    {
       
       while (1)
       {
        int connfd=accept(m_listenfd,(sockaddr*)&client_address,&client_addrlen);
        if(connfd<0)
        {
            break;
        }
        
        if(http_conn::m_user_count>=MAX_FD)
        {
            utils.show_error(connfd,"Internal server busy");
            break;
        }
        timer(connfd,client_address);
       }
       return false;
    }

    return true;
}

bool WebServer::dealwithsignal(bool &timeout,bool &stop_server)
{
int ret=0;
int sig;
char signals[1024];
ret=recv(m_pipefd[0],signals,sizeof(signals),0);

if(ret==-1)
{
    return false;
}
else if(ret==0)
{
    return false;
}
else
{
    for(int i=0;i<ret;++i)
    {
        switch (signals[i])
        {
         case SIGALRM:
         {
            timeout=true;
            break;
         }
         case SIGTERM:
         {
            stop_server=true;
            break;
         }
        }
    }
}
return true;
}

//处理读事件
void WebServer::dealwithread(int sockfd)
{
util_timer* timer=users_timer[sockfd].timer;

//reactor
if(m_actormodel==1)
{
    if(timer)
    {
        adjust_timer(timer);
    }

    //检测到读事件，将事件放入请求队列
    m_pool->append(users+sockfd,0);

    while(1)
    {
        if(users[sockfd].improv==1)
        {
         if(users[sockfd].timer_flag==1)
         {
            deal_timer(timer,sockfd);
            users[sockfd].timer_flag=0;
         }
        users[sockfd].improv=0;
        break;
        }
    }
}
else
{
  //proactor
  if(users[sockfd].read_once())
  {
    //若检测到读事件，将该事件放入请求队列
    m_pool->append(users+sockfd,0);
    if(timer)
    {
        adjust_timer(timer);
    }
  }
  else
  {
    deal_timer(timer,sockfd);
  }
}
}


//处理写事件
void WebServer::dealwithwrite(int sockfd)
{
util_timer* timer=users_timer[sockfd].timer;

//reactor
if(m_actormodel==1)
{
    if(timer)
    {
        adjust_timer(timer);
    }

    //检测到读事件，将事件放入请求队列
    m_pool->append(users+sockfd,1);

    while(1)
    {
        if(users[sockfd].improv==1)
        {
         if(users[sockfd].timer_flag==1)
         {
            deal_timer(timer,sockfd);
            users[sockfd].timer_flag=0;
         }
        users[sockfd].improv=0;
        break;
        }
    }
}
else
{
  //proactor
  if(users[sockfd].write())
  {
    if(timer)
    {
        adjust_timer(timer);
    }
  }
  else
  {
    deal_timer(timer,sockfd);
  }
}
}

void WebServer::eventLoop()
{
    bool timeout=false;
    bool stop_server=false;

    while(!stop_server)
    {
        int number=epoll_wait(m_epollfd,evens,MAX_EVENT_NUMBER,-1);
        if(number<0&&errno!=EINTR)
        {
            break;
        }
        

        for(int i=0;i<number;i++)
        {
            int sockfd=evens[i].data.fd;
            //处理新接受到的客户端
            if(sockfd==m_listenfd)
            {
                bool flag=dealclientdata();
                if(flag==false)
                continue;
            }
            else if(evens[i].events&(EPOLLRDHUP|EPOLLHUP|EPOLLERR))
            {
                //服务端关闭连接，移除定时器
                auto timer=users_timer[sockfd].timer;
                deal_timer(timer,sockfd);
            }
            else if(sockfd==m_pipefd[0]&&(evens[i].events&EPOLLIN))//处理信号
            {
              bool flag=dealwithsignal(timeout,stop_server);
            }
            else if(evens[i].events&EPOLLIN)
            {
                dealwithread(sockfd);
            }
            else if(evens[i].events&EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
        if(timeout)
        {
            utils.timer_handler();
            timeout=false;
        }
    }
}