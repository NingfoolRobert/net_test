#include "threadpool.h"
#include <algorithm>
#include <unistd.h>


threadpool::threadpool(){

}

threadpool::~threadpool(){

}
		
void threadpool::run(int size/* = 2*/, int timeout/* = 10*/)
{
	for(auto i = 0; i < size; ++i){
		_ths.push_back(std::move(new eventthread));
		_ths[i]->run(timeout);
	}	
}

void threadpool::stop(){
	for_each(_ths.begin(), _ths.end(), [](const eventthread& th){
			th->stop();
			});
}
