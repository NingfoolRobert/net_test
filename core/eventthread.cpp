#include "eventthread.h"
#include <thread>


eventthread::eventthread():_loop(nullptr){
}
eventthread::~eventthread()
{
}

void eventthread::run(size_t timeout /*= 10*/)
{
	_thr.reset(new std::thread([this, timeout]() {
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
