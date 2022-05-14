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
        //����thread_pool
        /*errif(pthread_mutex_init(&pool_mutex, NULL) != 0 ||
            pthread_cond_init(&pool_cond, NULL) != 0, "init mutex or condition fail...");*/
        cout << "Threadpool mutex and condition init success" << endl;
        Threadpool::min_num = min_num;
        for (int i = 0; i < min_num; i++)
        {
            thread_list->AddThread(Worker);
            alive_num++;
        }
        //��ʼ���̳߳ص�������������
        // �����������߳�
        pthread_create(&manager_thread, NULL, Manager,NULL);
        stop = 0;
    } while (0);
}
void Threadpool::Destroy()
{
    stop = 1;
    // ���ٹ������߳�
    pthread_join(manager_thread, NULL);
    // ���������������߳�
    for (int i = 0; i < alive_num; i++)
        pthread_cond_signal(&pool_cond);
    //�����������
    if (task_queue) delete task_queue;
    //�����̳߳�
    if (thread_list) delete thread_list;
    pthread_mutex_destroy(&pool_mutex);
    pthread_cond_destroy(&pool_cond);
}

void Threadpool::AddTask(Task* task)
{
    if (stop)return;
    // ������񣬲���Ҫ�������������������
    task_queue->PushTask(task);
    // ���ѹ������߳�
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


// �����߳�������
void* Threadpool::Worker(void* arg)
{
    Thread* my_thread = (Thread*) arg;
    // һֱ��ͣ�Ĺ���
    while (true)
    {
        // �����������(������Դ)����
        pthread_mutex_lock(&pool_mutex);
        // �ж���������Ƿ�Ϊ��, ���Ϊ�չ����߳�����
        while (task_queue->GetSize()== 0 && !stop)
        {
            cout << "Thread " << to_string(pthread_self()) << " waiting..." << endl;
            // �����߳�
            pthread_cond_wait(&pool_cond, &pool_mutex);

            // �������֮��, �ж��Ƿ�Ҫ�����߳�
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
        // �ж��̳߳��Ƿ񱻹ر���
        if (stop)
        {
            pthread_mutex_unlock(&pool_mutex);
            ThreadExit(my_thread);
        }
        // �����������ȡ��һ������
        Task* task = task_queue->PopTask();
        // �������߳�+1
        busy_num++;
        // �̳߳ؽ���
        pthread_mutex_unlock(&pool_mutex);
        // ִ������
        cout << "Thread " << to_string(pthread_self()) << " start working..." << endl;
        try
        {
            task->callBack();
        }
        catch(exception&)
        {
            cout << "Thread " << to_string(pthread_self()) << " working crush..." << endl;
        }
        // ���������
        cout << "Thread " << to_string(pthread_self()) << " end working..."<<endl;
        pthread_mutex_lock(&pool_mutex);
        busy_num--;
        pthread_mutex_unlock(&pool_mutex);
    }

    return nullptr;
}

// �������߳�������
void* Threadpool::Manager(void* arg)
{

    // ����̳߳�û�йر�, ��һֱ���
    while (!stop)
    {
        // ÿ��5s���һ��
        sleep(5);
        // ȡ���̳߳��е����������߳�����
        //  ȡ���������̳߳�����
        pthread_mutex_lock(&pool_mutex);
        int queue_size_now = task_queue->GetSize();
        int alive_num_now = alive_num;
        int busy_num_now = busy_num;
        pthread_mutex_unlock(&pool_mutex);

        // �����߳�
        const int change_step = 2;
        // ��ǰ�������>�����߳��� && �����߳���<����̸߳���
        if (queue_size_now > alive_num_now )
        {
            // �̳߳ؼ���
            pthread_mutex_lock(&pool_mutex);
            for (int i = 0; i < change_step; i++)
            {
                thread_list->AddThread(Worker);
                alive_num++;
            }
            pthread_mutex_unlock(&pool_mutex);
        }
          
        // ���ٶ�����߳�
        // æ�߳�*2 < �����߳���Ŀ && �����߳��� > ��С�߳�����
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

// �߳��˳�
void Threadpool::ThreadExit(Thread* del_thread)
{
    cout << "Thread " << to_string(pthread_self()) << " exit..." << endl;
    thread_list->DelThread(del_thread);
    pthread_exit(NULL);
}

