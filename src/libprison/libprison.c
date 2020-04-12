#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/param.h>
#include <sys/un.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <netdb.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <err.h>

int
sock_ipc_may_read(int fd, void *buf, size_t n)
{
	ssize_t res, pos;
	char *s;

	s = buf;
	pos = 0;
	while (n > pos) {
		res = read(fd, s + pos, n - pos);
		switch (res) {
		case -1:
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			}
			err(1, "read failed");
		case 0:
			return (1);
		default:
			pos += res;
		}
	}
	return (0);
}

/*
 * Read data with the assertion that it all must come through, or
 * else abort the process.  Based on atomicio() from openssh. 
 */
ssize_t
sock_ipc_must_read(int fd, void *buf, size_t n)
{
	ssize_t res, pos;
	char *s;

	s = buf;
	pos = 0;
	while (n > pos) {
		res = read(fd, s + pos, n - pos);
		switch (res) {
		case -1:
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			}
		case 0:
			return (0);
		default:
			pos += res;
		}
	}
	return (n);
}

/*
 * Write data with the assertion that it all has to be written, or
 * else abort the process.  Based on atomicio() from openssh.
 */
ssize_t
sock_ipc_must_write(int fd, void *buf, size_t n)
{
	ssize_t res, pos;
	char *s;

	pos = 0;
	s = buf;
	while (n > pos) {
		res = write(fd, s + pos, n - pos);
		switch (res) {
		case -1:
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			}
			err(1, "sock_ipc_must_write failed");
		case 0:
			return (0);
		default:
			pos += res;
		}
	}
	return (n);
}
