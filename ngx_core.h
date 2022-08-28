#ifndef _NGX_CORE_H_
#define _NGX_CORE_H_

#include "eventloop.h"
#include "ngx_log.h"
#include "Common.h"

#define HOST_IP_MAX			4
#define HOST_MAC_MAX		5
#define MAC_LENGTH_MAX		24

typedef struct ngx_core_s ngx_core_t;

struct ngx_core_s
{
	COMMONCFG				cfg;
	eventloop				*loop;
	ngx_log					*log;
	unsigned int			timeout;		//event loop ms;
	unsigned char			logoned;
	unsigned char			started;
	unsigned char			ip_cnt;
	unsigned char			ip_idx;
	unsigned int			host_ip[HOST_IP_MAX];		//DNS => host_ip
	unsigned short			port[HOST_IP_MAX];
	char					api_name[32];
	unsigned int			api_version;				//
	unsigned int			log_max_size;				//
	char					mac[HOST_MAC_MAX][MAC_LENGTH_MAX];		//mac addr
};

void parse_url(ngx_core_t  *core);

void get_mac(ngx_core_t	*core);

bool ngx_core_init(ngx_core_t *core);

void ngx_core_uninit(ngx_core_t *core);
#endif 