#ifndef  _COMMON_H_
#define	 _COMMON_H_

#include <stdio.h>

typedef struct tagCommonCfg {
	char				ip[16];
	unsigned short		port;
	unsigned char		auto_reconnect;  //Ĭ�ϲ��Զ�����:0, 1:�Զ�����
	unsigned char		reserver;
	unsigned int		userid;
	char				pwd[32];
	char				lisence[128];
}COMMONCFG;


#endif
