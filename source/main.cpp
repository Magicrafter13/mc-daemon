#include <errno.h>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <string>
//#include <systemd/sd-daemon.h
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include "server.hpp"
#include "usock.hpp"

enum _cmd_t {
	daemonize,
	quit,
	test,
	start,
	restart,
	stop,
	user,
};
typedef enum _cmd_t Command_t;

struct _cmd {
	Command_t type;
	std::string server_name;
	std::string additional;
};
typedef struct _cmd Command;

std::vector<Server*> *parseConfig();

int main(int argc, char *argv[]) {
	std::vector<Command> commands;

	// Parse arguments
	bool simple = false;
	if (argc == 2) {
		simple = true;
		std::string argument(argv[1]);
		Command cmd;
		if (argument == "--daemon")
			cmd.type = daemonize;
		else if (argument == "--quit")
			cmd.type = quit;
		else if (argument == "--test")
			cmd.type = test;
		else
			simple = false;
		if (simple)
			commands.push_back(cmd);
	}
	if (!simple) {
		std::string argument;
		for (int arg = 1; arg < argc; ++arg) {
			argument = argv[arg];
			Command cmd = {};
			if (argument == "--daemon" || argument == "--quit" || argument == "--test") {
				std::cerr << argument << " cannot be used with other arguments" << std::endl;
				return 1;
			}
			else if (argument == "--start")
				cmd.type = start;
			else if (argument == "--restart")
				cmd.type = restart;
			else if (argument == "--stop")
				cmd.type = stop;
			else if (argument == "--command")
				cmd.type = user;
			else {
				std::cerr << "Unexpected argument \"" << argv[arg] << "\"!" << std::endl;
				return 1;
			}
			if (argv[arg + 1] != NULL && argv[arg + 1][0] != '-')
				cmd.server_name = argv[++arg];
			if (cmd.type == user) {
				if (cmd.server_name.empty() || argv[arg + 1] == NULL) {
					std::cerr << "--command requires a string argument that does not start with '-'!" << std::endl;
					return 1;
				}
				cmd.additional = argv[++arg];
			}
			commands.push_back(cmd);
		}
	}
	if (commands.empty()) {
		std::cerr << "Please specify at least one command!" << std::endl;
		return 1;
	}

	std::vector<Server*> *config = parseConfig();
	// Parse config file
	if (commands[0].type == test) {
		if (config != nullptr) {
			std::cout << "Config file OK." << std::endl;
			delete config;
		}
		return 0;
	}

	// Create socket data
	Socket *sock = new Socket("/run/mc-daemon/socket");

	// Check if socket already exists
	if (commands[0].type != daemonize) {
	//if (access("/run/mc-daemon/socket", F_OK) == 0) {
		if (sock->connect() == -1) {
			int err = errno;
			std::cerr << "connect error" << std::endl;
			delete sock;
			delete config;
			return err;
		}
		bool done = false;
		int error = 0;
		// Send command to daemon
		for (Command c : commands) {
			switch (c.type) {
				case daemonize:
					std::cerr << "--daemon did not follow correct path!\n" << std::endl;
					done = true;
					error = 1;
					break;
				case quit:
					sock->sendLine("quit");
					break;
				case test:
					std::cerr << "--test did not exit after testing!\n" << std::endl;
					done = true;
					error = 1;
					break;
				case start:
					sock->sendLine("start" + (c.server_name.empty() ? "" : " " + c.server_name));
					break;
				case restart:
					sock->sendLine("restart" + (c.server_name.empty() ? "" : " " + c.server_name));
					break;
				case stop:
					sock->sendLine("stop" + (c.server_name.empty() ? "" : " " + c.server_name));
					break;
				case user:
					sock->sendLine("user " + c.server_name + '\n' + c.additional);
			}
			if (done)
				break;
		}
		delete sock;
		delete config;
		return error;
	}
	// Create service dir if it doesn't exist
	if (mkdir("/run/mc-daemon", 0755) == -1) {
		if (errno != EEXIST) {
			int err = errno;
			std::cerr << "Could not create /run/mc-daemon/!" << std::endl;
			return err;
		}
	}

	// Create daemon
	pid_t daemon = fork();
	if (daemon) {
		std::cout << "Started daemon" << std::endl;
		std::ofstream pid_file("/run/mc-daemon/pid", std::ios_base::out);
		pid_file << daemon << std::endl;
		pid_file.close();
		delete sock;
		delete config;
		return 0;
	}

	// Newly forked daemon will execute the following code
	if (sock->bind() == -1) {
		int err = errno;
		std::cerr << "bind error" << std::endl;
		delete sock;
		delete config;
		return err;
	}
	if (sock->listen() == -1) {
		int err = errno;
		std::cerr << "listen error" << std::endl;
		delete sock;
		unlink("/run/mc-daemon/socket");
		delete config;
		return err;
	}

	// Start default servers
	std::cout << "Config has " << config->size() << " servers." << std::endl;
	for (std::vector<Server*>::iterator s = config->begin(); s != config->end(); ++s) {
		if ((*s)->defaultStartup()) {
			std::cout << "Starting server [" << (*s)->getName() << "]" << std::endl;
			(*s)->start();
		}
	}
	// Act as daemon
	bool quit = false;
	while (!quit) {
		if (sock->accept() == -1) {
			int err = errno;
			std::cerr << "accept error" << std::endl;
			delete sock;
			unlink("/run/mc-daemon/socket");
			delete config;
			return err;
		}

		sock->read();
		std::string command, name;
		while (sock->hasMessage()) {
			command = sock->nextMessage();
			name.clear();

			std::string::size_type space = command.find_first_of(' ');
			if (space != std::string::npos) {
				name = command.substr(space + 1);
				command.erase(space);
			}
			if (command == "quit") {
				quit = true;
				break;
			}

			// Handle command
			if (name.empty()) {
				// Attempt command on all servers
				for (Server *s : *config) {
					if (command == "start") {
						if (!s->start())
							continue;
						std::cout << "Starting";
					}
					else if (command == "restart") {
						if (!s->restart())
							continue;
						std::cout << "Restarting";
					}
					else if (command == "stop") {
						if (!s->stop())
							continue;
						std::cout << "Stopped";
					}
					std::cout << " server [" << s->getName() << "]" << std::endl;
				}
			}
			else {
				std::vector<Server*>::size_type i;
				Server *s;
				for (i = 0; i < config->size(); ++i) {
					if ((*config)[i]->getName() == name) {
						s = (*config)[i];
						break;
					}
				}
				if (i == config->size())
					std::cout << "No server named [" << name << "]!" << std::endl;
				else {
					if (command == "start")
						std::cout << (s->start() ? "Starting server [" + name + "]" : "Server [" + name + "] is already running!") << std::endl;
					else if (command == "restart")
						std::cout << (s->restart() ? "Restarting server [" + name + "]" : "Server [" + name + "] is not running!") << std::endl;
					else if (command == "stop")
						std::cout << (s->stop() ? "Stopped server [" + name + "]" : "Server [" + name + "] is not running!") << std::endl;
					else if (command == "user") {
						if (!sock->hasMessage()) {
							std::cout << "Expected next line to contain custom command, but message queue was empty!" << std::endl;
							continue;
						}
						std::cout << "Sending custom command to [" + name + "]:" << std::endl;
						std::string message = sock->nextMessage();
						s->send(message + '\n');
					}
				}
			}
		}
	}
	write(1, "Daemon dying\n", 13);
	delete sock;
	unlink("/run/mc-daemon/socket");
	delete config;
}

