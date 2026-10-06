#include "cseries.h"
#include <xtl.h>
#include "bungie_net/network/transport.h"
#include "networking/telnet_console.h"
void n3ds_log(const char *message);
int n3ds_socket_error_tests(void);
short transport_server_initialize(void);
#define CHECK(e) do { if(!(e)) { n3ds_log("NETWORK FAIL: " #e); return 1; } } while(0)
int halo_engine_network_tests(void)
{
    fd_set set;
    XNKID id={{0}};
    XNKEY key={{0}};
    XNADDR address={0};
    IN_ADDR result;
    u_long nonblocking=1;
    unsigned int i;
    CHECK(!n3ds_socket_error_tests());
    for(i=0;i<2;++i) {
        CHECK(transport_initialize()==_transport_error_not_initialized);
        CHECK(!transport_network_available());
        telnet_console_initialize(); telnet_console_process();
        telnet_console_print("Offline telnet test"); telnet_console_dispose();
        CHECK(transport_server_initialize()==_transport_error_not_initialized);
        CHECK(transport_dispose()==_transport_error_not_initialized);
    }
    CHECK(XNetGetEthernetLinkStatus()==0);
    CHECK(XNetRegisterKey(&id,&key)==WSANOTINITIALISED);
    CHECK(XNetUnregisterKey(&id)==WSANOTINITIALISED);
    result.s_addr=0x12345678;
    CHECK(XNetXnAddrToInAddr(&address,&id,&result)==WSANOTINITIALISED);
    CHECK(result.s_addr==0x12345678);
    FD_ZERO(&set); CHECK(!__WSAFDIsSet(7,&set));
    FD_SET(7,&set); FD_SET(31,&set); FD_SET(7,&set);
    CHECK(set.fd_count==2 && __WSAFDIsSet(7,&set) && __WSAFDIsSet(31,&set));
    CHECK(!__WSAFDIsSet(0,&set)); FD_CLR(7,&set);
    CHECK(!__WSAFDIsSet(7,&set) && __WSAFDIsSet(31,&set));
    for(i=0;i<FD_SETSIZE;++i) set.fd_array[i]=i+100;
    set.fd_count=FD_SETSIZE;
    CHECK(__WSAFDIsSet(100+FD_SETSIZE-1,&set));
    set.fd_count=FD_SETSIZE+1; CHECK(!__WSAFDIsSet(100,&set));
    CHECK(!__WSAFDIsSet(100,NULL));
    CHECK(ioctlsocket(INVALID_SOCKET,FIONBIO,&nonblocking)==SOCKET_ERROR);
    CHECK(WSAGetLastError()==WSAENOTSOCK && nonblocking==1);
    CHECK(ioctlsocket(INVALID_SOCKET,FIONBIO,NULL)==SOCKET_ERROR && WSAGetLastError()==WSAEFAULT);
    CHECK(ioctlsocket(INVALID_SOCKET,0,&nonblocking)==SOCKET_ERROR && WSAGetLastError()==WSAEINVAL);
    n3ds_log("PASS: offline transport stays unavailable across repeated startup/server/dispose, rejects Xbox keys/address translation, preserves Winsock fd-set layout and propagates native ioctl failures");
    return 0;
}
