#include <fstream>
#include <grp.h>
#include <iostream>
#include <pwd.h>
#include <vector>
#include "config.hpp"

enum conf_key {
	ck_default,
	ck_user,
	ck_group,
	ck_path,
	ck_log,
	ck_before,
	ck_run,
	ck_after,
	ck_notify
};

struct conf_entry {
	size_t linenum;
	std::string value;
};

bool Config::error() {
	return parse_error;
}

std::map<std::string, Server*> Config::getServers() {
	return servers;
}

bool Config::parseConfigFile() {
	std::map<std::string, std::map<enum conf_key, struct conf_entry>> temp_config;

	std::ifstream conf_file(path, std::ios_base::in);
	std::string buffer, current_name;
	for (size_t line = 1; getline(conf_file, buffer), !conf_file.eof(); ++line) {
		// Ignore empty lines and comments
		if (buffer.empty() || buffer[0] == '#')
			continue;
		if (buffer[0] == '[') {
			if (buffer.back() != ']') {
				std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - expected ']', got '" << buffer[buffer.size()] << "'!" << std::endl;
				return false;
			}
			if (buffer[1] == '-') {
				std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - server name cannot start with '-'!" << std::endl;
				return false;
			}
			current_name = buffer.substr(1, buffer.size() - 2);
			if (temp_config.find(current_name) != temp_config.end()) {
				std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - A server named [" << current_name << "] was already defined!" << std::endl;
				return false;
			}
			temp_config[current_name];
		}
		else {
			if (current_name.empty()) {
				std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - no [server] block was defined yet!" << std::endl;
				return false;
			}
			std::string::size_type equals = buffer.find_first_of('=');
			if (equals == std::string::npos) {
				std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - no '=' found!" << std::endl;
				return false;
			}
			std::string key = buffer.substr(0, equals);
			std::string value = buffer.substr(equals + 1);
			enum conf_key ck;
			if (key == "default")
				ck = ck_default;
			else if (key == "user")
				ck = ck_user;
			else if (key == "group")
				ck = ck_group;
			else if (key == "path")
				ck = ck_path;
			else if (key == "log")
				ck = ck_log;
			else if (key == "before")
				ck = ck_before;
			else if (key == "run")
				ck = ck_run;
			else if (key == "after")
				ck = ck_after;
			else if (key == "notify")
				ck = ck_notify;
			else {
				std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " unknown key \"" << key << "\"!" << std::endl;
				return false;
			}
			auto orig_it = temp_config[current_name].find(ck);
			if (orig_it != temp_config[current_name].end()) {
				std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl;
				std::cerr << "On line " << line << " - redefinition of \"" << key << "\" as \"" << value << "\"!" << std::endl;
				std::cerr << "\tOriginally defined on line " << orig_it->second.linenum << " as \"" << orig_it->second.value << "\"." << std::endl;
				return false;
			}
			if (ck == ck_default && value != "yes" && value != "no") {
				std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << line << " - expected \"yes\" or \"no\", got \"" << value << "\"!" << std::endl;
				return false;
			}
			temp_config[current_name][ck] = (struct conf_entry){ .linenum = line, .value = value };
		}
	}
	conf_file.close();

	// Make sure necessary keys were included
	for (std::pair<std::string, std::map<enum conf_key, struct conf_entry>> block : temp_config) {
		auto end = block.second.end();
		// Check if we have a user
		if (block.second.find(ck_user) == end) {
			std::cerr << "Error in [" << block.first << "], no user defined!" << std::endl;
			return false;
		}
		// Now check if user is valid
		errno = 0;
		struct passwd *pwd_ent = getpwnam(block.second[ck_user].value.c_str());
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
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << block.second[ck_user].linenum << " - no such user with the name \"" << block.second[ck_user].value << "\"!" << std::endl;
			}
			return false;
		}
		temp_config[block.first][ck_user].value = std::to_string(pwd_ent->pw_uid);

		// Check if we have a group
		if (block.second.find(ck_group) == end) {
			std::cerr << "Error in [" << block.first << "], no group defined!" << std::endl;
			return false;
		}
		// Now check if group is valid
		errno = 0;
		struct group *grp_ent = getgrnam(block.second[ck_group].value.c_str());
		if (grp_ent == NULL) {
			switch (errno) {
				case EINTR:
				case EIO:
				case EMFILE:
				case ENFILE:
				case ENOMEM:
				case ERANGE:
					std::cerr << "getgrnam error (" << errno << ")" << std::endl;
					break;
				default:
					std::cerr << "Error reading /etc/mc-daemon.conf" << std::endl << "On line " << block.second[ck_group].linenum << " - no such group with the name \"" << block.second[ck_user].value << "\"!" << std::endl;
			}
			return false;
		}
		temp_config[block.first][ck_group].value = std::to_string(grp_ent->gr_gid);

		// Check if we have a path
		if (block.second.find(ck_path) == end) {
			std::cerr << "Error in [" << block.first << "], no path defined!" << std::endl;
			return false;
		}
	}

	// Set server values
	for (std::pair<std::string, std::map<enum conf_key, struct conf_entry>> block : temp_config) {
		bool exists = servers.find(block.first) != servers.end(), running = false;
		if (!exists)
			servers[block.first] = new Server(block.first);
		Server *s = servers[block.first];
		for (std::pair<enum conf_key, struct conf_entry> line : block.second) {
			std::string value = line.second.value;
			switch (line.first) {
				case ck_default:
					if (s->defaultStartup() == (value == "yes"))
						break;
					s->setDefault(value == "yes");
					break;
				case ck_user: {
					uid_t uid = std::stoi(value);
					if (s->getUser() == uid)
						break;
					if (s->setUser(uid))
						running = true;
					break;
				}
				case ck_group: {
					gid_t gid = std::stoi(value);
					if (s->getGroup() == gid)
						break;
					if (s->setGroup(gid))
						running = true;
					break;
				}
				case ck_path:
					if (s->getPath() == value)
						break;
					if (s->setPath(value))
						running = true;
					break;
				case ck_log:
					if (s->getLog() == value)
						break;
					if (s->setLog(value))
						running = true;
					break;
				case ck_before: {
					std::vector<std::string> before_argv;
					while (!value.empty()) {
						std::string::size_type space = value.find_first_of(' ');
						before_argv.push_back(value.substr(0, space));
						value.erase(0, space == std::string::npos ? space : space + 1);
					}
					if (s->getBefore() == before_argv)
						break;
					s->setBefore(before_argv);
					break;
				}
				case ck_run:
					if (s->getRun() == value)
						break;
					if (s->setRun(value))
						running = true;
					break;
				case ck_after: {
					std::vector<std::string> after_argv;
					while (!value.empty()) {
						std::string::size_type space = value.find_first_of(' ');
						after_argv.push_back(value.substr(0, space));
						value.erase(0, space == std::string::npos ? space : space + 1);
					}
					if (s->getAfter() == after_argv)
						break;
					s->setAfter(after_argv);
					break;
				}
				case ck_notify:
					if (s->getNotify() == value)
						break;
					s->setNotify(value);
					break;
			}
		}
		if (running)
			s->start();
	}
	for (auto block : servers) {
		if (temp_config.find(block.first) == temp_config.end()) {
			std::cout << "[" << block.first << "] is no longer in the config file!" << std::endl;
			block.second->stop();
			delete servers[block.first];
			servers.erase(block.first);
		}
	}

	return true;
}

Config::Config(std::string path) {
	this->path = path;

	parse_error = !parseConfigFile();
}

Config::~Config() {
	for (std::pair<std::string, Server*> block : servers)
		delete block.second;
}
