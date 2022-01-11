#include <fcntl.h>
#include <iostream>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "server.hpp"

/*void Server::addWorld(std::string world) {
	worlds.push_back(world);
}*/

bool Server::backup() {
	if (backup_dir.empty()) {
		std::cerr << "No backup directory specified in config!" << std::endl;
		return true;
	}
	if (!running)
		return false;
	this->send("backup\n");
	return true;
}

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
		if (command == "backup\n") {
			write(fds[1], "say §1Server is backing up. There might be lag while this process completes.\n", 78);
			write(fds[1], "save-all\nsave-off\n", 18);
			sleep(5);

			std::vector<std::string> tar = { "tar", "-zcf" };
			time_t t = time(NULL);
			struct tm* now = localtime(&t);
			// TODO add backup_dir to config
			tar.push_back(backup_dir + '/' +
					name + '_' +
					std::to_string(now->tm_year + 1900) + '-' + std::to_string(now->tm_mon + 1) + '-' + std::to_string(now->tm_mday) + '-' + std::to_string(now->tm_hour) + '-' + std::to_string(now->tm_min) + '-' + std::to_string(now->tm_sec) +
					".tgz");
			tar.push_back(".");
			pid_t tar_pid = execute(tar);

			int tar_stat;
			while (waitpid(tar_pid, &tar_stat, 0) == -1 && errno == EINTR);

			write(fds[1], "save-on\n", 8);
			if (WEXITSTATUS(tar_stat))
				write(fds[1], "say §1An error occured while backing up, please alert an administrator!\n", 73);
			else
				write(fds[1], "say §1Backup finished.\n", 24);
			lck.lock();
			continue;
		}
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

void Server::setAfter(std::vector<std::string> after) {
	this->after = after;
}

void Server::setBackup(std::string backup) {
	backup_dir = backup;
}

void Server::setBefore(std::vector<std::string> before) {
	this->before = before;
}

void Server::setDefault(bool default_startup) {
	this->default_startup = default_startup;
}

bool Server::setGroup(gid_t group) {
	bool ret = running;
	if (ret)
		stop();
	this->group = group;
	return ret;
}

bool Server::setLog(std::string log) {
	bool ret = running;
	if (ret)
		stop();
	this->log = log;
	return ret;
}

void Server::setNotify(std::string notify) {
	this->notify = notify;
}

bool Server::setPath(std::string path) {
	bool ret = running;
	if (ret)
		stop();
	this->path = path;
	return ret;
}

bool Server::setRun(std::string run) {
	bool ret = running;
	if (ret)
		stop();
	this->run = run;
	return ret;
}

bool Server::setUser(uid_t user) {
	bool ret = running;
	if (ret)
		stop();
	this->user = user;
	return ret;
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
