#include "server.h"
#include <signal.h>

int main(int argc, char *argv[])
{
	signal(SIGPIPE, SIG_IGN);
	rediscpp::server_type server;
	int thread = 3;
	for (int i = 1; i < argc; ++i) {
		if (*argv[i] == '-') {
			switch (argv[i][1]) {
			case 't':
				++i;
				if (i < argc) {
					thread = atoi(argv[i]);
				}
				break;
			}
		}
	}
	if (!server.start("127.0.0.1", "6379", thread)) {
		return -1;
	}
	return 0;
}
