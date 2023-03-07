#include <stdio.h> 
#include <signal.h> 
#ifndef _WIN32 
#include <unistd.h>
#include <stdlib.h> 
#include "cmdline.h"
#endif 
#include "global_var.h"
#include "version.h"
#include "ngx_acceptor.h"
#include "tcp_conn.h"
#include "log_def.h"

////////////////////////////////////////
bool		g_running = true;
eventloop	g_loop;
///////////////////////////////////////
#ifndef _WIN32 
void signal_handle(int);
#else 
BOOL WINAPI ConsoleHandler(DWORD cEvent);
#endif 
bool init();
void uninit();
void api_apt_cb(ngx_sock fd);
size_t msg_head_parse(void* data, size_t len);
void api_discon(int err, net_client_base* conn);
bool api_msg_process(net_client_base* conn, void* data, unsigned int len);
///////////////////////////////////////
int main(int argc, char* argv[])
{
#ifndef _WIN32 
	cmdline::parser a;
	a.add<std::string>("config", 'c', "configure file full name [Y]", false, "./gateway.cfg");
	a.add<int>("port", 'p', "port number", false, 2000, cmdline::range(1024, 65535));
	a.add<std::string>("logdir", 'l', "log output dir", false, "./");
	a.add("version", 'v', "project version");
	a.parse_check(argc, argv);
	if(a.exist("version"))
	{
		printf("%s version :	%s\n", APIGW_VERSION_NAME,  APIGW_VERSION);
		printf("%s compile timestamp: %s %s\n",APIGW_VERSION_NAME, __DATE__, __TIME__);
		trace_print("%s\n", __FUNCTION__);
		return 0;
	}
#else 
	WSAData wsd;
	if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0)
		return -1;

	if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)&ConsoleHandler, TRUE))
		return -1;
#endif


 		trace_print("%s\n", __FUNCTION__);
 		info_print("%s\n", __FUNCTION__);
 		error_print("%s\n", __FUNCTION__);


 		//
 #ifndef _WIN32 
 		signal(SIGPIPE, SIG_IGN);
 		signal(SIGTERM, signal_handle);
 #endif 
 		//
 		if(!init())
 		{
 			printf("%s init fail...\n", APIGW_VERSION_NAME);
 			return -1;
 		}
 		//
 		ngx_acceptor*  apt = new ngx_acceptor;
 #ifndef _WIN32 
 		if(NULL == apt || !apt->init(0, a.get<int>("port"), api_apt_cb))
 #else 
 		if (NULL == apt || !apt->init(0, 2000, api_apt_cb))
 #endif 
 		{
 			printf("net listen fail. port:%d\n", 2000);
 			return -1;
 		}
		//
 		g_loop.add(apt);	
 		while(g_running) 
			g_loop.loop(10);
 		//
 		uninit();	
 	 	printf("%s stop ...\n", APIGW_VERSION_NAME);
#ifdef _WIN32 
		WSACleanup();
#endif 
 	return 0;
}




bool init()
{
	return true;
}
//
void uninit()
{

}

void api_apt_cb(ngx_sock fd)
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


#ifndef _WIN32
void signal_handle(int ret)
{
	switch (ret)
	{
	case SIGINT:
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
#else 
BOOL WINAPI ConsoleHandler(DWORD cEvent)
{
	switch (cEvent)
	{
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
		MessageBox(NULL, L"CTRL+C", L"ÌבÊ¾", MB_OK);
		g_running = false;
		g_loop.wakeup();
		break;
	case CTRL_CLOSE_EVENT:
		break;
	case CTRL_LOGOFF_EVENT:
		break;
	case CTRL_SHUTDOWN_EVENT:
		break;
	default:
		break;
	}
	
	info_print("%d\n", cEvent);
	return TRUE;
}
#endif 