#ifndef  _COMMON_H_
#define	 _COMMON_H_

#include <stdio.h>

typedef struct tagCommonCfg {
	char				ip[16];
	unsigned short		port;
	unsigned char		auto_reconnect;  //默认不自动重连:0, 1:自动重连
	unsigned char		reserver;
	unsigned int		userid;
	char				pwd[32];
	char				lisence[128];
}COMMONCFG;


#endif
