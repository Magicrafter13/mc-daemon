#ifndef SERVER_H
#define SERVER_H

#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class Server {
	// Config related variables
	std::string name;
	uid_t user = -1;
	gid_t group = -1;
	bool default_startup;
	std::vector<std::string> before;
	std::string run;
	std::vector<std::string> after;
	std::string notify;
	std::string path;

	// Thread related variables
	bool running = false;
	std::thread *thread;
	std::mutex *mtx;
	std::condition_variable *cv;
	std::queue<std::string> commands;
	int fds[2];

	// Thread utilities
	bool hasCommand();
	std::string popCommand();
	// Thread function
	void runServer();
	pid_t execute(std::vector<std::string>);

public:
	// Config related getters and setters
	                                          std::string getName();
	bool setUser(std::string);                uid_t getUser();
	bool setGroup(std::string);               gid_t getGroup();
	void setDefault(bool);                    bool defaultStartup();
	bool setBefore(std::vector<std::string>); std::vector<std::string> getBefore();
	bool setRun(std::string);                 std::string getRun();
	bool setAfter(std::vector<std::string>);  std::vector<std::string> getAfter();
	bool setNotify(std::string);              std::string getNotify();
	bool setPath(std::string);                std::string getPath();

	// Thread related getters
	std::mutex *getMtx();
	std::condition_variable *getCv();

	// Server communication/running
	bool start();
	bool restart();
	bool stop();
	void send(std::string);

	// Constructors and Destructors
	Server(std::string);
	~Server();
};

#endif
