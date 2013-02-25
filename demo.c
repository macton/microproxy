#ifdef WINDOWS
#include "winsock.h"
#endif

#include "stdio.h"

int proxy_callback(char *host, char *headers, int client_sock);

extern void proxy_start(int port, char *extproxyaddr, int extproxyport, int (*callback)(), int comeback, int singlethread);


//
// simple console prog which demonstrates the proxy
//
// as you can see below, it launches the proxy to listen on port 8888
//

main(int argc, char *argv[])
{
#ifdef WINDOWS
	// In Windows, we need to explicitly fire up the sockets interface
	WORD wVersionRequested;
	WSADATA wsaData;

	SetProcessShutdownParameters(0x100, SHUTDOWN_NORETRY);
	wVersionRequested = MAKEWORD(2, 0);
	if (WSAStartup(wVersionRequested, &wsaData) != 0) {
		exit(1);
	}
#endif

	//proxy_start(8888, "127.0.0.1", 8088, proxy_callback, 0);
	proxy_start(8888, NULL, 0, proxy_callback, 0, 0);
	return 0;
}


// this function gets called by the proxy.during every request
//
// warning - all code herein must be thread-safe, because it'll
// get called in all kinds of thread contexts
//
// You can form your own reply to send back to the browser, or just log the requests, or whatever.
//

int proxy_callback(char *host, char *headers, int client_sock)
{
	// sample intervention - prank redirection - just try to visit www.microsoft.com
	if (!strcmp(host, "www.microsoft.com"))
	{
		char *reply = "HTTP/1.0 302 Found\nConnection: close\nLocation: http://www.farts.com\n\n";
		send(client_sock, reply, strlen(reply), 0);
		return 1;
	}
	return 0;
}


