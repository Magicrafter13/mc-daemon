#include <fcntl.h>
#include <grp.h>
#include <iostream>
#include <pwd.h>
#include <sys/wait.h>
#include <unistd.h>
#include "server.hpp"

bool Server::defaultStartup() {
	return default_startup;
}

pid_t Server::execute(std::vector<std::string> args) {
	pid_t child = fork();
	if (!child) {
		// set up file descriptors
		close(fds[1]);
		dup2(fds[0], 0);

		// become proper user/group
		setgid(group);
		setuid(user);

		int logfd;

		// Go to the designated logging directory (if one was set)
		if (!log.empty()) {
			if (chdir(log.c_str()) == -1) {
				std::cerr << "chdir error (" << errno << ")" << std::endl;
				exit(1);
			}
			if (logfd = open(("mcd." + name + ".log").c_str(), O_CREAT | O_APPEND | O_WRONLY, S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH), logfd == -1) {
				std::cerr << "Could not open log file for writing! (" << errno << ")" << std::endl;
				exit(1);
			}
		}

		// Go to the designated directory
		if (chdir(path.c_str()) == -1) {
			std::cerr << "chdir error (" << errno << ")" << std::endl;
			exit(1);
		}
		if (log.empty()) {
			if (logfd = open(("mcd." + name + ".log").c_str(), O_CREAT | O_APPEND | O_WRONLY, S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH), logfd == -1) {
				std::cerr << "Could not open log file for writing! (" << errno << ")" << std::endl;
				exit(1);
			}
		}

		// Redirect stdout and stderr to log file
		dup2(logfd, 1);
		dup2(logfd, 2);

		const char *argv[args.size() + 1];
		std::vector<std::string>::size_type i;
		for (i = 0; i < args.size(); ++i)
			argv[i] = args[i].c_str();
		argv[i] = NULL;
		if (execvp(argv[0], (char**)argv) == -1)
			std::cerr << "execvp error when trying to run " << argv[0] << "! (" << errno << ")" << std::endl;
		close(fds[0]);
		exit(errno);
	}
	return child;
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

std::string Server::getLog() {
	return log;
}

std::mutex *Server::getMtx() {
	return mtx;
}

std::string Server::getName() {
	return name;
}

std::string Server::getNotify() {
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

	// Create communication pipe
	pipe(fds);

	pid_t child;
	signal(SIGTERM, SIG_IGN);

	/*
	 * Before
	 */
	if (!before.empty()) {
		if (child = execute(before), child == -1)
			return;
		while (waitpid(child, NULL, 0) == -1 && errno == EINTR);
	}

	// Notify
	if (!notify.empty()) {
		if (child = execute({ notify, "Starting " + name + "." }), child == -1)
			return;
		while (waitpid(child, NULL, 0) == -1 && errno == EINTR);
	}

	/*
	 * Run
	 */
	if (child = execute({ run }), child == -1)
		return;
	std::unique_lock<std::mutex> lck(*mtx);
	std::string command;
	bool stop = false;
	while (!stop) {
		while (commands.empty())
			cv->wait(lck);
		command = popCommand();
		lck.unlock();
		if (command == "restart\n") {
			write(fds[1], "say §4Restarting server in §c10§4 seconds!\n", 46);
			sleep(10);
			write(fds[1], "stop\n", 5);
			while (waitpid(child, NULL, 0) == -1 && errno == EINTR);
			if (child = execute({ run }), child == -1)
				return;
			lck.lock();
			continue;
		}
		if (command == "stop\n") {
			// Notify
			if (!notify.empty())
				if (execute({ notify, "Stopping " + name + "..." }) == -1)
					return;
			stop = true;
		}
		write(fds[1], command.c_str(), command.size());
		lck.lock();
	}
	lck.unlock();
	std::cout << "Waiting for server to stop..." << std::endl;
	while (waitpid(child, NULL, 0) == -1 && errno == EINTR);

	// Notify
	if (!notify.empty()) {
		if (child = execute({ notify, "Stopped " + name + "." }), child == -1)
			return;
		while (waitpid(child, NULL, 0) == -1 && errno == EINTR);
	}

	/*
	 * After
	 */
	if (!after.empty()) {
		if (child = execute(after), child == -1)
			return;
		while (waitpid(child, NULL, 0) == -1 && errno == EINTR);
	}

	close(fds[0]);

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

bool Server::setLog(std::string log) {
	if (!this->log.empty())
		return false;
	this->log = log;
	return true;
}

bool Server::setNotify(std::string notify) {
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

	// Start server
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
