#include <grp.h>
#include <iostream>
#include <pwd.h>
#include <sys/wait.h>
#include <unistd.h>
#include "server.hpp"

bool Server::defaultStartup() {
	return default_startup;
}

std::vector<std::string> Server::getAfter() {
	return after;
}

std::vector<std::string> Server::getBefore() {
	return before;
}

std::condition_variable *Server::getCv() {
	return cv;
}

gid_t Server::getGroup() {
	return group;
}

std::mutex *Server::getMtx() {
	return mtx;
}

std::string Server::getName() {
	return name;
}

std::vector<std::string> Server::getNotify() {
	return notify;
}

std::string Server::getPath() {
	return path;
}

std::string Server::getRun() {
	return run;
}

uid_t Server::getUser() {
	return user;
}

bool Server::hasCommand() {
	return !commands.empty();
}

std::string Server::popCommand() {
	std::string cmd = commands.front();
	commands.pop();
	return cmd;
}

bool Server::restart() {
	if (!running)
		return false;
	this->send("restart\n");
	return true;
}

void Server::runServer() {
	std::cout << "Thread created" << std::endl;

	signal(SIGTERM, SIG_IGN);

	/*
	 * Before
	 */
	pid_t child_pre = fork();
	if (!child_pre) {
		const char *argv[before.size() + 1];
		std::string::size_type i;
		for (i = 0; i < before.size(); ++i)
			argv[i] = before[i].c_str();
		argv[i] = NULL;
		if (execvp(argv[0], (char**)argv) == -1)
			std::cerr << "execvp error when trying to run " << argv[0] << "! (" << errno << ")" << std::endl;
		return;
	}
	while (waitpid(child_pre, NULL, 0) == -1 && errno == EINTR);

	/*
	 * Run
	 */
	// Create communication pipe
	int fds[2];
	pipe(fds);

	pid_t child = fork();
	if (!child) {
		// Set up file descriptors
		close(fds[1]);
		dup2(fds[0], 0);

		// Go to the designated directory
		if (chdir(path.c_str()) == -1) {
			std::cerr << "chdir error (" << errno << ")" << std::endl;
			send("stop\n");
			close(fds[0]);
			return;
		}

		// Become proper user/group
		setgid(group);
		setuid(user);

		// Run user specified program
		const char *argv[2] = { run.c_str(), NULL };
		if (execvp(argv[0], (char**)argv) == -1)
			std::cerr << "execvp error when trying to run " << argv[0] << "! (" << errno << ")" << std::endl;
		send("stop\n"); // Attempt to stop the server
		close(fds[0]); // Destroy the pipe
		return;
	}
	close(fds[0]);

	std::unique_lock<std::mutex> lck(*mtx);
	std::string command;
	bool stop = false;
	while (!stop) {
		while (commands.empty())
			cv->wait(lck);
		command = popCommand();
		lck.unlock();
		if (command == "stop\n")
			stop = true;
		write(fds[1], command.c_str(), command.size());
		lck.lock();
	}
	lck.unlock();
	std::cout << "Waiting for server to stop..." << std::endl;
	while (waitpid(child, NULL, 0) == -1 && errno == EINTR);

	/*
	 * After
	 */
	pid_t child_post = fork();
	if (!child_post) {
		const char *argv[after.size() + 1];
		std::string::size_type i;
		for (i = 0; i < after.size(); ++i)
			argv[i] = after[i].c_str();
		argv[i] = NULL;
		if (execvp(argv[0], (char**)argv) == -1)
			std::cerr << "execvp error when trying to run " << argv[0] << "! (" << errno << ")" << std::endl;
		return;
	}
	while (waitpid(child_post, NULL, 0) == -1 && errno == EINTR);

	std::cout << "Thread exiting" << std::endl;
}

void Server::send(std::string message) {
	mtx->lock();
	commands.push(message);
	mtx->unlock();
	cv->notify_one();
}

bool Server::setAfter(std::vector<std::string> after) {
	if (!this->after.empty())
		return false;
	this->after = after;
	return true;
}

bool Server::setBefore(std::vector<std::string> before) {
	if (!this->before.empty())
		return false;
	this->before = before;
	return true;
}

void Server::setDefault(bool default_startup) {
	this->default_startup = default_startup;
}

bool Server::setGroup(std::string group) {
	if (this->group != (gid_t)-1)
		return false;
	errno = 0;
	struct group *grp_ent = getgrnam(group.c_str());
	if (grp_ent == NULL) {
		switch (errno) {
			case EINTR:
			case EIO:
			case EMFILE:
			case ENFILE:
			case ENOMEM:
			case ERANGE:
				std::cerr << "getpwnam error (" << errno << ")" << std::endl;
				break;
			default:
				std::cerr << "No group with the name \"" << group << "\" exists in the group file!" << std::endl;
		}
		return false;
	}
	this->group = grp_ent->gr_gid;
	return true;
}

bool Server::setNotify(std::vector<std::string> notify) {
	if (!this->notify.empty())
		return false;
	this->notify = notify;
	return true;
}

bool Server::setPath(std::string path) {
	if (!this->path.empty())
		return false;
	this->path = path;
	return true;
}

bool Server::setRun(std::string run) {
	if (!this->run.empty())
		return false;
	this->run = run;
	return true;
}

bool Server::setUser(std::string user) {
	if (this->user != (uid_t)-1)
		return false;
	struct passwd *pwd_ent = getpwnam(user.c_str());
	if (pwd_ent == NULL) {
		switch (errno) {
			case EINTR:
			case EIO:
			case EMFILE:
			case ENFILE:
			case ENOMEM:
			case ERANGE:
				std::cerr << "getpwnam error (" << errno << ")" << std::endl;
				break;
			default:
				std::cerr << "No user with the name \"" << user << "\" exists in the password file!" << std::endl;
		}
		return false;
	}
	this->user = pwd_ent->pw_uid;
	return true;
}

bool Server::start() {
	if (running)
		return false;
	mtx = new std::mutex;
	cv = new std::condition_variable;
	thread = new std::thread(&Server::runServer, this);
	running = true;
	return true;
}

bool Server::stop() {
	if (!running)
		return false;
	this->send("stop\n");
	thread->join();
	delete thread; thread = nullptr;
	delete cv;     cv = nullptr;
	delete mtx;    mtx = nullptr;
	running = false;
	return true;
}

Server::Server(std::string name) {
	this->name = name;

	mtx = nullptr;
	cv = nullptr;
	thread = nullptr;
}

Server::~Server() {
	if (thread != nullptr)
		delete thread;
	if (cv != nullptr)
		delete cv;
	if (mtx != nullptr)
		delete mtx;
}
