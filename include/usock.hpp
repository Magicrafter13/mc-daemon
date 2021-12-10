#include <queue>
#include <string>
#include <sys/un.h>

class Socket {
	int connectfd;
	std::queue<std::string> messages;
	struct sockaddr_un sock;
	int sockfd;

public:
	/*
	 * Wait for, and accept the next connection.
	 */
	int accept();

	/*
	 * Get the socket address structure for this socket.
	 */
	struct sockaddr_un addr();

	/*
	 * Bind program to this socket for receiving connections.
	 */
	int bind();

	/*
	 * Connect to the socket.
	 */
	int connect();

	/*
	 * Get the file descriptor associated with this socket.
	 */
	int fd();

	/*
	 * After calling read, this will return true until there all messages have been received.
	 */
	bool hasMessage();

	/*
	 * Listen for connections to the socket.
	 */
	int listen();

	/*
	 * Returns the next message in the queue (see hasMessage).
	 */
	std::string nextMessage();

	/*
	 * Read a string from the socket, after a connection was accepted.
	 */
	void read();

	/*
	 * Send a string through the socket followed by a new line.
	 */
	void sendLine(std::string);

	/*
	 * Initialize a UNIX socket, and return the file descriptor.
	 * Returns the result of socket(3), check for errors!
	 */
	Socket(std::string);

	/*
	 * Destructor that closes the open file descriptor for you.
	 */
	~Socket();

	//friend std::istream &operator>> (std::istream &in, Socket &s);
};
