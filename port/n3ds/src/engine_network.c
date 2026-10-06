/* Xbox network capability boundary for the offline Campaign port.
 * This is not a multiplayer implementation. No identity/key is fabricated. */
#include "cseries.h"
#include <xtl.h>

int n3ds_socket_set_nonblocking(unsigned int socket,unsigned int enabled);
int n3ds_socket_bad_argument(int bad_pointer);

DWORD WSAAPI XNetGetEthernetLinkStatus(void) { return 0; }
INT WSAAPI XNetRegisterKey(const XNKID *id,const XNKEY *key)
{ (void)id; (void)key; return WSANOTINITIALISED; }
INT WSAAPI XNetUnregisterKey(const XNKID *id)
{ (void)id; return WSANOTINITIALISED; }
INT WSAAPI XNetXnAddrToInAddr(const XNADDR *address,const XNKID *id,IN_ADDR *result)
{ (void)address; (void)id; (void)result; return WSANOTINITIALISED; }

/* Winsock fd_set is a count plus an array, not libctru's bitset. */
int PASCAL __WSAFDIsSet(SOCKET socket,fd_set *set)
{
    unsigned int i;
    if(!set || set->fd_count>FD_SETSIZE) return 0;
    for(i=0;i<set->fd_count;++i) if(set->fd_array[i]==socket) return 1;
    return 0;
}
int WSAAPI ioctlsocket(SOCKET socket,long command,u_long *argument)
{
    if(!argument) return n3ds_socket_bad_argument(1);
    if((unsigned long)command!=(unsigned long)FIONBIO)
        return n3ds_socket_bad_argument(0);
    return n3ds_socket_set_nonblocking((unsigned int)socket,*argument!=0);
}
