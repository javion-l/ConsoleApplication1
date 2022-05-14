#pragma once
#include<unistd.h>
#include<stdlib.h>
#include<netdb.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<fcntl.h>
#include<string>
#include<stdio.h>
#include<sys/epoll.h>
#include<arpa/inet.h>
#include<fcntl.h>
#include<errno.h>
#include<stdlib.h>
#include<time.h>
#include<iostream>
#include<cstring>
#include<vector>
#include<queue>
using namespace std;
void errif(bool condition, const char* errmsg);
class Task
{
public:
	virtual string getInfo() = 0;
	virtual void callBack() = 0;
	virtual ~Task() {};
};
class Socket;
class Client_Socket;
class Threadpool;
struct Record
{
	time_t last_active_time;
	int pos;
	bool operator <(const Record r)const
	{
		return last_active_time < r.last_active_time;
	}
};
class HeartBeat //心跳检测机制，检查挂在epoll上超时的client_sock
{
private:
	priority_queue<Record> record_heap;
	vector<Socket*> pos_to_ptr_mapping;
	int time_out;
public:
	void init(int time_out = 10);//初始化
	void tick(int pos);//有新活动，添加新记录
	void unbind(int pos);//解除HeartBeat和pos处Client_sock的绑定，堆中的有关记录均变为无效记录
	int bind(Socket* sock);	//绑定
	void check();//主动检查并释放超时的sock
};
class Epoll:public Task
{
private:
	int epoll_fd;
	pthread_mutex_t heart_beat_mutex;
	HeartBeat heart_beat;
	epoll_event* event_list;	//从epoll_fd返回的可执行事件表	
	int event_list_capacity;
	
public://均为被动执行的函数
	Epoll(int event_list_capacity = 100, time_t time_out = 10);
	~Epoll();
	void bind(Socket& sock);	//绑定新的sock
	void unbind(Socket& sock);	//解除绑定
	void tick(Socket* sock);	//添加活动记录
	void resetWatch(Socket& sock);	//sock在sock_list，而不在event_list时，重新载入事件
	void pauseWatch(Socket& sock);
	void catchEvents(int block_time = 5);	//从内核中得到一次最新的有消息的事件表
	void callBack();
	string getInfo() { return "Epoll( fd:" + to_string(epoll_fd) + " )"; }
};
class Socket:public Task
{
protected:
	int fd;					//套接字
	sockaddr_in address;	//网络地址标签
	int current_event;		//正在epoll中监听或执行的的事件
	int mode;				//事件触发模式
	bool status;			//状态：0：空闲		1:具有可执行事件
	Epoll* epoll;			//所在的epoll
	int pos;	//在epoll的HeartBeat中的id
	friend class Epoll;		//Epoll类可以控制Socket类
public:
	Socket() {};
	Socket(int fd, sockaddr_in& address, Epoll& epoll)
	{
		this->fd = fd;
		this->address = address; 
		this->epoll = &epoll;
	}
	char* getIP() { return inet_ntoa(address.sin_addr); }
	short getPort() { return ntohs(address.sin_port); }
	bool getStatus(){ return status; }
	int getFd() { return fd; }
	~Socket() {
		current_event = NULL;
		mode = NULL;
		epoll = NULL; 
		status = 0;
		close(fd); 
		fd = -1;
	}
};
class Client_Socket :public Socket
{
private:
	string buffer;			//向内核中读取/发送数据，在用户空间的缓存区
public:

