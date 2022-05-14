#include"Threadpool.h"
int main()
{
    Threadpool::Init(10);
    Epoll* ep = new Epoll(1024,3);
    Threadpool:: AddTask(ep);
    while (1)
    {
        sleep(2);
       // cout << "Alive Num:" << Threadpool::GetAliveNumber() << " , Busy Num:" << Threadpool::GetBusyNumber() << endl;
    }
    //Threadpool::Destroy();
}