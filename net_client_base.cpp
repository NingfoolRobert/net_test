#include "net_client_base.h"


net_client_base::net_client_base():_break_timestamp(0)
{
#if _WIN32 
	_fd = INVALID_SOCKET;
#else 
	_fd = -1;
#endif 
}


net_client_base::~net_client_base()
{
	close();
}
#if _WIN32 
SOCKET net_client_base::create(int domain /*= AF_INET*/, int socket_type /*= SOCK_STREAM*/, int protocol_type /*= IPPROTO_IP*/)
#else 
int	 net_client_base::create(int domain /*= AF_INET*/, int socket_type /*= SOCK_STREAM*/, int protocol_type /*= IPPROTO_IP*/)
#endif 
{
	_fd = ::socket(domain, socket_type, protocol_type);
	return _fd;
}

bool net_client_base::connect(unsigned int host_ip, unsigned short port)
{
	struct sockaddr_in  svr_addr;
	memset(&svr_addr, 0, sizeof(struct sockaddr_in));
	
	svr_addr.sin_family = AF_INET;
#if _WIN32 
	svr_addr.sin_addr.S_un.S_addr = htonl(host_ip);
#else 
	svr_addr.sin_addr.s_addr = htonl(host_ip);
#endif
	svr_addr.sin_port = htons(port);
	if(::connect(_fd, (struct sockaddr*)&svr_addr, sizeof(svr_addr)) < 0)
		return false;
	//
	return true;
}

bool net_client_base::set_tcp_nodelay()
{
	int enable = 1;
	setsockopt(_fd, IPPROTO_IP, TCP_NODELAY, (char*)&enable, sizeof(enable));
	return true;
}

bool net_client_base::set_nio(int mode /*= 1*/)
{
#if _WIN32
	ioctlsocket(_fd, FIONBIO, (u_long*)&mode);
#else
	int flags = fcntl(_fd, F_GETFL, 0);
	fcntl(_fd, F_SETFL, mode == 1? O_NONBLOCK | flags : ~O_NONBLOCK & flags)
#endif 
	return true;
}

void net_client_base::close()
{
#if _WIN32
	closesocket(_fd);
	_fd = INVALID_SOCKET;
#else 
	::close(_fd);
	_fd = -1;
#endif 
}

int net_client_base::send_msg(const char* pData, unsigned int nMsgLen)
{
	if (_break_timestamp)
		return 0;
	//
	int sended_len = ::send(_fd, pData, nMsgLen, 0);
	return sended_len;
}