		string getInfo() { return "Client_socket( fd:" + to_string(fd)+" )"; }
		void doReq() 
		{
			buffer = "HTTP/1.1 200 OK \nContent - Length: 53 \nContent - Type : text / plain; charset = UTF - 8 \nDate: Thu, 12 May 2022 15 : 52 : 03 GMT\n\n";
			int times = 999999;
			while (times--)
			{
				buffer += "<p>"+to_string(999999 -times)+"</p>";
			}
		}
		int recvReq() 
		{
			cout << getInfo() << " start to receive..."<<" ( pid:"<<pthread_self()<<" ) "<< endl;
			buffer.clear();			//清空写缓存区，读的过程中是不在epoll中的，所以不会有写的事件干扰
			char buf_tmp[128];		//读一次128个字节
			while (true)			//由于使用非阻塞IO，读取客户端buffer，一次读取buf大小数据，直到全部读取完毕
			{			
				bzero(buf_tmp, sizeof buf_tmp);
				ssize_t bytes_recv = recv(fd,buf_tmp,sizeof(buf_tmp),NULL);
				if (bytes_recv > 0)
				{
					buffer.append(buf_tmp);
					cout << "--receive " << bytes_recv << " byte "<<endl;
				}
				else if (bytes_recv == -1 && errno == EINTR) 
				{  //客户端正常中断、继续读取
					cout<<"--continue receiving..."<<endl;
					continue;
				}
				else if (bytes_recv == -1 && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) 
				{	//非阻塞IO，这个条件表示数据全部读取完毕
					cout<<"--finish receiving once..."<<endl;
					break;
				}
				else if (bytes_recv == 0) 
				{  //EOF，客户端断开连接
					cout << "--stop receiving" << endl;
					return -1;
					break;
				}
			}
			cout << "--content:" << getBuffer() << endl;
			return 0;
		};
		void callBack()
		{
			if (current_event & EPOLLIN)
			{
				current_event = NULL;
				if (recvReq() == -1)
				{
					delete this;
					return;
				}
				//处理请求
				doReq();
				//处理后重置为写，重新挂到树上，监听
				current_event = EPOLLRDHUP | EPOLLOUT;
				mode =EPOLLET;
				epoll->resetWatch(*this);
				epoll->tick(this);
			}
			else if (current_event & EPOLLOUT)
			{
				current_event = NULL;
				int send_num = send(fd, buffer.c_str(), buffer.length(), NULL);
				if (send_num <= 0 || send_num == buffer.length()) delete this;	//发送完或者发生故障，断开连接
				else //没有发送完，挂到树上继续监听写缓存区
				{
					cout << send_num << endl;
					buffer = buffer.substr(send_num, buffer.length() - send_num);
					current_event = EPOLLRDHUP | EPOLLOUT;
					mode = EPOLLET;
					epoll->resetWatch(*this);
					epoll->tick(this);
				}
			}
		};
		Client_Socket(int client_fd, sockaddr_in& client_address, Epoll& epoll) :Socket(client_fd, client_address, epoll)
		{
			current_event = EPOLLRDHUP|EPOLLIN;
			mode = EPOLLET;
			fcntl(client_fd, F_SETFL, O_NONBLOCK);			//将IO设置为非堵塞，Recv和Send不再阻塞
			this->epoll->tick(this);
			this->epoll->bind(*this);
		}
		~Client_Socket() 
		{ 
			epoll->unbind(*this);
			cout<<getInfo()+" disconnected... " << " ( pid:" << pthread_self() << " ) " <<endl;
		}
		string getBuffer() { return buffer; }
};
class Listen_Socket :public Socket
{
private:
	int max_con_num;
public:
	Listen_Socket(int host_port, Epoll& epoll, int max_con_num = 20);
	void acceptNewCon();//接受客户端的socket到自己所在epoll
	void callBack() 
	{
		cout << getInfo() << " wait to accept new client ..." << " ( pid:" << pthread_self() << " ) " << endl;;
		current_event = NULL;
		acceptNewCon();
		current_event = EPOLLIN;
		mode = 0;//Listen_Socket是水平触发，即EPOLLLT。EPOLLIN|EPOLLLT==EPOLLIN，所以EPOLLLT==0
		epoll->resetWatch(*this);
	}
	~Listen_Socket(){}
	string getInfo() { return "Listen_Socket( fd:" + to_string(fd) + " )"; }
};

