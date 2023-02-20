#include "lst_timer.h"
#include "../http/http_conn.h"

sort_timer_list::sort_timer_list()
{
    head=NULL;
    tail=NULL;
}

sort_timer_list::~sort_timer_list()
{
    if(!head)
    return;
    auto temp=head;
    while(temp)
    {
        head=head->next;
        delete temp;
        temp=head;
    }
}

void sort_timer_list::add_timer(util_timer* timer)
{
    if(!timer)
    return ;

    if(!head)
    {
        tail=head=timer;
        return;
    }

    if(timer->expire<head->expire)
    {
        timer->next=head;
        head->pre=timer;
        head=timer;
        return;
    }
    add_timer(timer,head);
}

//如果当前的expire大于后面的expire，则交换
void sort_timer_list::adjust_timer(util_timer* timer)
{
    if(!timer)
    return;

    auto temp=timer->next;

    if(!temp||temp->expire>timer->expire)
    return;

    if(timer==head)
    {
    head=head->next;
    head->pre=NULL;
    timer->next=NULL;
    add_timer(timer,head);
    }
    else
    {
        temp->pre=timer->pre;
        timer->pre->next=temp;
        add_timer(timer,timer->next);
    }
}

void sort_timer_list::del_timer(util_timer* timer)
{
    if(timer)
    return;

    if(timer==head&&timer==tail)
    {
        delete timer;
        head=NULL;
        tail=NULL;
        return;
    }

    if(timer==head)
    {
        head=head->next;
        head->pre=NULL;
        delete timer;
        return;
    }

    if(timer==tail)
    {
        tail=tail->pre;
        tail->next=NULL;
        delete timer;
        return;
    }

    timer->pre->next=timer->next;
    timer->next->pre=timer->pre;
    delete timer;
    return;
}

void sort_timer_list::tick()
{
    if(!head)
    return;

    time_t cur=time(NULL);
    auto temp=head;

    while(temp)
    {
       if(cur<temp->expire)
       {
        break;
       }

       temp->cb_func(temp->userdata);
       head=temp->next;

       if(head)
       {
        head->pre=NULL;
       }
       delete temp;
       return;
    }
}
//将timer添加到合适的位置
void sort_timer_list::add_timer(util_timer* timer,util_timer* head)
{
auto temp=head->next;
auto prev=head;

while(temp)
{
    if(timer->expire<temp->expire)
    {
        timer->next=temp;
        timer->pre=prev;
        temp->pre=timer;
        prev->next=timer;
        break;
    }
    prev=temp;
    temp=temp->next;
}

if(!temp)
{
    tail->next=timer;
    timer->pre=tail;
    timer->next=NULL;
    tail=timer;
}
}
//设置超时事件
void Utils::init(int timeslot)
{
  m_TIMELOUT=timeslot;
}

//对文件描述符设置非阻塞
//fcntl 用于改变已经打开的文件描述府进行获取或者修改
int Utils::set_noblocking(int fd)
{
int old_option=fcntl(fd,F_GETFL);
int new_option=old_option|O_NONBLOCK;
fcntl(fd,F_SETFL,new_option);
return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd,int fd,bool oneshot,int TRIGMode)
{
    epoll_event event; //指定事件
    event.data.fd=fd;

    if(TRIGMode==1)
    event.events= EPOLLIN|EPOLLET|EPOLLRDHUP; //可读 采用边沿触发 对方关闭时间
    else
    event.events=EPOLLIN|EPOLLRDHUP;

    if(oneshot)
    event.events=EPOLLONESHOT;

    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event); //epoll控制函数，添加一个event事件
    set_noblocking(fd);
}

//信号处理函数
void Utils::sig_handler(int sig)
{
//为保证函数的可重入性，保留原来的errno
    int save_errno=errno;
    int msg=sig;
    send(u_pipefd[1],(char *)(&msg),1,0); //???
    errno=save_errno;
}

//信号设置函数，设置收到信号时，信号handler怎么处理
void Utils::addsig(int sig,void (handler)(int),bool restart)
{
    struct sigaction sa;  //用于描述对信号的处理方式
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler=handler;
    if(restart)
    {
        sa.sa_flags|=SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig,&sa,NULL)!=-1);
}

//定时处理任务,重新定时以不断触发SIGALARM信号
void Utils::timer_handler()
{
    m_timer_list.tick();
    alarm(m_TIMELOUT);
}

void Utils::show_error(int connfd,const char* info)
{
send(connfd,info,sizeof(info),0);
close(connfd);
}

int *Utils::u_pipefd=NULL;
int Utils::u_epollfd=0; //遇到问题：类内静态变量必须在类外初始化以后才能使用
class Utils;

void cb_func(client_data* user_data)
{
    epoll_ctl(Utils::u_epollfd,EPOLL_CTL_DEL,user_data->sockfd,0); //事件注册表中删除事件
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;

}

