#ifndef _BIZ_USER_H_
#define _BIZ_USER_H_


#include "Common.h"



class CBizUser
{
public:
	CBizUser();
	virtual ~CBizUser();
public:
	//message arrive callback 
	virtual bool  OnMessage(unsigned int nMsgID, unsigned int nMsgNo, void* pData, unsigned int nMsgLen) { return true; };
	//net connect  callback, connect success
	virtual void  OnConnect(){}
	//net connection disconnect callback
	virtual void  OnDisConnect(){}
	//log on result, 0:success, 1:faild
	virtual void  OnLogon(int ret) {};
	//
	virtual void  OnLogData(int err_level, const char*  log_data){}
public:
	bool	start(COMMONCFG*  cfg);

	void	stop();
	
	void	reconnect();

	bool	send_message(unsigned int nMsgID, unsigned int nMsgNo, char*  pData, unsigned int nMsgLen);
public:
	struct Impl;
	Impl*	_impl;
};


#endif

