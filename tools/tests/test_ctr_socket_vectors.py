#!/usr/bin/env python3
"""Check the production SOC vector adapter with short transfers and broken peers."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#define recv fake_recv
#define send fake_send
#define getsockopt fake_getsockopt
#define recvmsg ctr_recvmsg
#define sendmsg ctr_sendmsg
#define socketpair ctr_socketpair
#define readv ctr_readv
#define writev ctr_writev
#include "platform/ctr/asio/sys/socket.c"
#include "platform/ctr/asio/sys/uio.c"
#include <assert.h>
#include <string.h>
static ssize_t replies[4];
static int calls, socket_type=SOCK_STREAM, fault=EPIPE;
static char sent[32];
static size_t sent_length;
static void reset(ssize_t a,ssize_t b,ssize_t c) {
    replies[0]=a;replies[1]=b;replies[2]=c;calls=0;sent_length=0;errno=0;
}
int fake_getsockopt(int fd,int level,int opt,void *value,socklen_t *len) {
    assert(fd==7 && level==SOL_SOCKET && opt==SO_TYPE && *len==sizeof(int));
    *(int *)value=socket_type;return 0;
}
ssize_t fake_send(int fd,const void *data,size_t length,int flags) {
    (void)flags;assert(fd==7 && calls<3);ssize_t n=replies[calls++];
    if(n<0) {errno=fault;return n;}
    assert((size_t)n<=length && sent_length+(size_t)n<=sizeof(sent));
    memcpy(sent+sent_length,data,(size_t)n);sent_length+=(size_t)n;return n;
}
ssize_t fake_recv(int fd,void *data,size_t length,int flags) {
    (void)flags;assert(fd==7 && calls<3);ssize_t n=replies[calls++];
    if(n<0) {errno=fault;return n;}
    assert((size_t)n<=length);memset(data,'a'+calls,(size_t)n);return n;
}
int main(void) {
    char a[]="abcd",b[]="efgh";
    struct iovec vectors[2]={{a,4},{b,4}};
    struct msghdr message={0};message.msg_iov=vectors;message.msg_iovlen=2;
    reset(2,4,0);assert(ctr_sendmsg(7,&message,0)==2 && calls==1);
    assert(sent_length==2 && memcmp(sent,"ab",2)==0); // No skipped tail.
    reset(4,4,0);assert(ctr_sendmsg(7,&message,0)==8 && calls==2);
    assert(memcmp(sent,"abcdefgh",8)==0);
    reset(4,-1,0);assert(ctr_sendmsg(7,&message,0)==4 && calls==2);
    reset(-1,0,0);assert(ctr_sendmsg(7,&message,0)==-1 && errno==EPIPE);
    reset(0,0,0);assert(ctr_sendmsg(7,&message,0)==0 && calls==1);
    reset(0,0,0);assert(ctr_recvmsg(7,&message,MSG_WAITALL)==0 && calls==1);
    reset(2,0,0);assert(ctr_recvmsg(7,&message,MSG_WAITALL)==2 && calls==2);
    reset(2,-1,0);assert(ctr_recvmsg(7,&message,MSG_WAITALL)==2 && calls==2);
    fault=EAGAIN;reset(-1,0,0);assert(ctr_recvmsg(7,&message,0)==-1 && errno==EAGAIN);
    reset(-100,0,0);assert(ctr_recvmsg(7,&message,0)==-1); // SDK Result normalized.
    reset(2,2,0);assert(ctr_recvmsg(7,&message,MSG_PEEK|MSG_WAITALL)==2 && calls==1);
    reset(4,4,0);assert(ctr_recvmsg(7,&message,MSG_WAITALL)==8 && calls==2);
    socket_type=SOCK_DGRAM;
    assert(ctr_sendmsg(7,&message,0)==-1 && errno==ENOTSUP);
    assert(ctr_recvmsg(7,&message,0)==-1 && errno==ENOTSUP);
    socket_type=SOCK_STREAM;
    assert(ctr_sendmsg(7,NULL,0)==-1 && errno==EFAULT);
    message.msg_iov=NULL;assert(ctr_recvmsg(7,&message,0)==-1 && errno==EFAULT);
    message.msg_iov=vectors;vectors[0].iov_len=(size_t)SSIZE_MAX;
    assert(ctr_sendmsg(7,&message,0)==-1 && errno==EINVAL);
    vectors[0].iov_len=4;vectors[0].iov_base=NULL;
    assert(ctr_recvmsg(7,&message,0)==-1 && errno==EFAULT);
    vectors[0].iov_base=a;message.msg_controllen=1;
    assert(ctr_sendmsg(7,&message,0)==-1 && errno==ENOTSUP);
    assert(ctr_socketpair(0,0,0,NULL)==-1 && errno==ENOTSUP);
    assert(ctr_readv(7,vectors,2)==-1 && errno==ENOTSUP);
    assert(ctr_writev(7,vectors,2)==-1 && errno==ENOTSUP);
}
'''
with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / 'vectors.c'
    exe = Path(directory) / 'vectors'
    source.write_text(HARNESS)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall',
                    '-fsanitize=address,undefined', '-I'+str(ROOT/'Source'),
                    str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: vector sends preserve byte order; EOF/error/peek terminate correctly; POSIX errors (ASan/UBSan)')
