#if 0
 # gcc -Wall -O2 v4.c -o v4
 # ./v4 any|<dev> 1.1.1.1 8080 10.10.10.1 10.10.10.2 10.10.10.3
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/tcp.h>
#include <net/if.h>

#define MARK_MAGIC_HEALTH 0x0D00

int main(int argc, char **argv)
{
	int ifindex, ret, fd, i, cnt = 0, mark = MARK_MAGIC_HEALTH, retries = 2;
	struct timeval tmo = {
		.tv_sec		= 1,
	};
	struct sockaddr_in f_addr = {
		.sin_family	= AF_INET,
	}, b_addr[128] = {};
	const int b_max = sizeof(b_addr) / sizeof(b_addr[0]), b_real = argc - 4;

	if (argc < 5 || b_real > b_max) {
		fprintf(stderr, "Usage: %s any|<dev> [fe] [port] [be1] [be2] ... [beN] (N_max=%d)\n",
			argv[0], b_max);
		return -1;
	}

	ret = inet_pton(AF_INET, argv[2], &f_addr.sin_addr);
	if (ret != 1) {
		fprintf(stderr, "pton(%s): %s\n", argv[2],
			strerror(errno));
		return ret;
	}

	f_addr.sin_port = htons(atoi(argv[3]));
	for (i = 0; i < b_real; i++) {
		b_addr[i].sin_family = f_addr.sin_family;
		b_addr[i].sin_port   = f_addr.sin_port;

		ret = inet_pton(AF_INET, argv[4 + i], &b_addr[i].sin_addr);
		if (ret != 1) {
			fprintf(stderr, "pton(%s): %s\n", argv[4 + i],
				strerror(errno));
			return ret;
		}
	}

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("socket");
		return -1;
	}

	if (strcmp(argv[1], "any")) {
		ret = setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE,
				 argv[1], strlen(argv[1]));
		if (ret < 0) {
			perror("setsockopt(SO_BINDTODEVICE)");
			goto out;
		}

		ret = if_nametoindex(argv[1]);
		if (ret == 0) {
			perror("nametoindex");
			goto out;
		}

		ifindex = ret;
		ret = setsockopt(fd, SOL_SOCKET, SO_BINDTOIFINDEX,
				 &ifindex, sizeof(ifindex));
		if (ret < 0) {
			perror("setsockopt(SO_BINDTOIFINDEX)");
			goto out;
		}

		printf("Bound health checker to device: %s (ifindex %u)\n", argv[1], ifindex);
	}

	ret = setsockopt(fd, SOL_SOCKET, SO_MARK, &mark, sizeof(mark));
	if (ret < 0) {
		perror("setsockopt(SO_MARK)");
		goto out;
	}

	ret = setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tmo, sizeof(tmo));
	if (ret < 0) {
		perror("setsockopt(SO_SNDTIMEO)");
		goto out;
	}

	ret = setsockopt(fd, IPPROTO_TCP, TCP_SYNCNT, &retries, sizeof(retries));
	if (ret < 0) {
		perror("setsockopt(TCP_SYNCNT)");
		goto out;
	}

	for (i = 0; i < b_real; i++) {
		ret = bind(fd, (struct sockaddr*)&b_addr[i], sizeof(b_addr[i]));
		if (ret < 0) {
			fprintf(stderr, "bind(%s): %s\n", argv[4 + i],
				strerror(errno));
			goto out;
		}

		ret = connect(fd, (struct sockaddr*)&f_addr, sizeof(f_addr));
		if (ret < 0) {
			fprintf(stderr, "backend down(%s): %s\n", argv[4 + i],
				strerror(errno));
			cnt++;
		}

		shutdown(fd, SHUT_RDWR);
	}
	ret = 0;
	printf("Summary for %s:%d: %d/%d backends reachable\n",
	       argv[2], ntohs(f_addr.sin_port), b_real - cnt, b_real);
out:
	close(fd);
	return ret;
}
