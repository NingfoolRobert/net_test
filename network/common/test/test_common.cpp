#include "../net_common.h"
#include <assert.h>
#include <gtest/gtest.h>
#include <iostream>

TEST(net_common, net_common_test) {
  
    net::common::addr_t addr;
    addr.set_ip("192.168.1.1");
    addr.set_port(3000);
    
    auto sock_addr = addr.to_sockaddr();
    
    net::common::addr_t addr1;
    addr1.from_sockaddr(sock_addr);

    EXPECT_EQ(addr1.port, addr.port);
    EXPECT_STREQ(addr.ip, addr1.ip);
    EXPECT_STREQ(addr.to_string().c_str(),  "192.168.1.1:3000");
    
    net::common::ip_pair p;
    p.if_idx = 2;
    std::string  eth = p.get_if_name();
    
    p.set_if_name(net::util::if_index_to_name(2).c_str());
    EXPECT_EQ(p.if_idx,  2);
}




//int main() {
//  
//  net::common::addr_t  addr; 
//  addr.set_ip("192.168.1.1");
//  addr.port = 30000;
//  
//  
//  auto sock_addr = addr.to_sockaddr();
//  
//  net::common::addr_t  addr1;
//  addr1.from_sockaddr(sock_addr);
//  printf("%d\n",  sock_addr.sin_port);
//  
//  printf("addr:%s\n", addr.to_string().c_str());
//  printf("addr1:%s\n", addr1.to_string().c_str());
//
//  
//  net::common::ip_pair  p;
//
//  p.set_if_name("ens33");
//  
//  printf("%s\n", p.get_if_name().c_str());
//  
//  p.set_if_name("192.168.244.130");
//
//  printf("%s\n", net::util::if_index_to_name(2).c_str());
//
//  printf("%s\n", net::util::if_name_to_ipv4("ens33").c_str());
//  
//  auto flag = net::util::is_multicast_addr("239.1.1.1"); 
//  
//  printf("%d\n", flag);
//  return 0;
//}
