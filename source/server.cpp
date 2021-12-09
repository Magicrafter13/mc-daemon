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

bool Server::setGroup(gid_t gid) {
	if (group != (gid_t)-1)
		return false;
	group = gid;
	return true;
}

bool Server::setNotify(std::vector<std::string> notify) {
	if (!this->notify.empty())
		return false;
	this->notify = notify;
	return true;
}

bool Server::setRun(std::string run) {
	if (!this->run.empty())
		return false;
	this->run = run;
	return true;
}

bool Server::setUser(uid_t uid) {
	if (user != (uid_t)-1)
		return false;
	user = uid;
	return true;
}

bool Server::start(void (*function)(Server*)) {
	if (running)
		return false;
	mtx = new std::mutex;
	cv = new std::condition_variable;
	thread = new std::thread(function, (Server*)this);
	running = true;
	return true;
}

bool Server::stop() {
	if (!running)
		return false;
	this->send("stop\n");
	thread->join();
	delete thread;
	delete cv;
	delete mtx;
	running = false;
	return true;
}

Server::Server(std::string name) {
	this->name = name;
}

Server::~Server() {
	if (thread != nullptr)
		delete thread;
	if (cv != nullptr)
		delete cv;
	if (mtx != nullptr)
		delete mtx;
}
