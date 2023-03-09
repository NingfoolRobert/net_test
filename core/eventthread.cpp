#include "eventthread.h"
#include <thread>


eventthread::eventthread():
	_loop(nullptr), 
	_flag(false)
{
	//_thr.reset(new std::thread(&eventthread::run, this));
}
eventthread::~eventthread()
{
}

		
void eventthread::run()
{
	_thr.reset(new std::thread([this]() {
		_flag = true;
		eventloop loop;
		_loop = &loop;
		while (_flag) _loop->loop(10);
		_loop = nullptr;
	}));
}

void eventthread::stop()
{
	if (nullptr == _loop)
		return;
	//
	_flag = false;	
	_loop->wakeup();
	//
	if(_thr->joinable())
		_thr->join();
}
