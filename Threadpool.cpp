#include"Threadpool.h"
using namespace std;
//Taskqueue
Taskqueue::Taskqueue()
{
    pthread_mutex_init(&task_queue_mutex, NULL);
}

Taskqueue::~Taskqueue()
{
    while (task_queue.size())
    {
        delete task_queue.front();
        task_queue.pop();
    }
    pthread_mutex_destroy(&task_queue_mutex);
    cout << "taskqueue destroy success" << endl;
}

void Taskqueue::PushTask(Task* task)
{
    pthread_mutex_lock(&task_queue_mutex);
    task_queue.push(task);
    pthread_mutex_unlock(&task_queue_mutex);
}

Task* Taskqueue::PopTask()
{
    Task* task;
    pthread_mutex_lock(&task_queue_mutex);
    if (task_queue.size() > 0)
    {
        task = task_queue.front();
        task_queue.pop();
    }
    pthread_mutex_unlock(&task_queue_mutex);
    return task;
}
//Threadpool
Threadlist* Threadpool::thread_list = new Threadlist();
Taskqueue* Threadpool::task_queue = new Taskqueue();
pthread_mutex_t Threadpool::pool_mutex;
pthread_cond_t Threadpool::pool_cond;
pthread_t Threadpool::manager_thread;
int Threadpool::min_num;
int Threadpool::alive_num;
int Threadpool::busy_num;
int Threadpool::exit_num;
bool Threadpool::stop;
void Threadpool::Init(int min_num)
{
    do {
        //创建thread_pool
        /*errif(pthread_mutex_init(&pool_mutex, NULL) != 0 ||
            pthread_cond_init(&pool_cond, NULL) != 0, "init mutex or condition fail...");*/
        cout << "Threadpool mutex and condition init success" << endl;
        Threadpool::min_num = min_num;
        for (int i = 0; i < min_num; i++)
        {
            thread_list->AddThread(Worker);
            alive_num++;
        }
        //初始化线程池的锁和条件变量
        // 创建管理者线程
        pthread_create(&manager_thread, NULL, Manager,NULL);
        stop = 0;
    } while (0);
}
void Threadpool::Destroy()
{
    stop = 1;
    // 销毁管理者线程
    pthread_join(manager_thread, NULL);
    // 唤醒所有消费者线程
    for (int i = 0; i < alive_num; i++)
        pthread_cond_signal(&pool_cond);
    //销毁任务队列
    if (task_queue) delete task_queue;
    //销毁线程池
    if (thread_list) delete thread_list;
    pthread_mutex_destroy(&pool_mutex);
    pthread_cond_destroy(&pool_cond);
}

void Threadpool::AddTask(Task* task)
{
    if (stop)return;
    // 添加任务，不需要加锁，任务队列中有锁
    task_queue->PushTask(task);
    // 唤醒工作的线程
    pthread_cond_signal(&pool_cond);
}

int Threadpool::GetAliveNumber()
{
    int num = 0;
    pthread_mutex_lock(&pool_mutex);
    num = alive_num;
    pthread_mutex_unlock(&pool_mutex);
    return num;
}

int Threadpool::GetBusyNumber()
{
    int num = 0;
    pthread_mutex_lock(&pool_mutex);
    num = busy_num;
    pthread_mutex_unlock(&pool_mutex);
    return num;
}


// 工作线程任务函数
void* Threadpool::Worker(void* arg)
{
    Thread* my_thread = (Thread*) arg;
    // 一直不停的工作
    while (true)
    {
        // 访问任务队列(共享资源)加锁
        pthread_mutex_lock(&pool_mutex);
        // 判断任务队列是否为空, 如果为空工作线程阻塞
        while (task_queue->GetSize()== 0 && !stop)
        {
            cout << "Thread " << to_string(pthread_self()) << " waiting..." << endl;
            // 阻塞线程
            pthread_cond_wait(&pool_cond, &pool_mutex);

            // 解除阻塞之后, 判断是否要销毁线程
            if (exit_num > 0)
            {
                exit_num--;
                if (alive_num>min_num)
                {
                    alive_num--;
                    pthread_mutex_unlock(&pool_mutex);
                    ThreadExit(my_thread);
                }
            }
        }
        // 判断线程池是否被关闭了
        if (stop)
        {
            pthread_mutex_unlock(&pool_mutex);
            ThreadExit(my_thread);
        }
        // 从任务队列中取出一个任务
        Task* task = task_queue->PopTask();
        // 工作的线程+1
        busy_num++;
        // 线程池解锁
        pthread_mutex_unlock(&pool_mutex);
        // 执行任务
        cout << "Thread " << to_string(pthread_self()) << " start working..." << endl;
        try
        {
            task->callBack();
        }
        catch(exception&)
        {
            cout << "Thread " << to_string(pthread_self()) << " working crush..." << endl;
        }
        // 任务处理结束
        cout << "Thread " << to_string(pthread_self()) << " end working..."<<endl;
        pthread_mutex_lock(&pool_mutex);
        busy_num--;
        pthread_mutex_unlock(&pool_mutex);
    }

    return nullptr;
}

// 管理者线程任务函数
void* Threadpool::Manager(void* arg)
{

    // 如果线程池没有关闭, 就一直检测
    while (!stop)
    {
        // 每隔5s检测一次
        sleep(5);
        // 取出线程池中的任务数和线程数量
        //  取出工作的线程池数量
        pthread_mutex_lock(&pool_mutex);
        int queue_size_now = task_queue->GetSize();
        int alive_num_now = alive_num;
        int busy_num_now = busy_num;
        pthread_mutex_unlock(&pool_mutex);

        // 创建线程
        const int change_step = 2;
        // 当前任务个数>存活的线程数 && 存活的线程数<最大线程个数
        if (queue_size_now > alive_num_now )
        {
            // 线程池加锁
            pthread_mutex_lock(&pool_mutex);
            for (int i = 0; i < change_step; i++)
            {
                thread_list->AddThread(Worker);
                alive_num++;
            }
            pthread_mutex_unlock(&pool_mutex);
        }
          
        // 销毁多余的线程
        // 忙线程*2 < 存活的线程数目 && 存活的线程数 > 最小线程数量
        else if (busy_num_now * 2 < alive_num_now && alive_num_now > min_num)
        {
            pthread_mutex_lock(&pool_mutex);
            exit_num = change_step;
            pthread_mutex_unlock(&pool_mutex);
            for (int i = 0; i < change_step;i++)
                pthread_cond_signal(&pool_cond);
        }
    }
    return nullptr;
}

// 线程退出
void Threadpool::ThreadExit(Thread* del_thread)
{
    cout << "Thread " << to_string(pthread_self()) << " exit..." << endl;
    thread_list->DelThread(del_thread);
    pthread_exit(NULL);
}

