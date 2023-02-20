#ifndef LOCKER_H
#define LOCKER_H

#include<exception>
// 定义与异常处理相关的若干类型和函数。 异常处理用于系统可从错误中恢复的情形。 它提供了将控制权从函数返回给程序的一种方法。

#include<semaphore.h>
//semaphore是系统中的东西,所以不同系统中包含头文件不同,在linux中包含<semaphore.h>
//linux下信号量(semaphore)

#include<pthread.h>
//Linux系统下的多线程遵循POSIX线程接口，称为pthread。编写Linux下的多线程程序，需要使用头文件pthread.h

class sem
{
  public:
    sem()
    {
        if(sem_init(&m_sem,0,0)!=0)  //初始化信号量值为0
        {
            throw std::exception();
        }
    }

    sem(int num)   //带数值的初始化
    {
        if(sem_init(&m_sem,0,num)!=0)
        {
            throw std::exception();
        }
    }

    ~sem()
    {
        sem_destroy(&m_sem);
    }

    bool wait()
    {
        return sem_wait(&m_sem)==0;
    }
    // 用来等待信号量的值大于0（value > 0），等待时该线程为阻塞状态
    // 解除阻塞后sem值会减去1

    bool post()
    {
        return sem_post(&m_sem)==0;
    }
    //sem的值+1

    private:
     sem_t m_sem;  //信号两
};

class locker
{
 public:
    locker()
    {
        if(pthread_mutex_init(&mutex_t,NULL)!=0)
        {
            throw std::exception();
        }
    }

    ~locker()
    {
        pthread_mutex_destroy(&mutex_t);
    }

    bool lock()  //上锁，只有一个进程能访问核心代码段
    {
        return pthread_mutex_lock(&mutex_t)==0;
    }

    bool unlock()  //解锁
    {
        return pthread_mutex_unlock(&mutex_t)==0;
    }

 private:
  pthread_mutex_t mutex_t;  // 互斥锁
};

// 一般pthread_cond_t，会搭配pthread_mutex_t 一起使用的， 因为线程间通信时操作共享内存时，需要用到锁。
// 当锁住的共享变量发生改变时，可能需要通知相应的线程（因为可能该共享变量涉及到多个线程），这时就需要用到pthread_cond_t
// 这种条件变量来精准的通知某个或几个线程， 让他们执行相应的操作
class cond
{

cond()
{
    if(pthread_cond_init(&cond_t,NULL)!=0)
    throw std::exception();
}

~cond()
{
    pthread_cond_destroy(&cond_t);
}

bool wait(pthread_mutex_t *mutex_t)  //不理解
{
    int ret=0;
    ret=pthread_cond_wait(&cond_t,mutex_t);
    return ret;
}

bool timewait(pthread_mutex_t* mutex_t,timespec t) //不理解
{
    int ret=0;
    ret=pthread_cond_timedwait(&cond_t,mutex_t,&t);
    return ret;
}

bool signal() //不理解
{
    return pthread_cond_signal(&cond_t);
}

bool broadcast() //不理解
{
    return pthread_cond_broadcast(&cond_t);
}

private:
 pthread_cond_t cond_t; 
};


#endif