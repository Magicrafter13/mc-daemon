#include <map>
#include <string>
#include "server.hpp"

class Config {
	bool parse_error;
	std::string path;
	std::map<std::string, Server*> servers;

public:
	bool error();
	std::map<std::string, Server*> getServers();
	bool parseConfigFile();

	Config(std::string);
	~Config();
};
