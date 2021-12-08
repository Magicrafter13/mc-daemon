#include <condition_variable>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdio.h>
#include <string>
//#include <systemd/sd-daemon.h
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

enum _cmd_t {
	start,
	restart,
	stop,
	test,
	daemonize,
};
typedef enum _cmd_t Command_t;

struct _cmd {
	Command_t type;
	std::string server_name;
};
typedef struct _cmd Command;

struct _srv {
	std::string name;
	bool default_startup;
	std::vector<std::string> before;
	std::string run;
	std::vector<std::string> after;
	std::vector<std::string> notify;
	
	std::thread *thread;
	std::string command;
	std::mutex *mtx;
	std::condition_variable *cv;
};
typedef struct _srv Server;

const char default_file[] = "minecraft";
const char default_dir[] = "/usr/share/minecraft/";

std::vector<Server> *parseConfig();
int parseCommand(std::vector<Server>*, Command);
static void runServer(Server*);

int main(int argc, char *argv[]) {
	std::vector<Command> commands;
	char *custom_file = NULL;
	char *custom_dir = NULL;

	// Parse arguments
	for (int arg = 1; arg < argc; ++arg) {
		std::string argument(argv[arg]);
		bool server_action = false;
		if (argument == "--start") {
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
		else if (argument == "--test") {
			if (commands.empty())
				commands.push_back((Command){ .type = test });
			else {
				write(1, "--test cannot be used with other arguments\n", 43);
				return 1;
			}
		}
		else if (argument == "--daemon") {
			if (commands.size()) {
				Command first = commands[0];
				commands[0] = (Command){ .type = daemonize };
				commands.push_back(first);
			}
			else
				commands.push_back((Command){ .type = daemonize });
		}
		else
			custom_file = argv[arg];
		if (server_action)
			if (argv[arg + 1] != NULL && argv[arg + 1][0] != '-')
				commands.back().server_name = argv[arg + 1];
	}
	if (commands.empty())
		commands.push_back((Command){ .type = start });

	std::vector<Server> *config = parseConfig();
	// Parse config file
	if (commands[0].type == test) {
		if (config != nullptr) {
			std::cout << "Config file OK." << std::endl;
			delete config;
		}
		return 0;
	}

	// Get socket filename
	char sockpath[strlen(custom_file == NULL ? default_file : custom_file) + 6]; // 5 for "/tmp/", 1 for '\0'
	sockpath[0] = '\0';
	strcat(sockpath, "/tmp/");
	strcat(sockpath, custom_file == NULL ? default_file : custom_file);

	// Create socket data
	struct sockaddr_un sock;
	sock.sun_family = AF_UNIX;
	strcpy(sock.sun_path, "/run/mc-daemon/socket");
	int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);

	// Check if socket already exists
	if (commands[0].type != daemonize) {
	//if (access("/run/mc-daemon/socket", F_OK) == 0) {
		if (connect(sockfd, (struct sockaddr*)&sock, sizeof (sock)) == -1) {
			write(2, "connect error\n", 14);
			close(sockfd);
			delete config;
			return errno;
		}
		bool done = false;
		int error = 0;
		// Send command to daemon
		for (Command c : commands) {
			switch (c.type) {
				case start: {
					std::string message = "start" + (c.server_name.empty() ? "" : " " + c.server_name) + "\n";
					write(sockfd, message.c_str(), message.size());
					break;
				}
				case restart: {
					std::string message = "restart" + (c.server_name.empty() ? "" : " " + c.server_name) + "\n";
					write(sockfd, message.c_str(), message.size());
					break;
				}
				case stop: {
					std::string message = "stop" + (c.server_name.empty() ? "" : " " + c.server_name) + "\n";
					write(sockfd, message.c_str(), message.size());
					break;
				}
				case test:
					write(2, "--test did not exit after testing!\n", 35);
					done = true;
					error = 1;
			}
			if (done)
				break;
		}
		close(sockfd);
		delete config;
		return error;
	}
	// Create daemon
	pid_t daemon = fork();
	if (daemon) {
		close(sockfd);
		write(1, "Started daemon\n", 15);
		std::ofstream pid_file("/run/mc-daemon/pid", std::ios_base::out);
		pid_file << daemon << std::endl;
		delete config;
		return 0;
	}
	// Newly forked daemon will execute the following code
	if (bind(sockfd, (struct sockaddr*)&sock, sizeof (sock)) == -1) {
		write(2, "bind error\n", 11);
		close(sockfd);
		delete config;
		return errno;
	}
	if (listen(sockfd, 0) == -1) {
		write(2, "listen error\n", 13);
		close(sockfd);
		unlink("/run/mc-daemon/socket");
		delete config;
		return errno;
	}
	// Start default servers
	std::cout << "Config has " << config->size() << " servers." << std::endl;
	for (std::vector<Server>::iterator s = config->begin(); s != config->end(); ++s) {
		std::cout << "Server name: " << s->name << std::endl;
		if (s->default_startup) {
			std::cout << "Creating thread..." << std::endl;
			s->thread = new std::thread(runServer, &*s);
		}
	}
	// Parse any command line options given
	for (Command c : commands)
		parseCommand(config, c);
	// Act as daemon
	char buffer[512];
	while (1) {
		int connectfd = accept(sockfd, NULL, NULL);
		if (connectfd == -1) {
			write(2, "accept error\n", 13);
			close(sockfd);
			unlink("/run/mc-daemon/socket");
			return errno;
		}
		memset(buffer, '\0', 512);
		//printf("daemon accepted connection\n");
		read(connectfd, buffer, 512);
		close(connectfd);
		if (!strcmp(buffer, "restart\n"))
			;// do something
		else if (!strcmp(buffer, "stop\n"))
			break;
	}
	write(1, "Daemon dying\n", 13);
	close(sockfd);
	unlink("/run/mc-daemon/socket");
}