std::vector<Server*> *parseConfig() {
	std::vector<Server*> *config = new std::vector<Server*>;
	std::ifstream conf_file("/etc/mc-daemon.conf", std::ios_base::in);
	std::string buffer;
	size_t line = 0;
	while (++line, getline(conf_file, buffer), !conf_file.eof()) {
		// Ignore empty lines
		if (buffer.empty())
			continue;
		// Ignore comments
		if (buffer[0] == '#')
			continue;
		if (buffer[0] == '[') {
			if (buffer.back() != ']') {
				std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - expected ']', got '" << buffer[buffer.size()] << "'!" << std::endl;
				delete config;
				return nullptr;
			}
			if (buffer[1] == '-') {
				std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - server name cannot start with '-'!" << std::endl;
				delete config;
				return nullptr;
			}
			config->push_back(new Server(buffer.substr(1, buffer.size() - 2)));
		}
		else {
			if (config->empty()) {
				std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - no [server] block was defined yet!" << std::endl;
				delete config;
				return nullptr;
			}
			std::string::size_type equals = buffer.find_first_of('=');
			if (equals == std::string::npos) {
				std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - no '=' found!" << std::endl;
				delete config;
				return nullptr;
			}
			std::string key = buffer.substr(0, equals);
			std::string value = buffer.substr(equals + 1);
			if (key == "user") {
				if (!config->back()->setUser(value)) {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - key \"user\" was already defined!" << std::endl;
					delete config;
					return nullptr;
				}
			}
			else if (key == "group") {
				if (!config->back()->setGroup(value)) {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - key \"group\" was already defined!" << std::endl;
					delete config;
					return nullptr;
				}
			}
			else if (key == "default") {
				if (value != "yes" && value != "no") {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - expected \"yes\" or \"no\", got \"" << value << "\"!" << std::endl;
					delete config;
					return nullptr;
				}
				config->back()->setDefault(value == "yes");
			}
			else if (key == "before") {
				std::vector<std::string> server_argv;
				while (!value.empty()) {
					std::string::size_type space = value.find_first_of(' ');
					server_argv.push_back(value.substr(0, space));
					value.erase(0, space == std::string::npos ? space : space + 1);
				}
				if (!config->back()->setBefore(server_argv)) {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - key \"before\" was already defined!" << std::endl;
					delete config;
					return nullptr;
				}
			}
			else if (key == "run") {
				if (!config->back()->setRun(value)) {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - key \"run\" was already defined!" << std::endl;
					delete config;
					return nullptr;
				}
			}
			else if (key == "after") {
				std::vector<std::string> server_argv;
				while (!value.empty()) {
					std::string::size_type space = value.find_first_of(' ');
					server_argv.push_back(value.substr(0, space));
					value.erase(0, space == std::string::npos ? space : space + 1);
				}
				if (!config->back()->setAfter(server_argv)) {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - key \"after\" was already defined!" << std::endl;
					delete config;
					return nullptr;
				}
			}
			else if (key == "notify") {
				std::vector<std::string> server_argv;
				while (!value.empty()) {
					std::string::size_type space = value.find_first_of(' ');
					server_argv.push_back(value.substr(0, space));
					value.erase(0, space == std::string::npos ? space : space + 1);
				}
				if (!config->back()->setNotify(server_argv)) {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - key \"notify\" was already defined!" << std::endl;
					delete config;
					return nullptr;
				}
			}
			else if (key == "path") {
				if (!config->back()->setPath(value)) {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - key \"path\" was already defined!" << std::endl;
					delete config;
					return nullptr;
				}
			}
		}
	}
	conf_file.close();
	for (std::vector<Server*>::iterator s = config->begin(); s != config->end(); ++s) {
		if ((*s)->getUser() == (uid_t)-1) {
			std::cerr << "Error in [" << (*s)->getName() << "], no user defined!" << std::endl;
			delete config;
			return nullptr;
		}
		else if ((*s)->getGroup() == (gid_t)-1) {
			std::cerr << "Error in [" << (*s)->getName() << "], no group defined!" << std::endl;
			delete config;
			return nullptr;
		}
		else if ((*s)->getPath().empty()) {
			std::cerr << "Error in [" << (*s)->getName() << "], no path defined!" << std::endl;
			delete config;
			return nullptr;
		}
	}
	return config;
}
