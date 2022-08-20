#ifndef _BIZ_USER_H_
#define _BIZ_USER_H_


#include "Common.h"

#if defined _WIN32 || defined __CYGWIN__
#ifdef _EXPORTING// genreate dll define
	#ifdef  __GNUC__ 
		#define  DLL_PUBLIC __attribute__((dllexport))
	#else 
		#define  DLL_PUBLIC __declspec(dllexport)
	#endif 
#else 
	#ifdef __GNUC__
		#define DLL_PUBLIC __attribute__((dllimport))
	#else 
		#define DLL_PUBLIC __declspec(dllimport)
	#endif 
#endif 
#else 
	#if __GNUC__ >= 4
		#define DLL_PUBLIC __attribute__((visibility("default")))
		#define DLL_HIDDEN __attribute__((visibility("hidden")))
	#else 
		#define DLL_PUBLIC
		#define DLL_HIDDEN
	#endif 
#endif 


class DLL_PUBLIC CBizUser
{
public:
	struct Impl;
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
	virtual void  OnLogon(const int ret) {}
	//
	virtual void  OnHeartBeat() {}
public:
	bool	start(const COMMONCFG&  cfg);

	void	stop();
	
	void	reconnect();

	bool	send_message(unsigned int nMsgID, unsigned int nMsgNo, char*  pData, unsigned int nMsgLen);

private:
	Impl*	_impl;
};


#endif

