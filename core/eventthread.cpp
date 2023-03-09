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

void eventthread::run(size_t timeout /*= 10*/)
{
	_thr.reset(new std::thread([this, timeout]() {
		_flag = true;
		eventloop lp;
		_loop = &lp;
		_loop->loop(timeout);
		_loop = nullptr;
	}));
}

void eventthread::stop()
{
	if (nullptr == _loop)
		return;
	//
	_loop->stop();
	//
	if(_thr->joinable())
		_thr->join();
}
