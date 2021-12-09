//#include <systemd/sd-daemon.h>
#include <sys/socket.h>
#include <unistd.h>
#include "usock.hpp"

#define SOCK_BUF_SIZE 512

int Socket::accept() {
	return connectfd = ::accept(sockfd, NULL, NULL);
}

struct sockaddr_un Socket::addr() {
	return sock;
}

int Socket::bind() {
	return ::bind(sockfd, (struct sockaddr*)&sock, sizeof (sock));
}

void Socket::close() {
	::close(connectfd);
}

int Socket::connect() {
	return ::connect(sockfd, (struct sockaddr*)&sock, sizeof (sock));
}

int Socket::fd() {
	return sockfd;
}

int Socket::listen() {
	return ::listen(sockfd, 0);
}

std::string Socket::read() {
	char buffer[SOCK_BUF_SIZE] = {};
	::read(connectfd, buffer, SOCK_BUF_SIZE);
	std::string s_buf(buffer);
	if (s_buf.back() == '\n')
		s_buf.pop_back();
	return s_buf;
}

void Socket::sendLine(std::string message) {
	write(sockfd, (message + '\n').c_str(), message.size() + 1);
}

Socket::Socket(std::string path) {
	sock.sun_family = AF_UNIX;
	strcpy(sock.sun_path, path.c_str());
	sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
}

Socket::~Socket() {
	close();
}

/*std::istream &operator>> (std::istream &in, Socket &s) {
	std::string token;
	while (getline(in, token), in.eof())
	while (bool(in >> token))
	std::string
}*/
