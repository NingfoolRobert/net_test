/**
 * @file TestDemo.h
 * @brief 
 * @author ningbf
 * @version 1.0.0
 * @date 2022-10-08
 */

#ifndef _TEST_DEMO_H_
#define _TEST_DEMO_H_ 

#include "BizUser.h"

class CTestDemo:
	public CBizUser
{
public:
	CTestDemo();
	virtual ~CTestDemo();
public:
	virtual bool OnMessage(unsigned int nMsgID, unsigned int nMsgNo, const char* pData, size_t len);
	
	virtual void OnHeartBeat();

	virtual void OnLogOn(const int ret);
	
	virtual void OnDisConnect();
	
	virtual void OnConnect();
public:
		
	
};


#endif 
