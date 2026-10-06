/* libctru reports newlib errno values; original transport code branches on
 * Xbox Winsock numbers. This adapts error reporting, not socket initialization,
 * Xbox network identity, sockaddr layouts or multiplayer protocols. */
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <stdio.h>
void n3ds_log(const char *message);

int n3ds_socket_bad_argument(int bad_pointer)
{ errno=bad_pointer?EFAULT:EINVAL; return -1; }
int n3ds_socket_set_nonblocking(unsigned int socket,unsigned int enabled)
{
    int flags=fcntl((int)socket,F_GETFL);
    if(flags<0) return -1;
    return fcntl((int)socket,F_SETFL,enabled?(flags|O_NONBLOCK):(flags&~O_NONBLOCK));
}

int WSAGetLastError(void)
{
    switch(errno) {
    case 0:return 0;
    case EINTR:return 10004;
    case EBADF:return 10009;
    case EACCES:return 10013;
    case EFAULT:return 10014;
    case EINVAL:return 10022;
    case EMFILE:return 10024;
    case EAGAIN:return 10035;
    case EINPROGRESS:return 10036;
    case EALREADY:return 10037;
    /* libctru soc_get_fd uses ENODEV for an unallocated descriptor. */
    case ENODEV:case ENOTSOCK:return 10038;
    case EDESTADDRREQ:return 10039;
    case EMSGSIZE:return 10040;
    case EPROTOTYPE:return 10041;
    case ENOPROTOOPT:return 10042;
    case EPROTONOSUPPORT:return 10043;
#ifdef ESOCKTNOSUPPORT
    case ESOCKTNOSUPPORT:return 10044;
#endif
    case ENOTSUP:case EOPNOTSUPP:return 10045;
    case EPFNOSUPPORT:return 10046;
    case EAFNOSUPPORT:return 10047;
    case EADDRINUSE:return 10048;
    case EADDRNOTAVAIL:return 10049;
    case ENETDOWN:return 10050;
    case ENETUNREACH:return 10051;
    case ENETRESET:return 10052;
    case ECONNABORTED:return 10053;
    case ECONNRESET:return 10054;
    case ENOMEM:case ENOBUFS:return 10055;
    case EISCONN:return 10056;
    case ENOTCONN:return 10057;
    case EPIPE:return 10058;
#ifdef ESHUTDOWN
    case ESHUTDOWN:return 10058;
#endif
    case ETOOMANYREFS:return 10059;
    case ETIMEDOUT:return 10060;
    case ECONNREFUSED:return 10061;
    case EHOSTDOWN:return 10064;
    case EHOSTUNREACH:return 10065;
    default:return 10107; /* WSASYSCALLFAILURE: never turn an unknown error into success. */
    }
}

int n3ds_socket_error_tests(void)
{
    /* Core categories used by original write_endpoint/disconnect_endpoint. */
    static const int cases[][2]={{0,0},{EAGAIN,10035},{EINPROGRESS,10036},
        {ECONNRESET,10054},{ECONNABORTED,10053},{ETIMEDOUT,10060},
        {ENOTCONN,10057},{EPIPE,10058},{ENOBUFS,10055},{EINVAL,10022},
        {ENODEV,10038},{ENOTSOCK,10038},{EACCES,10013},{EHOSTUNREACH,10065},
        {0x7fffffff,10107}};
    int saved=errno;
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        errno=cases[i][0];
        if(WSAGetLastError()!=cases[i][1] || errno!=cases[i][0]) return 1;
    }
    /* These exercise the real libctru failure path before any SOC IPC/network
     * operation: soc_get_fd rejects -1 locally. */
    if(send(-1,"x",1,0)!=-1 || errno!=ENODEV || WSAGetLastError()!=10038) return 1;
    if(closesocket(-1)!=-1 || errno!=ENODEV || WSAGetLastError()!=10038) return 1;
    errno=saved;
    n3ds_log("PASS: native socket errno translates to Xbox failure categories without clearing errors; actual invalid send/close reject locally");
    return 0;
}
