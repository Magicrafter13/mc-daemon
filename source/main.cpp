#include <errno.h>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
//#include <systemd/sd-daemon.h
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include "config.hpp"
#include "server.hpp"
#include "usock.hpp"

enum _cmd_t {
	daemonize,
	quit,
	test,
	reload,
	start,
	restart,
	stop,
	backup,
	user,
};
typedef enum _cmd_t Command_t;

struct _cmd {
	Command_t type;
	std::string server_name;
	std::string additional;
};
typedef struct _cmd Command;

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
		else if (argument == "--reload")
			cmd.type = reload;
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
			if (argument == "--daemon" || argument == "--quit" || argument == "--test" || argument == "--reload") {
				std::cerr << argument << " cannot be used with other arguments" << std::endl;
				return 1;
			}
			else if (argument == "--start")
				cmd.type = start;
			else if (argument == "--restart")
				cmd.type = restart;
			else if (argument == "--stop")
				cmd.type = stop;
			else if (argument == "--backup")
				cmd.type = backup;
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

	// Get config file location and load
	char *env_config_loc = getenv("MCD_CONFIG");
	Config config(env_config_loc == NULL ? "/etc/mc-daemon.conf" : env_config_loc);
	// Parse config file
	if (commands[0].type == test) {
		if (!config.error())
			std::cout << "Config file OK." << std::endl;
		return config.error();
	}
	if (config.error())
		return 1;

	// Get data location (socket and pid file)
	char *env_data_loc = getenv("MCD_DATA");
	std::string data_loc = env_data_loc == NULL ? "/run/mc-daemon" : env_data_loc;

	// Create socket data
	Socket *sock = new Socket(data_loc + "/socket");

	// Check if socket already exists
	if (commands[0].type != daemonize) {
	//if (access("/run/mc-daemon/socket", F_OK) == 0) {
		if (sock->connect() == -1) {
			int err = errno;
			std::cerr << "connect error" << std::endl;
			delete sock;
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
				case reload:
					sock->sendLine("reload");
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
				case backup:
					sock->sendLine("backup" + (c.server_name.empty() ? "" : " " + c.server_name));
					break;
				case user:
					sock->sendLine("user " + c.server_name + '\n' + c.additional);
			}
			if (done)
				break;
		}
		delete sock;
		return error;
	}
	// Create service dir if it doesn't exist
	if (mkdir(data_loc.c_str(), 0755) == -1) {
		if (errno != EEXIST) {
			int err = errno;
			std::cerr << "Could not create " << data_loc << "/!" << std::endl;
			return err;
		}
	}

	// Create daemon
	pid_t daemon = fork();
	if (daemon) {
		std::cout << "Started daemon" << std::endl;
		std::ofstream pid_file((data_loc + "/pid").c_str(), std::ios_base::out);
		pid_file << daemon << std::endl;
		pid_file.close();
		delete sock;
		return 0;
	}

	// Newly forked daemon will execute the following code
	if (sock->bind() == -1) {
		int err = errno;
		std::cerr << "bind error" << std::endl;
		delete sock;
		return err;
	}
	if (sock->listen() == -1) {
		int err = errno;
		std::cerr << "listen error" << std::endl;
		delete sock;
		unlink((data_loc + "/socket").c_str());
		return err;
	}

	// Start default servers
	std::map<std::string, Server*> servers(config.getServers());
	std::cout << "Config has " << servers.size() << " servers." << std::endl;
	for (auto block : servers) {
		if (block.second->defaultStartup()) {
			std::cout << "Starting server [" << block.first << "]" << std::endl;
			block.second->start();
		}
	}
	// Act as daemon
	bool quit = false;
	while (!quit) {
		if (sock->accept() == -1) {
			int err = errno;
			std::cerr << "accept error" << std::endl;
			delete sock;
			unlink((data_loc + "/socket").c_str());
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
			if (command == "reload") {
				std::cout << "Reloading config file..." << std::endl;
				if (config.parseConfigFile())
					std::cout << "Successfully reloaded config." << std::endl;
				else
					std::cout << "Please fix your config file and try again - no servers were modified." << std::endl;
				continue;
			}
			if (command == "restart" && name.empty()) {
				std::cout << "Stopping all servers..." << std::endl;
				for (auto block : servers)
					block.second->stop();
				std::cout << "Stopped." << std::endl;
				config = Config("/etc/mc-daemon.conf");
				if (config.error())
					return 1;
				servers = config.getServers();
				std::cout << "Config has " << servers.size() << " servers." << std::endl;
				for (auto block : servers) {
					Server *s = block.second;
					if (s->defaultStartup()) {
						std::cout << "Starting server [" << s->getName() << "]" << std::endl;
						s->start();
					}
				}
				break;
			}

			// Handle command
			if (name.empty()) {
				// Attempt command on all servers
				for (auto block : servers) {
					Server *s = block.second;
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
					else if (command == "backup") {
						if (!s->backup())
							continue;
						std::cout << "Backing up";
					}
					std::cout << " server [" << s->getName() << "]" << std::endl;
				}
			}
			else {
				auto block_it = servers.find(name);
				if (block_it == servers.end())
					std::cout << "No server named [" << name << "]!" << std::endl;
				else {
					Server *s = block_it->second;
					if (command == "start")
						std::cout << (s->start() ? "Starting server [" + name + "]" : "Server [" + name + "] is already running!") << std::endl;
					else if (command == "restart")
						std::cout << (s->restart() ? "Restarting server [" + name + "]" : "Server [" + name + "] is not running!") << std::endl;
					else if (command == "stop")
						std::cout << (s->stop() ? "Stopped server [" + name + "]" : "Server [" + name + "] is not running!") << std::endl;
					else if (command == "backup")
						std::cout << (s->backup() ? "Backing up server [" + name + "]" : "Server [" + name + "] is not running!") << std::endl;
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
	std::cout << "Stopping servers..." << std::endl;
	for (auto block : servers)
		if (block.second->stop())
			std::cout << "Stopped [" << block.first << "]" << std::endl;
	delete sock;
	unlink((data_loc + "/socket").c_str());
}
