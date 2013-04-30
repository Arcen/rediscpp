#include "server.h"
#include <signal.h>

int main(void)
{
	signal(SIGPIPE, SIG_IGN);

	rediscpp::server_type server;
	if (!server.start("127.0.0.1", "6379")) {
		return -1;
	}
	return 0;
}
