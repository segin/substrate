/*
 * svr4_streams.h - the STREAMS transport interface System V programs
 * reach the network through, over substrate's sockets (svr4_streams.c).
 */
#ifndef _EXEC_PERSO_SVR4_SVR4_STREAMS_H
#define _EXEC_PERSO_SVR4_SVR4_STREAMS_H

#include <stdint.h>

#include <exec/perso/sysv386.h>

/* The devices, by the names programs open. */
#define SVR4_DEV_TCP        "/dev/tcp"
#define SVR4_DEV_UDP        "/dev/udp"
#define SVR4_DEV_TICOTS     "/dev/ticots"
#define SVR4_DEV_TICOTSORD  "/dev/ticotsord"
#define SVR4_DEV_TICLTS     "/dev/ticlts"
#define SVR4_DEV_SPX        "/dev/spx"
/* The X server's local transports: /dev/X/server.N (a named stream) and
 * /dev/XNR (the request end of a stream pipe pair). */
#define SVR4_DEV_X_SERVER   "/dev/X/server."
#define SVR4_DEV_X_PREFIX   "/dev/X"
/* Where substrate's X server listens. */
#define SVR4_X_SOCKET       "/tmp/.X11-unix/X"
#define SVR4_X_SOCKET_FMT   SVR4_X_SOCKET "%u"

/* ioctl(2) requests on a stream: 'S' << 8 | n (<sys/stropts.h>). */
#define SVR4_I_NREAD      0x5301
#define SVR4_I_PUSH       0x5302
#define SVR4_I_POP        0x5303
#define SVR4_I_LOOK       0x5304
#define SVR4_I_FLUSH      0x5305
#define SVR4_I_SRDOPT     0x5306
#define SVR4_I_GRDOPT     0x5307
#define SVR4_I_STR        0x5308
#define SVR4_I_SETSIG     0x5309
#define SVR4_I_GETSIG     0x530a
#define SVR4_I_FIND       0x530b
#define SVR4_I_PEEK       0x530f
#define SVR4_I_FDINSERT   0x5310
#define SVR4_I_SENDFD     0x5311
#define SVR4_I_SWROPT     0x5313
#define SVR4_I_GWROPT     0x5314
#define SVR4_I_SETCLTIME  0x5320
#define SVR4_I_GETCLTIME  0x5321
#define SVR4_I_CANPUT     0x5322
#define SVR4_FMNAMESZ     8

/* timod's, 'T' << 8 | n (<sys/timod.h>), inside I_STR or on their own.
 * Release 3 numbered the first four from 100, Release 4 from 140. */
#define SVR3_TI_GETINFO      0x5464
#define SVR3_TI_OPTMGMT      0x5465
#define SVR3_TI_BIND         0x5466
#define SVR3_TI_UNBIND       0x5467
#define SVR4_TI_GETINFO      0x548c
#define SVR4_TI_OPTMGMT      0x548d
#define SVR4_TI_BIND         0x548e
#define SVR4_TI_UNBIND       0x548f
#define SVR4_TI_GETMYNAME    0x5490
#define SVR4_TI_GETPEERNAME  0x5491
#define SVR4_TI_SETMYNAME    0x5492
#define SVR4_TI_SETPEERNAME  0x5493

/* sockmod's, 'I' << 8 | n (<sys/sockmod.h>), inside I_STR. */
#define SVR4_SI_GETUDATA     0x4965
#define SVR4_SI_SHUTDOWN     0x4966
#define SVR4_SI_LISTEN       0x4967
#define SVR4_SI_SETMYNAME    0x4968
#define SVR4_SI_SETPEERNAME  0x4969
#define SVR4_SI_GETINTRANSIT 0x496a

/* The BSD-numbered requests Release 4 kept for sockets. */
#define SVR4_FIONREAD     0x4004667f
#define SVR4_FIONBIO      0x8004667e

