#include "eventloop.h"
#include <vector> 
#include <mutex> 

#ifdef _BUSY_LOOP_
int  eventloop::loop(int  timeout)
{
	std::vector<net_io*> conns;
	fd_set  rd_fds, wt_fds;
	//
	while(_running)
	{
		process_task();
		process_timer();
		FD_ZERO(&rd_fds);
		FD_ZERO(&wt_fds);
#ifdef _WIN32 
		FD_SET(_wake_recv->_fd, &rd_fds);
#else
		FD_SET(_wake_fd, &rd_fds);
#endif 
		{
			std::unique_lock<spinlock> _(_lck);
			for (auto it = _conns.begin(); it != _conns.end(); ++it)
			{
				auto pConn = *it;
				conns.push_back(pConn); pConn->add_ref();
				FD_SET(pConn->_fd, &rd_fds);
				//
				if (pConn->wait_sndmsg_size())
					FD_SET(pConn->_fd, &wt_fds);
			}
		}
		//	
#ifdef _WIN32 
			if (FD_ISSET(_wake_recv->_fd, &rd_fds)){
#else 
			if (FD_ISSET(_wake_fd, &rd_fds)){
#endif 
				handle_read();
			}
			//
			for (auto i = 0u; i < conns.size(); ++i)
			{
				auto pConn = conns[i];
				if (FD_ISSET(pConn->_fd, &rd_fds))
					pConn->OnRead();
				if (FD_ISSET(pConn->_fd, &wt_fds))
					pConn->OnSend();
				//
				pConn->release();
			}
		//
		conns.clear();
	}	
	return 0;
}


#endif //_BUSY_LOOP_
