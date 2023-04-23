#ifndef _SIMPLE_TOKEN_H_
#define _SIMPLE_TOKEN_H_

#include <stdio.h>
#include <stdint.h>



class simple_token
{
public:
	explicit simple_token(uint64_t rate);
	~simple_token();
	
	void wait();

private:
	uint64_t		_rate;
	uint64_t		_time_point;
};


#endif


