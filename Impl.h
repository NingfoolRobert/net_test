#ifndef _IMPL_H_
#define _IMPL_H_


#include "BizUser.h"
#include "eventloop.h"

#define HOST_IP_MAX			4
#define HOST_MAC_MAX		5
#define MAC_LENGTH_MAX		24

typedef  void(*PTIMERCALLBACK)(void* param);

struct CBizUser::Impl {
	COMMONCFG				cfg;
	eventloop				*loop;
	net_client_base*		conn;
	unsigned int			timeout;		//event loop ms;
	unsigned char			logoned;
	unsigned char			started;
	unsigned char			ip_cnt;
	unsigned char			ip_idx;
	unsigned int			host_ip[HOST_IP_MAX];		//DNS => host_ip
	unsigned short			port;
	char					api_name[30];
	char					mac[HOST_MAC_MAX][MAC_LENGTH_MAX];		//mac addr

	PTIMERCALLBACK			timer_cb;
	PDISCONNCALLBACK		dis_conn_cb;
	PNETMSGCALLBACK			msg_cb;
	PMSGLENPARSEFUNC		msg_head_fnc;
	///////////////////////////////
	Impl() :loop(nullptr), conn(nullptr), logoned(0), started(0), timeout(10), ip_cnt(0), ip_idx(0) {

		if (cfg.hearbeat_int == 0) cfg.hearbeat_int = 30;
		if (cfg.log_level == 0) cfg.log_level = 4;
	}
	//
	~Impl() {
		if (loop)
		{
			delete loop;
			loop = nullptr;
		}
		if (conn)
		{
			delete conn;
			conn = nullptr;
		}
	}

	bool	init();

	bool	connect();

	void	parse_url();

	void	get_local_mac();

	void	get_local_ip();
};


#endif
