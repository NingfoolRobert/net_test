#include "eventthread.h"
#include <thread>
#include <sstream>



eventthread::eventthread():_loop(nullptr){
}
eventthread::~eventthread()
{
}

void eventthread::run(size_t timeout /*= 10*/)
{
	_thr.reset(new std::thread([this, timeout]() {
		_tid = std::this_thread::get_id();
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

int eventthread::get_tid(){
	std::stringstream ss;
	ss << _tid;
	std::string s = ss.str();
	return atoi(s.c_str());
}
