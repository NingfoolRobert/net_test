#include "eventthread.h"
#include <thread>


eventthread::eventthread():_flag(0)
{
	_thr.reset(new std::thread(&eventthread::run, this));
}
eventthread::~eventthread()
{
}

		
void eventthread::run()
{
	_flag = 1;
	eventloop loop;
	_loop = loop;	
	//
	while(_flag)
		_loop.loop(10);	
}

void eventthread::stop()
{
	_flag = false;	
	_loop.wakeup();
	_thr->join();
}
