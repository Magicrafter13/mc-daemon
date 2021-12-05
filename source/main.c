#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

enum _cmd {
	unspecified,
	start,
	restart,
	stop,
};
typedef enum _cmd Cmd;

const char default_file[] = "minecraft";

int main(int argc, char *argv[]) {
	Cmd command = unspecified;
	char *custom_file = NULL;

	// Parse arguments
	for (int arg = 1; arg < argc; ++arg) {
		if (!strcmp(argv[arg], "--start"))
			command = start;
		else if (!strcmp(argv[arg], "--restart"))
			command = restart;
		else if (!strcmp(argv[arg], "--stop"))
			command = stop;
		else
			custom_file = argv[arg];
	}
	if (command == unspecified)
		command = start;

	// Get socket filename
	char sockpath[strlen(custom_file == NULL ? default_file : custom_file) + 6]; // 5 for "/tmp/", 1 for '\0'
	sockpath[0] = '\0';
	strcat(sockpath, "/tmp/");
	strcat(sockpath, custom_file == NULL ? default_file : custom_file);

	// Create socket data
	struct sockaddr_un sock;
	sock.sun_family = AF_UNIX;
	strcpy(sock.sun_path, sockpath);
	int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);

	// Check if socket already exists
	if (access(sockpath, F_OK) == 0) {
		// Send command to daemon
		switch (command) {
			case unspecified:
				write(2, "command error!\n", 15);
				close(sockfd);
				return 1;
			case start:
				write(1, "Daemon already running.\n", 24);
				close(sockfd);
				return 0;
			default:
				if (connect(sockfd, (struct sockaddr*)&sock, sizeof (sock)) == -1) {
					write(2, "connect error\n", 14);
					close(sockfd);
					return errno;
				}
				write(sockfd, command == restart ? "restart\n" : "stop\n", command == restart ? 8 : 5);
				close(sockfd);
				return 0;
		}
	}
	// Create daemon
	if (bind(sockfd, (struct sockaddr*)&sock, sizeof (sock)) == -1) {
		write(2, "bind error\n", 11);
		close(sockfd);
		return errno;
	}
	switch (command) {
		case unspecified:
			write(2, "command error!\n", 15);
			close(sockfd);
			return 1;
		case restart:
			write(1, "Daemon wasn't running...\n", 25);
			// fallthrough
		case start:
			if (fork()) {
				write(1, "Started daemon\n", 15);
				close(sockfd);
				return 0;
			}
			break;
		case stop:
			write(1, "Daemon wasn't running...\n", 25);
			close(sockfd);
			return 0;
	}

	// Newly forked daemon will execute the following code
	//printf("daemon listening\n");
	if (listen(sockfd, 0) == -1) {
		write(2, "listen error\n", 13);
		close(sockfd);
		unlink(sockpath);
		return errno;
	}
	char buffer[512];
	while (1) {
		int connectfd = accept(sockfd, NULL, NULL);
		if (connectfd == -1) {
			write(2, "accept error\n", 13);
			close(sockfd);
			unlink(sockpath);
			return errno;
		}
		memset(buffer, '\0', 512);
		//printf("daemon accepted connection\n");
		read(connectfd, buffer, 512);
		close(connectfd);
		if (!strcmp(buffer, "restart\n"))
			;// do something
		else if (!strcmp(buffer, "stop\n"))
			break;
	}
	write(1, "Daemon dying\n", 13);
	close(sockfd);
	unlink(sockpath);
}
