#include <csignal>
#include <cstddef>
#include <stdio.h> 
#include <signal.h> 
#include <stdlib.h> 
#include <unistd.h>
#include "global_var.h"
#include "version.h"
#include "cmdline.h"
#include "ngx_acceptor.h"
#include "tcp_conn.h"

////////////////////////////////////////
bool		g_running = true;
eventloop	g_loop;
///////////////////////////////////////
void signal_handle(int);
bool init();
void uninit();
void api_apt_cb(int fd);
size_t msg_head_parse(void* data, size_t len);
void api_discon(int err, net_client_base* conn);
bool api_msg_process(net_client_base* conn, void* data, unsigned int len);
///////////////////////////////////////
int main(int argc, char* argv[])
{
	cmdline::parser a;
	a.add<std::string>("config", 'c', "configure file full name [Y]", false, "./gateway.cfg");
	a.add<int>("port", 'p', "port number", false, 2000, cmdline::range(1024, 65535));
	a.add<std::string>("logdir", 'l', "log output dir", false, "./");
	a.add("version", 'v', "project version");
	a.parse_check(argc, argv);
	if(a.exist("version"))
	{
		printf("gateway version :	%s\n", APIGW_VERSION);
		printf("gateway compile timestamp: %s\n", APIGW_COMPILE_TIME);
		return 0;
	}
	//
	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, signal_handle);
	//
	if(!init())
	{
		printf("%s init fail...\n", APIGW_VERSION_NAME);
		return -1;
	}
	//
	ngx_acceptor*  apt = new ngx_acceptor;
	if(NULL == apt || !apt->init(0, a.get<int>("port"), api_apt_cb))
	{
		printf("net listen fail. port:%d\n", a.get<int>("port"));
		return -1;
	}
	g_loop.add(apt);	
	while(g_running) g_loop.loop(10);
	//
	uninit();	
	printf("%s stop ...\n", APIGW_VERSION_NAME);
	return 0;
}

void signal_handle(int ret)
{
	switch(ret)
	{
		case SIGTERM:		//exit 
			g_running = false;
			g_loop.wakeup();
			break;
		case SIGSEGV:
			printf("\n %s core dump...\n", APIGW_VERSION_NAME);
			break;
		default:
			break;
	}
}

bool init()
{
	return true;
}
//
void uninit()
{

}

void api_apt_cb(int fd)
{
	tcp_conn* conn = new tcp_conn(24,msg_head_parse, api_msg_process, api_discon);	
	if(nullptr == conn)
	{
		printf("memory error.");
		return ;
	}
	//	
	conn->_fd = fd;
	conn->set_nio();
	conn->set_tcp_nodelay();
	conn->set_tcp_linger();
	conn->get_peer_name();
	g_loop.add(conn);
	char ip[16] = { 0 };
	printf("clit, ip:port=%s:%d\n", conn->get_ip(ip), conn->_port);
}
//
size_t msg_head_parse(void* data, size_t len)
{
	if(len < 24)
		return 24;
	return len;
}
//
void api_discon(int err, net_client_base* conn)
{
	if(nullptr == conn)
		return ;
	//
	char ip[16] = { 0 };
	printf("disconnect  ip:port=%s:%d, err:%d\n", conn->get_ip(ip), conn->_port, err);
}

bool api_msg_process(net_client_base* conn, void* data, unsigned int len)
{
	return true;
}
