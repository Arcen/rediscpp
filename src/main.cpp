#include "server.h"

int main(int argc, char *argv[])
{
	int thread = 3;
	std::string host = "127.0.0.1";
	std::string port = "6379";
	std::string config;
	for (int i = 1; i < argc; ++i) {
		if (*argv[i] == '-') {
			switch (argv[i][1]) {
			case 't':
				++i;
				if (i < argc) {
					thread = atoi(argv[i]);
				}
				break;
			case 'h':
				++i;
				if (i < argc) {
					host = argv[i];
				}
				break;
			case 'p':
				++i;
				if (i < argc) {
					port = argv[i];
				}
				break;
			}
		} else {
			config = argv[i];
		}
	}
	rediscpp::server_type server;
	if (!server.start(host, port, thread)) {
		return -1;
	}
	return 0;
}
