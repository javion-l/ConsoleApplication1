#pragma once
#include "Epoll.h"
#include<pthread.h>
#include<queue>
#include<unordered_map>

class Taskqueue
{
public:
	Taskqueue();
	~Taskqueue();
	void PushTask(Task* task);
	Task* PopTask();
    int GetSize() 
    { 
        int size = 0;
        pthread_mutex_lock(&task_queue_mutex);
        size =  task_queue.size();
        pthread_mutex_unlock(&task_queue_mutex);
        return size;
    }
private:
	pthread_mutex_t	task_queue_mutex;    // 互斥锁
	queue<Task*> task_queue;   // 任务队列
};
struct Thread
{
    pthread_t pid;
    Thread* next;
    Thread* prev;
};
class Threadlist
{
private:
    Thread* head;
    int size;
public:
    int GetSize() { return size; }
    Threadlist()
    { 
        head = new Thread();
        head->next = NULL;
        head->prev = NULL;
        size = 0;
    }
    ~Threadlist()
    {
        Thread* p = head->next;
        Thread* p_last = head;
        while (p)
        {
            delete p_last;
            p_last = p;
            p = p->next;
        }
        delete p_last;
        size = 0;
    }
    void AddThread(void* (*func)(void *))
    {        
        Thread* new_thread = new Thread();
        pthread_t new_pid;
        pthread_create(&new_pid, NULL, func, new_thread);
        new_thread->pid = new_pid;
        AddThread(new_thread);
    }
    void AddThread(Thread* new_thread)
    {
        if (head->next)
        {
            new_thread->next = head->next;
            new_thread->prev = head;
            head->next->prev = new_thread;
            head->next = new_thread;
        }
        else
        {
            head->next = new_thread;
            new_thread->prev = head;
            new_thread->next = NULL;
        }
        size++;
    }
    Thread* Pop()
    {
        if (!head->next)return NULL;
        Thread* thread_pop = head->next;
        if (thread_pop->next)
            thread_pop->next->prev = head;
        head->next = thread_pop->next;
        size--;
        return thread_pop;
    }
    void DelThread(Thread* del_thread)
    {
        if (!del_thread->prev)return;
        if(del_thread->next)del_thread->next->prev = del_thread->prev;
        del_thread->prev = del_thread->next;
        size--;
        delete del_thread;
    }

};
class Threadpool//单例模式
{
public:
    static void Init(int min_num = 20);
    static void Destroy();
    // 添加任务
    static void AddTask(Task* task);
    // 获取忙线程的个数
    static int GetBusyNumber();
    // 获取活着的线程个数
    static int GetAliveNumber();
private:
    Threadpool() {};
    // 工作的线程的任务函数
    static void* Worker(void* arg);
    // 管理者线程的任务函数
    static void* Manager(void* arg);
    static void ThreadExit(Thread* del_thread);
    static pthread_mutex_t pool_mutex;
    static pthread_cond_t pool_cond;
    static Threadlist* thread_list;
    static pthread_t manager_thread;
    static Taskqueue* task_queue;
    static int min_num;
    static int exit_num;
    static int alive_num;
    static int busy_num;
    static bool stop;
};
