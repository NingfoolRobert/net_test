/**
 * @file acc_bsd_tcp.h
 * @brief
 * @author bfning
 * @version 0.1
 * @date 2025-03-09
 */
#pragma once

namespace net {

class acc_bsd_tcp {
public:
  
  bool  open();

  void  close();

  void* accept();
};
}  // namespace net
