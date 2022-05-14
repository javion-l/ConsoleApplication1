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
class HeartBeat //���������ƣ�������epoll�ϳ�ʱ��client_sock
{
private:
	priority_queue<Record> record_heap;
	vector<Socket*> pos_to_ptr_mapping;
	int time_out;
public:
	void init(int time_out = 10);//��ʼ��
	void tick(int pos);//���»������¼�¼
	void unbind(int pos);//���HeartBeat��pos��Client_sock�İ󶨣����е��йؼ�¼����Ϊ��Ч��¼
	int bind(Socket* sock);	//��
	void check();//������鲢�ͷų�ʱ��sock
};
class Epoll:public Task
{
private:
	int epoll_fd;
	pthread_mutex_t heart_beat_mutex;
	HeartBeat heart_beat;
	epoll_event* event_list;	//��epoll_fd���صĿ�ִ���¼���	
	int event_list_capacity;
	
public://��Ϊ����ִ�еĺ���
	Epoll(int event_list_capacity = 100, time_t time_out = 10);
	~Epoll();
	void bind(Socket& sock);	//���µ�sock
	void unbind(Socket& sock);	//�����
	void tick(Socket* sock);	//��ӻ��¼
	void resetWatch(Socket& sock);	//sock��sock_list��������event_listʱ�����������¼�
	void pauseWatch(Socket& sock);
	void catchEvents(int block_time = 5);	//���ں��еõ�һ�����µ�����Ϣ���¼���
	void callBack();
	string getInfo() { return "Epoll( fd:" + to_string(epoll_fd) + " )"; }
};
class Socket:public Task
{
protected:
	int fd;					//�׽���
	sockaddr_in address;	//�����ַ��ǩ
	int current_event;		//����epoll�м�����ִ�еĵ��¼�
	int mode;				//�¼�����ģʽ
	bool status;			//״̬��0������		1:���п�ִ���¼�
	Epoll* epoll;			//���ڵ�epoll
	int pos;	//��epoll��HeartBeat�е�id
	friend class Epoll;		//Epoll����Կ���Socket��
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
	string buffer;			//���ں��ж�ȡ/�������ݣ����û��ռ�Ļ�����
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
			buffer.clear();			//���д�����������Ĺ������ǲ���epoll�еģ����Բ�����д���¼�����
			char buf_tmp[128];		//��һ��128���ֽ�
			while (true)			//����ʹ�÷�����IO����ȡ�ͻ���buffer��һ�ζ�ȡbuf��С���ݣ�ֱ��ȫ����ȡ���
			{			
				bzero(buf_tmp, sizeof buf_tmp);
				ssize_t bytes_recv = recv(fd,buf_tmp,sizeof(buf_tmp),NULL);
				if (bytes_recv > 0)
				{
					buffer.append(buf_tmp);
					cout << "--receive " << bytes_recv << " byte "<<endl;
				}
				else if (bytes_recv == -1 && errno == EINTR) 
				{  //�ͻ��������жϡ�������ȡ
					cout<<"--continue receiving..."<<endl;
					continue;
				}
				else if (bytes_recv == -1 && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) 
				{	//������IO�����������ʾ����ȫ����ȡ���
					cout<<"--finish receiving once..."<<endl;
					break;
				}
				else if (bytes_recv == 0) 
				{  //EOF���ͻ��˶Ͽ�����
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
				//��������
				doReq();
				//���������Ϊд�����¹ҵ����ϣ�����
				current_event = EPOLLRDHUP | EPOLLOUT;
				mode =EPOLLET;
				epoll->resetWatch(*this);
				epoll->tick(this);
			}
			else if (current_event & EPOLLOUT)
			{
				current_event = NULL;
				int send_num = send(fd, buffer.c_str(), buffer.length(), NULL);
				if (send_num <= 0 || send_num == buffer.length()) delete this;	//��������߷������ϣ��Ͽ�����
				else //û�з����꣬�ҵ����ϼ�������д������
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
			fcntl(client_fd, F_SETFL, O_NONBLOCK);			//��IO����Ϊ�Ƕ�����Recv��Send��������
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
	void acceptNewCon();//���ܿͻ��˵�socket���Լ�����epoll
	void callBack() 
	{
		cout << getInfo() << " wait to accept new client ..." << " ( pid:" << pthread_self() << " ) " << endl;;
		current_event = NULL;
		acceptNewCon();
		current_event = EPOLLIN;
		mode = 0;//Listen_Socket��ˮƽ��������EPOLLLT��EPOLLIN|EPOLLLT==EPOLLIN������EPOLLLT==0
		epoll->resetWatch(*this);
	}
	~Listen_Socket(){}
	string getInfo() { return "Listen_Socket( fd:" + to_string(fd) + " )"; }
};

