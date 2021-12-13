#include <fstream>
#include <iostream>
#include "config.hpp"

bool Config::error() {
	return parse_error;
}

std::vector<Server*> Config::getServers() {
	return servers;
}

Config::Config(std::string path) {
	parse_error = false;
	this->path = path;

	std::ifstream conf_file(path, std::ios_base::in);
	std::string buffer;
	for (size_t line = 1; getline(conf_file, buffer), !conf_file.eof(); ++line) {
		// Ignore empty lines and comments
		if (buffer.empty() || buffer[0] == '#')
			continue;
		if (buffer[0] == '[') {
			if (buffer.back() != ']') {
				std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - expected ']', got '" << buffer[buffer.size()] << "'!" << std::endl;
				parse_error = true;
				return;
			}
			if (buffer[1] == '-') {
				std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - server name cannot start with '-'!" << std::endl;
				parse_error = true;
				return;
			}
			servers.push_back(new Server(buffer.substr(1, buffer.size() - 2)));
		}
		else {
			if (servers.empty()) {
				std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - no [server] block was defined yet!" << std::endl;
				parse_error = true;
				return;
			}
			std::string::size_type equals = buffer.find_first_of('=');
			if (equals == std::string::npos) {
				std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - no '=' found!" << std::endl;
				parse_error = true;
				return;
			}
			std::string key = buffer.substr(0, equals);
			std::string value = buffer.substr(equals + 1);
			if (key == "default") {
				if (value != "yes" && value != "no") {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - expected \"yes\" or \"no\", got \"" << value << "\"!" << std::endl;
					parse_error = true;
					return;
				}
				servers.back()->setDefault(value == "yes");
			}
			else if (key == "user") {
				if (!servers.back()->setUser(value)) {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - key \"user\" was already defined!" << std::endl;
					parse_error = true;
					return;
				}
			}
			else if (key == "group") {
				if (!servers.back()->setGroup(value)) {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - key \"group\" was already defined!" << std::endl;
					parse_error = true;
					return;
				}
			}
			else if (key == "path") {
				if (!servers.back()->setPath(value)) {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - key \"path\" was already defined!" << std::endl;
					parse_error = true;
					return;
				}
			}
			else if (key == "log") {
				if (!servers.back()->setLog(value)) {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - key \"log\" was already defined!" << std::endl;
					parse_error = true;
					return;
				}
			}
			else if (key == "before") {
				std::vector<std::string> server_argv;
				while (!value.empty()) {
					std::string::size_type space = value.find_first_of(' ');
					server_argv.push_back(value.substr(0, space));
					value.erase(0, space == std::string::npos ? space : space + 1);
				}
				if (!servers.back()->setBefore(server_argv)) {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - key \"before\" was already defined!" << std::endl;
					parse_error = true;
					return;
				}
			}
			else if (key == "run") {
				if (!servers.back()->setRun(value)) {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - key \"run\" was already defined!" << std::endl;
					parse_error = true;
					return;
				}
			}
			else if (key == "after") {
				std::vector<std::string> server_argv;
				while (!value.empty()) {
					std::string::size_type space = value.find_first_of(' ');
					server_argv.push_back(value.substr(0, space));
					value.erase(0, space == std::string::npos ? space : space + 1);
				}
				if (!servers.back()->setAfter(server_argv)) {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - key \"after\" was already defined!" << std::endl;
					parse_error = true;
					return;
				}
			}
			else if (key == "notify") {
				if (!servers.back()->setNotify(value)) {
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - key \"notify\" was already defined!" << std::endl;
					parse_error = true;
					return;
				}
			}
		}
	}
	conf_file.close();
	for (Server *&s : servers) {
		if (s->getUser() == (uid_t)-1) {
			std::cerr << "Error in [" << s->getName() << "], no user defined!" << std::endl;
			parse_error = true;
			return;
		}
		else if (s->getGroup() == (gid_t)-1) {
			std::cerr << "Error in [" << s->getName() << "], no group defined!" << std::endl;
			parse_error = true;
			return;
		}
		else if (s->getPath().empty()) {
			std::cerr << "Error in [" << s->getName() << "], no path defined!" << std::endl;
			parse_error = true;
			return;
		}
	}
}

Config::~Config() {
	for (Server *s : servers)
		delete s;
}
