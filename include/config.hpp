#include <string>
#include <vector>
#include "server.hpp"

class Config {
	bool parse_error;
	std::string path;
	std::vector<Server*> servers;

public:
	bool error();
	std::vector<Server*> getServers();
	Config(std::string);
	~Config();
};
