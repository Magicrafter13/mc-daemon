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
};
typedef enum _cmd_t Command_t;

struct _cmd {
	Command_t type;
	std::string server_name;
};
typedef struct _cmd Command;

std::vector<Server*> *parseConfig();
static void runServer(Server*);

int main(int argc, char *argv[]) {
	std::vector<Command> commands;

	// Parse arguments
	for (int arg = 1; arg < argc; ++arg) {
		std::string argument(argv[arg]);
		bool server_action = false;
		if (argument == "--daemon") {
			if (commands.size()) {
				Command first = commands[0];
				commands[0] = (Command){ .type = daemonize };
				commands.push_back(first);
			}
			else
				commands.push_back((Command){ .type = daemonize });
		}
		else if (argument == "--quit") {
			if (commands.empty())
				commands.push_back((Command){ .type = quit });
			else {
				std::cerr << "--quit cannot be used with other arguments" << std::endl;
				return 1;
			}
		}
		else if (argument == "--test") {
			if (commands.empty())
				commands.push_back((Command){ .type = test });
			else {
				std::cerr << "--test cannot be used with other arguments" << std::endl;
				return 1;
			}
		}
		else if (argument == "--start") {
			commands.push_back((Command){ .type = start });
			server_action = true;
		}
		else if (argument == "--restart") {
			commands.push_back((Command){ .type = restart });
			server_action = true;
		}
		else if (argument == "--stop") {
			commands.push_back((Command){ .type = stop });
			server_action = true;
		}
		else
			std::cerr << "Unexpected argument \"" << argv[arg] << "\"!" << std::endl;
		if (server_action)
			if (argv[arg + 1] != NULL && argv[arg + 1][0] != '-')
				commands.back().server_name = argv[++arg];
	}
	if (commands.empty())
		commands.push_back((Command){ .type = start });

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
				case start: {
					//std::cout << "Sending start to daemon..." << std::endl;
					sock->sendLine("start" + (c.server_name.empty() ? "" : " " + c.server_name));
					break;
				}
				case restart: {
					//std::cout << "Sending restart to daemon..." << std::endl;
					sock->sendLine("restart" + (c.server_name.empty() ? "" : " " + c.server_name));
					break;
				}
				case stop: {
					//std::cout << "Sending stop to daemon..." << std::endl;
					sock->sendLine("stop" + (c.server_name.empty() ? "" : " " + c.server_name));
					break;
				}
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
			(*s)->start(runServer);
		}
	}
	// Parse any command line options given
	bool done = false;
	int error = 0;
	for (std::vector<Command>::iterator c = commands.begin(); c != commands.end(); ++c) {
		if (c == commands.begin()) {
			if (c->type != daemonize) {
				std::cerr << "Tried to start daemon but --daemon wasn't the first command?" << std::endl;
				delete sock;
				unlink("/run/mc-daemon/socket");
				delete config;
				return 1;
			}
			continue;
		}
		std::vector<Server*>::iterator s;
		switch (c->type) {
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
				if (c->server_name.empty())
					for (s = config->begin(); s != config->end(); ++s)
						std::cout << ((*s)->start(runServer) ? "Starting server [" + (*s)->getName() + "]" : "Server [" + c->server_name + "] is already running!") << std::endl;
				else {
					for (s = config->begin(); s != config->end(); ++s) {
						if ((*s)->getName() == c->server_name) {
							std::cout << ((*s)->start(runServer) ? "Starting server [" + c->server_name + "]" : "Server [" + c->server_name + "] is already running!") << std::endl;
							break;
						}
					}
					if (s == config->end())
						std::cerr << "No server named [" << c->server_name << "]!" << std::endl;
				}
				break;
			case restart:
				if (c->server_name.empty())
					for (s = config->begin(); s != config->end(); ++s)
						std::cout << ((*s)->restart() ? "Restarting server [" + (*s)->getName() + "]" : "Server [" + c->server_name + "] is not running!") << std::endl;
				else {
					for (s = config->begin(); s != config->end(); ++s) {
						if ((*s)->getName() == c->server_name) {
							std::cout << ((*s)->restart() ? "Restarting server [" + c->server_name + "]" : "Server [" + c->server_name + "] is not running!") << std::endl;
							break;
						}
					}
					if (s == config->end())
						std::cerr << "No server named [" << c->server_name << "]!" << std::endl;
				}
				break;
			case stop:
				if (c->server_name.empty())
					for (s = config->begin(); s != config->end(); ++s)
						std::cout << ((*s)->stop() ? "Stopped server [" + (*s)->getName() + "]" : "Server [" + c->server_name + "] is not running!") << std::endl;
				else {
					for (s = config->begin(); s != config->end(); ++s) {
						if ((*s)->getName() == c->server_name) {
							std::cout << ((*s)->stop() ? "Stopped server [" + c->server_name + "]" : "Server [" + c->server_name + "] is not running!") << std::endl;
							break;
						}
					}
					if (s == config->end())
						std::cerr << "No server named [" << c->server_name << "]!" << std::endl;
				}
				break;
		}
		if (done)
			break;
	}
	if (error) {
		delete sock;
		delete config;
		return error;
	}
	// Act as daemon
	char buffer[512];
	while (1) {
		if (sock->accept() == -1) {
			int err = errno;
			std::cerr << "accept error" << std::endl;
			delete sock;
			unlink("/run/mc-daemon/socket");
			delete config;
			return err;
		}
		std::string command = sock->read(), name;
		sock->close();
		std::string::size_type space = command.find_first_of(' ');
		if (space != std::string::npos) {
			name = command.substr(space + 1);
			command.erase(space);
		}
		if (command == "quit")
			break;
		else if (command == "start") {
			std::vector<Server*>::iterator s;
			if (name.empty()) {
				for (s = config->begin(); s != config->end(); ++s)
					if ((*s)->start(runServer))
						std::cout << "Starting server [" << (*s)->getName() << "]" << std::endl;
			}
			else {
				for (s = config->begin(); s != config->end(); ++s) {
					if ((*s)->getName() == name) {
						std::cout << ((*s)->start(runServer) ? "Starting server [" + (*s)->getName() + "]" : "Server [" + (*s)->getName() + "] is already running!") << std::endl;
						break;
					}
				}
				if (s == config->end())
					std::cerr << "No server named [" << name << "]!" << std::endl;
			}
		}
		else if (command == "restart") {
			std::vector<Server*>::iterator s;
			if (name.empty()) {
				for (s = config->begin(); s != config->end(); ++s)
					if ((*s)->restart())
						std::cout << "Restarting server [" + (*s)->getName() + "]" << std::endl;
			}
			else {
				for (s = config->begin(); s != config->end(); ++s) {
					if ((*s)->getName() == name) {
						std::cout << ((*s)->restart() ? "Restarting server [" + name + "]" : "Server [" + name + "] is not running!") << std::endl;
						break;
					}
				}
				if (s == config->end())
					std::cerr << "No server named [" << name << "]!" << std::endl;
			}
		}
		else if (command == "stop") {
			std::vector<Server*>::iterator s;
			if (name.empty()) {
				for (s = config->begin(); s != config->end(); ++s)
					if ((*s)->stop())
						std::cout << "Stopped server [" + (*s)->getName() + "]" << std::endl;
			}
			else {
				for (s = config->begin(); s != config->end(); ++s) {
					if ((*s)->getName() == name) {
						std::cout << ((*s)->stop() ? "Stopped server [" + name + "]" : "Server [" + name + "] is not running!") << std::endl;
						break;
					}
				}
				if (s == config->end())
					std::cerr << "No server named [" << name << "]!" << std::endl;
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
			if (key == "uid") {
				try {
					config->back()->setUser(std::stoi(value));
				}
				catch (std::exception e){
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - expected integer, got \"" << value << "\"!" << std::endl;
					delete config;
					return nullptr;
				}
			}
			else if (key == "gid") {
				try {
					config->back()->setGroup(std::stoi(value));
				}
				catch (std::exception e){
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - expected integer, got \"" << value << "\"!" << std::endl;
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
		}
	}
	conf_file.close();
	for (std::vector<Server*>::iterator s = config->begin(); s != config->end(); ++s) {
		if ((*s)->getUser() == (uid_t)-1) {
			std::cerr << "Error in [" << (*s)->getName() << "], no uid defined!" << std::endl;
			delete config;
			return nullptr;
		}
	}
	return config;
}

static void runServer(Server *s) {
	std::cout << "Thread created" << std::endl;
	std::string run(s->getRun());

	int fds[2];
	pipe(fds);
	pid_t child = fork();
	if (!child) {
		// Set up file descriptors
		close(fds[1]);
		dup2(fds[0], 0);

		/*char run[s->getRun().size() + 1];
		strcpy(run, s->getRun().c_str());*/
		const char *argv[2] = { run.c_str(), NULL };
		//char *argv[2];
		/*argv[0] = run;
		argv[1] = NULL;*/

		// Become proper user/group
		setgid(s->getGroup());
		setuid(s->getUser());

		// Run user specified program
		if (execvp(argv[0], (char**)argv) == -1)
			std::cerr << "execvp error when trying to run " << argv[0] << "!" << std::endl;
		s->send("stop\n"); // Attempt to stop the server
		close(fds[0]); // Destroy the pipe
		return;
	}
	close(fds[0]);

	std::mutex *mtx = s->getMtx();
	std::condition_variable *cv = s->getCv();
	std::unique_lock<std::mutex> lck(*mtx);

	std::string command;
	bool stop = false;
	while (!stop) {
		while (!s->hasCommand())
			cv->wait(lck);
		command = s->popCommand();
		lck.unlock();
		if (command == "stop\n")
			stop = true;
		write(fds[1], command.c_str(), command.size());
		lck.lock();
	}
	lck.unlock();
	waitpid(child, NULL, 0);
	std::cout << "Thread exiting" << std::endl;
}