/* Transport Provider Interface primitives (<sys/tihdr.h>). */
#define SVR4_T_CONN_REQ      0
#define SVR4_T_DISCON_REQ    2
#define SVR4_T_DATA_REQ      3
#define SVR4_T_EXDATA_REQ    4
#define SVR4_T_INFO_REQ      5
#define SVR4_T_BIND_REQ      6
#define SVR4_T_UNBIND_REQ    7
#define SVR4_T_UNITDATA_REQ  8
#define SVR4_T_OPTMGMT_REQ   9
#define SVR4_T_ORDREL_REQ    10
#define SVR4_T_CONN_CON      12
#define SVR4_T_DISCON_IND    13
#define SVR4_T_DATA_IND      14
#define SVR4_T_INFO_ACK      16
#define SVR4_T_BIND_ACK      17
#define SVR4_T_ERROR_ACK     18
#define SVR4_T_OK_ACK        19
#define SVR4_T_OPTMGMT_ACK   22
#define SVR4_T_ORDREL_IND    23

/* Service types and states an endpoint reports. */
#define SVR4_T_COTS       1
#define SVR4_T_COTS_ORD   2
#define SVR4_T_CLTS       3
#define SVR4_TS_UNBND     0
#define SVR4_TS_IDLE      3
#define SVR4_TS_DATA_XFER 9

/* t_errno values (<sys/tiuser.h>). */
#define SVR4_TBADADDR     1
#define SVR4_TOUTSTATE    6
#define SVR4_TSYSERR      8
#define SVR4_TNOTSUPPORT  18

/* getmsg(2)'s results and flags. */
#define SVR4_MORECTL      1
#define SVR4_MOREDATA     2
#define SVR4_RS_HIPRI     1

/* poll(2) event bits where they are not substrate's. */
#define SVR4_POLLRDNORM   0x0040
#define SVR4_POLLRDBAND   0x0080
#define SVR4_POLLWRBAND   0x0100

/* The errnos a transport reports, as Release 4 numbers them. */
#define SVR4_ENOTSOCK        95
#define SVR4_EPROTO          71
#define SVR4_EAFNOSUPPORT    124
#define SVR4_EADDRINUSE      125
#define SVR4_EADDRNOTAVAIL   126
#define SVR4_ENETDOWN        127
#define SVR4_ENETUNREACH     128
#define SVR4_ECONNABORTED    130
#define SVR4_ECONNRESET      131
#define SVR4_ENOBUFS         132
#define SVR4_EISCONN         133
#define SVR4_ENOTCONN        134
#define SVR4_ECONNREFUSED    146
#define SVR4_EHOSTUNREACH    148
#define SVR4_EALREADY        149
#define SVR4_EINPROGRESS     150

struct svr4_strbuf {
    int32_t  maxlen;
    int32_t  len;
    uint32_t buf;
};

struct svr4_strioctl {
    int32_t  ic_cmd;
    int32_t  ic_timout;
    int32_t  ic_len;
    uint32_t ic_dp;
};

struct svr4_strfdinsert {
    struct svr4_strbuf ctlbuf;
    struct svr4_strbuf databuf;
    int32_t flags;
    int32_t fildes;
    int32_t offset;
};

struct svr4_iovec {
    uint32_t iov_base;
    int32_t  iov_len;
};

/*
 * open(2) of `path`: 1 with the call's result in *result if it names one
 * of the devices, 0 if it is for the filesystem.
 */
int svr4_streams_open(const char *path, int flags, int64_t *result);
/* ioctl(2): 1 with *result set if the request is a stream's. */
int svr4_streams_ioctl(struct sysv386_frame *f, int64_t *result);
/* close(2) is about to happen to `fd`. */
void svr4_streams_close(int fd);
/* read(2): 1 with *result set if `fd` is a stream. */
int svr4_streams_read(int fd, uint32_t buf, uint32_t len, int64_t *result);

int64_t svr4_sys_getmsg(struct sysv386_frame *f);
int64_t svr4_sys_putmsg(struct sysv386_frame *f);
int64_t svr4_sys_poll(struct sysv386_frame *f);
int64_t svr4_sys_readv(struct sysv386_frame *f);
int64_t svr4_sys_writev(struct sysv386_frame *f);

/* A substrate network errno as Release 4 numbers it (others unchanged). */
int svr4_net_errno(int native);

#endif /* _EXEC_PERSO_SVR4_SVR4_STREAMS_H */
