#ifndef _IMPL_H_
#define _IMPL_H_


#include "BizUser.h"
#include "ngx_core.h"

#define HOST_IP_MAX			4
#define HOST_MAC_MAX		5
#define MAC_LENGTH_MAX		24



typedef  void(*PTIMERCALLBACK)(void* param);

struct CBizUser::Impl {
	ngx_core_t				*core;
	net_client_base			*conn;			//
	void					*biz_user;		//
	unsigned int			logoned;		//
	
	PTIMERCALLBACK			timer_cb;
	PDISCONNCALLBACK		dis_conn_cb;
	PNETMSGCALLBACK			msg_cb;
	PMSGLENPARSEFUNC		msg_head_fnc;
	///////////////////////////////////////////
	Impl();

	~Impl();

	bool	init();

	void	uninit();

	bool	connect();

	ngx_log*  get_log();

	bool	logon();

	void	async(net_client_base* conn);

	void	unasync(net_client_base* conn);

	
};


#endif