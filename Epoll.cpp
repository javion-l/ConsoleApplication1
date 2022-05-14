#include"Epoll.h"
#include"Threadpool.h"
void errif(bool condition, string errmsg) {
	if (condition) {
		perror(errmsg.c_str());
		exit(EXIT_FAILURE);
	}
}
char* toEventName(int event)
{
	if (event == 0)
		return " lt";
	else if (event & EPOLLIN)
		return " receive new message...";
	else if (event == EPOLLET)
		return " et";
	else if (event & EPOLLOUT)
		return " can send new message...";
	else if (event & EPOLLRDHUP)
		return " need close...";
	else
		return " unknown action...";
}
//HeartBeat
void HeartBeat::init(int time_out)//初始化
{
	this->time_out = time_out;
}
void HeartBeat::tick(int pos)//有新活动，添加新记录
{
	if (pos >= pos_to_ptr_mapping.size())return;
	record_heap.push({ time(NULL),pos });
}
void HeartBeat::unbind(int pos)//解除HeartBeat和pos处Client_sock的绑定，堆中的有关记录均变为无效记录
{
	if (pos >= pos_to_ptr_mapping.size())return;
	pos_to_ptr_mapping[pos] = 0;
}
int HeartBeat::bind(Socket* sock)	//绑定
{
	for (int i = 0; i < pos_to_ptr_mapping.size(); i++)
		if (!pos_to_ptr_mapping[i])//找到空位置
		{
			pos_to_ptr_mapping[i] = sock;
			return i;
		}
	pos_to_ptr_mapping.push_back(sock);
	return pos_to_ptr_mapping.size() - 1;
}
void HeartBeat::check()//主动检查并释放超时的sock
{
	while (record_heap.size() && time(NULL) - record_heap.top().last_active_time > time_out)
	{
		Record r = record_heap.top();
		record_heap.pop();
		Socket* ptr = pos_to_ptr_mapping[r.pos];
		if (!ptr || ptr->getStatus())continue;//如果指针为空，或指针指向的sock正在工作，则为无效记录
		cout << ptr->getInfo() << " have no executable event over " << time_out << "s ,will exit..." << endl;
		delete ptr;	//释放内存
		cout << " HeatBeat have remained record：" << record_heap.size() << endl;
	}
}
//Epoll
void Epoll::tick(Socket* sock)	//添加活动记录
{
	pthread_mutex_lock(&heart_beat_mutex);
	heart_beat.tick(sock->fd);
	pthread_mutex_unlock(&heart_beat_mutex);
}
void Epoll::pauseWatch(Socket& sock)
{
	epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock.fd, NULL);
}
Epoll::Epoll(int event_list_capacity,time_t time_out)
{
	this->event_list_capacity = event_list_capacity;
	event_list = new epoll_event[event_list_capacity];
	epoll_fd = epoll_create(1024);
	pthread_mutex_init(&heart_beat_mutex,NULL);
	heart_beat.init(time_out);
	errif(epoll_fd == -1,  getInfo()+" create error");
	cout << getInfo()+" create success" << " ( pid:" << pthread_self() << " ) " << endl;;
}
Epoll::~Epoll()
{
	//销毁锁
	pthread_mutex_destroy(&heart_beat_mutex);
	//删除event_list
	delete event_list;
	//释放epoll_fd
	close(epoll_fd);
	cout << getInfo()+" exit sucessfully" << " ( pid:" << pthread_self() << " ) " << endl;;
}
void Epoll::bind(Socket& sock)
{
	//添加到epoll树上
	epoll_event  epv = { sock.current_event|sock.mode,&sock };
	errif( epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock.fd, &epv)==-1, getInfo()+" add event error");
	//加入heart_beat进行超时监管，双向绑定
	sock.pos =heart_beat.bind(&sock);
}
void Epoll::unbind(Socket& sock) 
{ 
	epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock.fd, NULL);
	heart_beat.unbind(sock.pos);
}
void Epoll::catchEvents(int block_time)//从树上摘下有最新消息的事件，并执行，多线程在这实现
{
	bzero(event_list,event_list_capacity);
	int events_num= epoll_wait(epoll_fd,event_list, event_list_capacity, block_time);
	errif(events_num == -1&&errno!=EINTR, getInfo()+" wait error");
	for (int i = 0; i < events_num; i++)
	{
		Socket* sock = (Socket*)event_list[i].data.ptr;
		if (event_list[i].events & EPOLLRDHUP) delete sock;	//socket中断校验
		else if (event_list[i].events & sock->current_event)//socket其他工作校验
		{
			cout << getInfo()+" catch new executable event: "+sock->getInfo()+toEventName(sock->current_event)+" ,mode:"+toEventName(sock->mode) << " ( pid:" << pthread_self() << " ) " << endl;;
			pauseWatch(*sock);//将sock从树上摘下，暂时不再监听
			cout << "--new executable event add to taskqueue ..." << " ( pid:" << pthread_self() << " ) " << endl;
			sock->status = 1;//具有可执行事件（正在排队或执行）
			Threadpool::AddTask(sock);	//添加到队列
		}
	}
}
void Epoll::resetWatch(Socket& sock)
{
	epoll_event  epv = { sock.current_event|sock.mode,&sock };
	errif(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock.fd, &epv) == -1, getInfo()+" reset event error");
}
void Epoll::callBack()
{
	cout << getInfo() + " start working..." << " ( pid:" << pthread_self() << " ) " << endl;;
	Listen_Socket* sock = new Listen_Socket(9006, *this);
	while (1)
	{
		catchEvents(3);
		heart_beat.check();
	}
}

//Listen_Socket
void Listen_Socket::acceptNewCon()//接受客户的socket到自己所在epoll
{
	//创建客户套接字
	sockaddr_in	client_address;
	socklen_t len = sizeof client_address;
	while (1)
	{
		int client_fd = accept(fd, (struct  sockaddr*)&client_address, &len);
		if (client_fd == -1)
		{
			cout << getInfo() + " finish accepting " << " ( pid:" << pthread_self() << " ) " << endl;
			return;
		}
		Client_Socket* client_sock = new Client_Socket(client_fd, client_address, *epoll);
		cout << client_sock->getInfo() + " have connected successfully, IP: " + client_sock->getIP() + " , PORT: " + to_string(client_sock->getPort()) << " ( pid:" << pthread_self() << " ) " << endl;
	}
}
Listen_Socket::Listen_Socket(int host_port, Epoll& epoll, int max_con_num )
{
	//获取地址
	address.sin_family = AF_INET;
	address.sin_port = htons(host_port);
	address.sin_addr.s_addr = htonl(INADDR_ANY);;
	//创建套接字
	fd = socket(AF_INET, SOCK_STREAM, 0);
	errif(fd == -1, getInfo()+" create error");
	fcntl(fd, F_SETFL, O_NONBLOCK);		//设置IO非阻塞，accept()不再堵塞
	int opt = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void*)&opt, sizeof(opt)); //端口复用
	this->fd = fd;
	this->address = address;
	this->epoll = &epoll;
	cout << getInfo() + " create successfully , IP: " + getIP() + " , PORT: " + to_string(getPort()) << " ( pid:" << pthread_self() << " ) " << endl;
	//开始工作
	status = 1; //正在工作，具有可执行事件，常驻
	errif(bind(fd, (struct sockaddr*)&address, sizeof(address)) == -1, getInfo()+" bind address error");
	errif(listen(fd, max_con_num)== -1, getInfo()+" begin to listen error");
	current_event = EPOLLIN;
	mode = 0;
	epoll.bind(*this);
}
