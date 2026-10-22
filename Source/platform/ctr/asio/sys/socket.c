#include <errno.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

static int validate_iov(const struct msghdr *message)
{
	if (message == NULL || (message->msg_iovlen != 0 && message->msg_iov == NULL)) {
		errno = EFAULT;
		return -1;
	}
	if (message->msg_iovlen < 0) {
		errno = EINVAL;
		return -1;
	}
	size_t total = 0;
	// devkitARM newlib omits SSIZE_MAX. On SOC, ssize_t is the signed
	// counterpart of size_t; derive its bound without that optional macro.
	_Static_assert(sizeof(ssize_t) == sizeof(size_t), "socket size types must match");
	const size_t maxTransfer = ((size_t)-1) >> 1;
	for (int i = 0; i < message->msg_iovlen; ++i) {
		const struct iovec *iov = &message->msg_iov[i];
		if (iov->iov_len > maxTransfer - total) {
			errno = EINVAL;
			return -1;
		}
		if (iov->iov_len != 0 && iov->iov_base == NULL) {
			errno = EFAULT;
			return -1;
		}
		total += iov->iov_len;
	}
	return 0;
}

ssize_t stream_recvmsg(int socket, struct msghdr *message, int flags)
{
	struct iovec *next = message->msg_iov;
	int iovcount = message->msg_iovlen;

	ssize_t total = 0;
	for (int i = 0; i < iovcount; ++i, ++next) {
		struct iovec *iov = next;
		char *base = iov->iov_base;
		size_t length = iov->iov_len;

		while (length > 0) {
			ssize_t bytesReceived = recv(socket, base, length, flags);
			// EOF must also end MSG_WAITALL. Preserve data already received
			// if the peer closes or a later receive fails.
			if (bytesReceived <= 0)
				return total > 0 ? total : (bytesReceived < 0 ? -1 : 0);
			base += bytesReceived;
			length -= bytesReceived;
			total += bytesReceived;

			// A second peek would copy the same bytes again.
			if ((flags & MSG_WAITALL) == 0 || (flags & MSG_PEEK) != 0)
				return total;
		}
	}
	return total;
}

ssize_t dgram_recvmsg(int socket, struct msghdr *message, int flags)
{
	errno = ENOTSUP;
	return -1;
}

ssize_t recvmsg(int socket, struct msghdr *message, int flags)
{
	if (validate_iov(message) < 0)
		return -1;
	int type;
	socklen_t length = sizeof(int);
	if (getsockopt(socket, SOL_SOCKET, SO_TYPE, &type, &length) < 0)
		return -1;

	if (type == SOCK_STREAM) {
		message->msg_flags = 0;
		message->msg_namelen = 0;
		message->msg_controllen = 0;
		return stream_recvmsg(socket, message, flags);
	}
	if (type == SOCK_DGRAM)
		return dgram_recvmsg(socket, message, flags);

	errno = ENOTSOCK;
	return -1;
}

ssize_t stream_sendmsg(int socket, const struct msghdr *message, int flags)
{
	struct iovec *next = message->msg_iov;
	int iovcount = message->msg_iovlen;

	ssize_t total = 0;
	for (int i = 0; i < iovcount; ++i, ++next) {
		struct iovec *iov = next;
		void *base = iov->iov_base;
		size_t length = iov->iov_len;
		if (length == 0)
			continue;

		ssize_t bytesSent = send(socket, base, length, flags);
		if (bytesSent < 0)
			return total > 0 ? total : -1;
		total += bytesSent;
		// The caller advances its buffers by the returned byte count.
		// Sending the next vector now would skip this vector's unsent tail.
		if ((size_t)bytesSent < length)
			return total;
	}
	return total;
}

ssize_t dgram_sendmsg(int socket, const struct msghdr *message, int flags)
{
	errno = ENOTSUP;
	return -1;
}

ssize_t sendmsg(int socket, const struct msghdr *message, int flags)
{
	if (validate_iov(message) < 0)
		return -1;
	if (message->msg_controllen != 0) {
		errno = ENOTSUP;
		return -1;
	}
	int type;
	socklen_t length = sizeof(int);
	if (getsockopt(socket, SOL_SOCKET, SO_TYPE, &type, &length) < 0)
		return -1;

	if (type == SOCK_STREAM)
		return stream_sendmsg(socket, message, flags);
	if (type == SOCK_DGRAM)
		return dgram_sendmsg(socket, message, flags);

	errno = ENOTSOCK;
	return -1;
}

int socketpair(int domain, int type, int protocol, int socket_vector[2])
{
	errno = ENOTSUP;
	return -1;
}
