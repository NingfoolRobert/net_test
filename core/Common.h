#ifndef  _COMMON_H_
#define	 _COMMON_H_

#include <stdio.h>

typedef struct tagCommonCfg {
	char				url[128];			//
	unsigned char		auto_reconnect;		//default: 0,dont connect, 1:auto connect
	unsigned char		log_level;			// 1:trace,2:debug, 3:info,4:notice, 5:warn, 6:error, 7:fatal, 8:off log
	unsigned short		hearbeat_int;		//default:30
	unsigned int		userid;
	char				pwd[32];
	char				lisence[128];
}COMMONCFG;


#endif
