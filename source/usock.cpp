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

int Socket::connect() {
	return ::connect(sockfd, (struct sockaddr*)&sock, sizeof (sock));
}

int Socket::fd() {
	return sockfd;
}

bool Socket::hasMessage() {
	return !messages.empty();
}

int Socket::listen() {
	return ::listen(sockfd, 0);
}

std::string Socket::nextMessage() {
	std::string message = messages.front();
	messages.pop();
	return message;
}

#include <iostream>
void Socket::read() {
	char buffer[SOCK_BUF_SIZE] = {};
	while (::read(connectfd, buffer, SOCK_BUF_SIZE) != -1 && buffer[0] != '\0') {
		std::string data(buffer);
		buffer[0] = '\0';
		std::string::size_type line_break;
		while (line_break = data.find_first_of('\n'), line_break != std::string::npos) {
			messages.push(data.substr(0, line_break));
			data.erase(0, line_break + 1);
		}
		if (!data.empty())
			messages.push(data);
	}
	close(connectfd);
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
	close(connectfd)
}

/*std::istream &operator>> (std::istream &in, Socket &s) {
	std::string token;
	while (getline(in, token), in.eof())
	while (bool(in >> token))
	std::string
}*/