std::vector<Server> *parseConfig() {
	std::vector<Server> *config = new std::vector<Server>;
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
				return nullptr;
			}
			if (buffer[1] == '-') {
				std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - server name cannot start with '-'!" << std::endl;
				return nullptr;
			}
			Server s;
			s.name = buffer.substr(1, buffer.size() - 2);
			config->push_back(s);
		}
		else {
			if (config->empty()) {
				std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - no [server] block was defined yet!" << std::endl;
				return nullptr;
			}
			std::string::size_type equals = buffer.find_first_of('=');
			if (equals == std::string::npos) {
				std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - no '=' found!" << std::endl;
				return nullptr;
			}
			std::string key = buffer.substr(0, equals);
			std::string value = buffer.substr(equals + 1);
			if (key == "default") {
				if (value != "yes" && value != "no") {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - expected \"yes\" or \"no\", got \"" << value << "\"!" << std::endl;
					return nullptr;
				}
				config->back().default_startup = value == "yes";
			}
			else if (key == "before") {
				if (!config->back().before.empty()) {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - key \"before\" was already defined!" << std::endl;
					return nullptr;
				}
				while (!value.empty()) {
					std::string::size_type space = value.find_first_of(' ');
					config->back().before.push_back(value.substr(0, space));
					value.erase(0, space == std::string::npos ? space : space + 1);
				}
			}
			else if (key == "run") {
				if (!config->back().run.empty()) {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - key \"run\" was already defined!" << std::endl;
					return nullptr;
				}
				config->back().run = value;
			}
			else if (key == "after") {
				if (!config->back().after.empty()) {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - key \"after\" was already defined!" << std::endl;
					return nullptr;
				}
				while (!value.empty()) {
					std::string::size_type space = value.find_first_of(' ');
					config->back().after.push_back(value.substr(0, space));
					value.erase(0, space == std::string::npos ? space : space + 1);
				}
			}
			else if (key == "notify") {
				if (!config->back().notify.empty()) {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - key \"notify\" was already defined!" << std::endl;
					return nullptr;
				}
				while (!value.empty()) {
					std::string::size_type space = value.find_first_of(' ');
					config->back().notify.push_back(value.substr(0, space));
					value.erase(0, space == std::string::npos ? space : space + 1);
				}
			}
		}
	}
	conf_file.close();
	return config;
}

int parseCommand(std::vector<Server> *config, Command c) {
	return 0;
}

static void runServer(Server *s) {
	std::cout << "Thread created" << std::endl;
	char *argv[2];
	argv[0] = (char*)s->run.c_str();
	argv[1] = NULL;

	int fds[2];
	pipe(fds);
	pid_t child = fork();
	if (!child) {
		close(fds[1]);
		if (execvp(argv[0], argv) == -1)
			std::cerr << "execvp error when trying to run " << argv[0] << "!" << std::endl;
		return;
	}

	close(fds[0]);
	s->mtx = new std::mutex;
	s->cv = new std::condition_variable;
	bool stop = false;
	std::unique_lock<std::mutex> lck(*s->mtx);
	while (!stop) {
		while (s->command.empty())
			s->cv->wait(lck);
		if (s->command == "stop")
			stop = true;
		write(fds[1], s->command.c_str(), s->command.size());
		lck.lock();
	}
	waitpid(child, NULL, 0);
	delete s->cv;
	delete s->mtx;
	std::cout << "Thread exiting" << std::endl;
}
