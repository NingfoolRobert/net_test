#include "../rx_bsd_mcbc.h"
#include "../tx_bsd_mcbc.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {

    using namespace detail::io;

    net::rx_bsd_mcbc bsd_rx;

    net::rx_bsd_mcbc_cfg_t cfg_rx;
    cfg_rx.pair.dst.set_ip("192.168.244.130");
    cfg_rx.pair.dst.set_port(3000);

    bsd_rx.set_opt(cfg_rx);

    if (!bsd_rx.open()) {
        printf("bsd rx open.");
        return -1;
    }
    //
    net::tx_bsd_mcbc bsd_tx;
    net::tx_bsd_mcbc_cfg_t cfg_tx;
    cfg_tx.pair.dst.set_ip("192.168.244.130");
    cfg_tx.pair.dst.set_port(3000);

    bsd_tx.set_opt(cfg_tx);
    if(!bsd_tx.open()){
      printf("bsd tx open fail.");
      return  -1;
    }

    char szTmp[1024] = { 0 };
    while(true) {
      
      int sended = bsd_tx.send("hello world", 12);
      printf("send, %d\n", sended);
      
      auto ret = bsd_rx.recv(szTmp, 256);
      printf("ret:%d, %s\n", ret, szTmp);
      
      usleep(1 * 1000 * 1000);
    }
    
    

    return 0;
}
