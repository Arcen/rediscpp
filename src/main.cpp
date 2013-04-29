#include "server.h"

int main(void)
{
	rediscpp::server_type server;
	if (!server.start("127.0.0.1", "6379")) {
		return -1;
	}
	return 0;
}
