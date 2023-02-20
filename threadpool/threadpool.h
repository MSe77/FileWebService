#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<list>
#include<cstdio>
#include<exception>
#include<pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template<typename T>
class threadpool
{
    public:
    //线性池数量最多为8,请求最大数量为10000
    threadpool(int actor_model, Connection_Pool* conn_pool,int thread_num=8,int max_request=10000);
    ~threadpool();
    //该函数用于添加读或者写任务到任务队列
    bool append(T* request,int state);
    //该函数用于？？?
    bool append_p(T* request);
    
    private:
    //工作线成运行的函数，它不断从工作队列中取出任务并且运行
    //void* 代表可以接受接受任意类型的指针参数，并且返回任意类型的指针
    static void* worker(void* args);

    void run();

    private:
    int m_thread_num; //线程池中线程数量
    int m_max_request; //请求队列中允许的最大数量
    pthread_t* m_threads;//线程池
    std::list<T* > m_workqueue; //请求队列
    locker m_queuelock; //保护请求队列的互斥锁 
    sem m_queuestate;//请求工作信号量
    Connection_Pool* m_conn_pool; //连接池
    int m_actor_model; //模式切换
};

template<typename T>
threadpool<T>::threadpool(int actor_model, Connection_Pool* conn_pool,int thread_num,int max_request):m_actor_model(actor_model),m_conn_pool(conn_pool),m_thread_num(thread_num),m_max_request(max_request),m_threads(NULL)
{
    if(thread_num<=0 || max_request<=0)
     throw std::exception();
    
    this->m_threads=new pthread_t[thread_num];
    if(!this->m_threads)
    {
     throw std::exception();
    }

    for(int i=0;i<thread_num;++i)
    {
        //线程创建函数，成功返回0,失败返回错误码
        //从左往右参数分别为，创建线程地质，线程属性，开始运行函数，运行函数的参数
        if(pthread_create(this->m_threads+i,NULL,worker,this)!=0)
        {
           delete []this->m_threads;
           throw std::exception();
        }
        
        //从状态上实现线程分离，指定该状态，线程主动与主控进程切断联系，该线程运行结束后会自动释放资源。
        //成功0,失败错误号
        if(pthread_detach(m_threads[i])!=0)
        {
           delete []this->m_threads;
           throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool()
{
delete []this->m_threads;
}

template <typename T>
bool threadpool<T>::append(T* request,int state)
{
  this->m_queuelock.lock();
  if(this->m_workqueue.size()>=this->m_max_request)
  {
    this->m_queuelock.unlock();
    return false;
  }

  request->m_state=state; //????
  this->m_workqueue.push_back(request);
  this->m_queuelock.unlock();
  this->m_queuestate.post();
  return true;
}

template<typename T>
bool threadpool<T>::append_p(T* request)
{
  this->m_queuelock.lock();
  if(this->m_workqueue.size()>=this->m_max_request)
  {
    this->m_queuelock.unlock();
    return false;
  }

  this->m_workqueue.push_back(request);
  this->m_queuelock.unlock();
  this->m_queuestate.post();
  return true;
}

template<typename T>
void* threadpool<T>::worker(void* arg)
{
     threadpool* pool=(threadpool*) arg;
     pool->run();
     return pool;
}

template<typename T>
void threadpool<T>::run()
{
    while(true)
    {
        m_queuestate.wait();
        m_queuelock.lock();

        if(m_workqueue.empty())
        {
            m_queuelock.unlock();
            continue;
        }
         
        T* request=m_workqueue.front();
        m_workqueue.pop_back();
        m_queuelock.unlock();

        if(!request)
        continue;

        if(m_actor_model==1)
        {
            if(request->m_state==0)
            {
                if(request->read_once())
                {
                     request->improv=1;
                     ConnectionRAII conn(&request->mysql,m_conn_pool);
                     request->process();
                }
            }
            else
            {
                if(request->write())
                {
                    request->improv=1;
                }
                else
                {
                    request->improv=1;
                    request->timer_flag=1;
                }
            }
        }
        else
        {
            ConnectionRAII conn(&request->mysql,m_conn_pool);
            request->process();
        }
    }
}
#endif