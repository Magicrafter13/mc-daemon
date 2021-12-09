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
	std::vector<std::string> notify;

	// Thread related variables
	bool running = false;
	std::thread *thread;
	std::mutex *mtx;
	std::condition_variable *cv;
	std::queue<std::string> commands;

public:
	// Config related getters and setters
	                                          std::string getName();
	bool setUser(uid_t);                      uid_t getUser();
	bool setGroup(gid_t);                     gid_t getGroup();
	void setDefault(bool);                    bool defaultStartup();
	bool setBefore(std::vector<std::string>); std::vector<std::string> getBefore();
	bool setRun(std::string);                 std::string getRun();
	bool setAfter(std::vector<std::string>);  std::vector<std::string> getAfter();
	bool setNotify(std::vector<std::string>); std::vector<std::string> getNotify();

	// Thread related getters
	std::mutex *getMtx();
	std::condition_variable *getCv();
	// Other thread utilities
	bool hasCommand();
	std::string popCommand();

	// Server communication/running
	bool start(void (*)(Server*));
	bool restart();
	bool stop();
	void send(std::string);

	// Constructors and Destructors
	Server(std::string);
	~Server();
};
