//
// proxy.c : a minimal http proxy server
//
// CopyLeft (c) May 2001, by David McNab - david@rebirthing.co.nz, http://freeweb.sf.org
// Released subject to GNU General Public License.
//
// This module implements a minimal transparent http proxy server, with two key features:
// 1) Proxy's listening port, as well as hostname and port of downstream proxy server, can
//     be easily changed in real time
// 2) It's easy to plug in a 'callback' function, which allows the caller to intervene in all http
//	   requests. This allows the caller to block certain websites, or redirect http requests to
//     other sources of data, such as Freenet
//
// You shouldn't need to modify this code - it should offer you enough flexibility to meet your needs.
// But if you come up with any useful mods, I'd be warmly grateful for an email from you 
// You are free to use, modify and redistrbute this source code, and any binaries, obj modules or
// libraries which use this code, subject to the following conditions:
//
// USAGE CONDITIONS
//
// 1) You must include in your source code the name, email address and website of the author (see above)
// 2) If you modify the code, you must note in this header that it is modified by you, and briefly state your modifications
// 3) You must not distribute any programs which use this code, unless you supply convenient access to
//     full, current, working and buildable source code for such programs
//
// Failure to observe these modest conditions will render you a complete twat, and cause you to set up
// universal forces which will culminate in you finding yourself in numerous humiliating predicaments which
// others will find extremely amusing at your expense.
// 

#include "stdio.h"
#include "string.h"

#ifdef WINDOWS

// Windows-specific wrappers for socket functions

#include "winsock2.h"

#define SockSend(sd, buf, len)			send(sd, buf, len, 0)
#define SockReceive(sd, buf, len)		recv(sd, buf, len, 0)
#define SockClose(sd)						closesocket(sd)
#define SockAddrType					struct sockaddr
#define SockError(msg)					MessageBox(0,msg,"Proxy server problem", MB_SYSTEMMODAL)
#define SockLastError()					 WSAGetLastError()
#define SockErrWouldBlock			  WSAEWOULDBLOCK
#define SockSleep()							Sleep(100L)

#define Strnicmp(s1, s2, n)					strnicmp(s1, s2, n)

#else

// Linux/Cygwin-specific wrappers for socket functions

#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>

typedef int SOCKET;
typedef unsigned int UINT;
#define INVALID_SOCKET  (SOCKET)(~0)
#define SOCKET_ERROR            (-1)

#define SockSend(sd, buf, len)			write(sd, buf, len)
#define SockReceive(sd, buf, len)		read(sd, buf, len)
#define SockClose(sd)						close(sd)
#define SockAddrType					struct sockaddr_in
#define SockError(msg)					  fprintf(stderr, msg)
#define SockLastError()					 (errno)
#define SockErrWouldBlock			  EWOULDBLOCK
#define SockSleep()							usleep(200000)

#define Strnicmp(s1, s2, n)					strncasecmp(s1, s2, n)

#endif		// platform-specific wrappers for socket functions


//
// EXPORTED DECLARATIONS
//

void proxy_start(int listenPortparm, char *extproxyaddr, int extproxyport, int (*callback_fn)(), int singlethread);
void proxy_newport(int newport);

//
// PRIVATE DECLARATIONS
//

static int		proxy_server();
static UINT		proxy_handlereq(SOCKET socket);
static int		proxy_sockgets(SOCKET sock, char *buf, int len);

static int		proxy_listenport;
static int		proxy_singlethread;
static char	  *proxy_extproxyaddr = NULL;
static int		proxy_extproxyport = 80;

static int		(*proxy_callback_fn)(char *host, char *headers, int client_sock);

static void		LaunchThread(int (*func)(), void *parg);

static int		proxy_getaddr(char* HostName, int Port, SockAddrType *Result);

/////////////////////////////////////////////////////////////////////////
//
//   EXPORTED FUNCTIONS
//
/////////////////////////////////////////////////////////////////////////

//
// Function:		proxy_start()
//
// Arguments:	listenPortParm		port on which this proxy listens for http requests
//						extproxyaddr		string - hostname of downstream proxy, or NULL if none
//						extproxyport		 port number of downstream proxy, disregarded if extproxyhost is NULL
//						callback_fn			 pointer to function to call to intercept requests, or NULL if none
//						comeback			1 to return immediately to caller and run proxy as a separate thread
//													0 to never return to the caller
//						singlethread		1 to prevent proxy from launching separate threads for each request.
//													useful for debugging, but makes the proxy very slow
//
// Returns:			nothing
//

void proxy_start(int listenport, char *extproxyaddr, int extproxyport, int (*callback_fn)(), int comeback, int singlethread)
{
	//printf("proxy_start: listenport = %d\n", listenport);

	proxy_listenport = listenport;
	proxy_extproxyaddr = extproxyaddr != NULL ? strdup(extproxyaddr) : NULL;
	proxy_extproxyport = extproxyport;
	proxy_callback_fn = callback_fn;

	//proxy_singlethread = 1;
	proxy_singlethread = singlethread;

	if (comeback)
		LaunchThread(proxy_server, NULL);
	else
		proxy_server(NULL);

}


// function which allows client to set a new listening port

void proxy_newport(int newport)
{
	proxy_listenport = newport;
}

void proxy_newextproxy(char *addr, int port)
{
	proxy_extproxyaddr = strdup(addr);
	proxy_extproxyport = port;
}


//
// main thread which listens for incoming http connections, and launches a thread pair
// when a connection comes in
//
static int proxy_server(void *dummy)
{
	int						Status;
	SOCKET			  sock_listen, sock_client;
	int						oldListenPort;
	char				  *oldextproxyaddr;
	int						lastError;
	unsigned long	nbParm = 1000;
	int						one = 1;

	SockAddrType	sockAddr;

	// loop around waiting for client connection or change of listen port
	while (1)
	{
		// create the socket
		oldListenPort = proxy_listenport;
		oldextproxyaddr = proxy_extproxyaddr;

		if (proxy_getaddr("127.0.0.1", proxy_listenport, &sockAddr)  == 0)
		{
			SockError("proxy_server(): failed to get socket address");
			return 1;
		}

		// bind the socket
	    sock_listen = socket(AF_INET, SOCK_STREAM, 0);
		//Status = setsockopt (sock_listen, IPPROTO_TCP, TCP_NODELAY, (char * ) &one, sizeof (int));
	    Status = bind(sock_listen, &sockAddr, sizeof(sockAddr));
	    if(Status < 0)
		{
			SockError("proxy_server(): Failed to connect socket for receiving connections");
			return 1;
		}

		// set socket to non-blocking mode
		if ((Status = SockBlock(sock_listen, 1)) < 0)
		{
			SockError("proxy_server(): Failed to set non-block mode on socket");
			return 1;
		}

		// set socket to listen
		if (listen(sock_listen, SOMAXCONN) != 0)
		{
			SockError("proxy_server(): Failed to listening mode on socket");
			return 1;
		}

		//printf("about to start awaiting connections\n");

		// loop accepting connections with timeout
		while (1)
		{
			// if external proxy addr has changed, ditch the strdup()'ed string
			if (oldextproxyaddr != proxy_extproxyaddr)
			{
				if (oldextproxyaddr != NULL)
					free(oldextproxyaddr);
				oldextproxyaddr = proxy_extproxyaddr;
			}

			// has someone changed the listening port?
			if (oldListenPort != proxy_listenport)
			{
				SockClose(sock_listen);
				break;		// fall out to top of outer loop, set up another socket on new port
			}

			// check for incoming connections
			if ((sock_client = accept(sock_listen, NULL, NULL)) == INVALID_SOCKET)
			{
				if (SockLastError() != SockErrWouldBlock)
				{
					SockError("proxy_server(): accept() call failed");
					return 1;
				}
				else
				{
					// no connections
					SockSleep();
					continue;
				}
			}

			// there's a new connection
			//printf("got connection\n");
			if (proxy_singlethread)
				proxy_handlereq(sock_client); // single-thread mode
			else
			{
				// spawn a thread to handle request
				// p_thrdsecattr initstacksize threadfn  threadarg creationflags  p_retthreadid
				LaunchThread(proxy_handlereq, sock_client);
			}
			//MessageBox(0, "Got incoming connection", "FreeWeb Proxy", MB_SYSTEMMODAL);
		}
		// don't add any statements after this inner loop!
	}			// 'while (1)'
	return 0;	// how on earth did we get here???
}			// 'proxyServer()'


//
// thread which handles a single http request
//
static int proxy_handlereq(SOCKET sock_client)
{
	SOCKET sock_server;

	char buf[65536];
	char header_line[1024];
	char host[256];
	int		serverPort = 80;
    struct sockaddr     serverSockAddr;

	int gotHeader = 0;
	int count;
	int Status;
	unsigned long nbParm = 0L;
	char newline = '\n';
	fd_set rfds;
	int n, maxfd;

	int one = 1;

	buf[0] = '\0';
	host[0] = '\0';

	// set client socket non-blocking
	if ((Status = SockBlock(sock_client, 0)) < 0)
	{
		SockError("proxy_handlereq(): Failed to clear non-block mode on socket");
		return 1;
	}

	// now get headers from client
	while ((count = proxy_sockgets(sock_client, header_line, 1024)) > 0)
	{
		gotHeader = 1;

		// look for 'Host:' header
		if (!Strnicmp(header_line, "host: ", 6))
		{
			char *pPort;

			strcpy(host, header_line + 6);
			if ((pPort = strchr(host, ':')) != NULL)
			{
				*pPort++ = '\0';
				serverPort = atoi(pPort);
			}
		}

		//MessageBox(0, header_line, "Req header", MB_SYSTEMMODAL);
		strcat(buf, header_line);
		strcat(buf, "\n");
	}

	// add second line terminator to mark end of header
	strcat(buf, "\n");

	// bail if nothing came through
	if (!gotHeader)
	{
		SockClose(sock_client);
		return 1;
	}

	// did we get a host address?
	if (host[0] == '\0')
	{
		char err400[] = "HTTP/1.0 400 Invalid header received from browser\n\n";
		SockSend(sock_client, err400, strlen(err400));
		SockClose(sock_client);
		SockError("proxy_handlereq(): host missing from http header");
		return 1;
	}

	// allow callback function to intercept
	if (proxy_callback_fn != NULL)
		if ((*proxy_callback_fn)(host, buf, sock_client) != 0)
		{
			// callback took over
			SockClose(sock_client);
			return 0;
		}

	//MessageBox(0, buf, "Header", MB_SYSTEMMODAL);

	// change host and portnum if using downstream proxy
	if (proxy_extproxyaddr != NULL)
	{
		strcpy(host, proxy_extproxyaddr);
		serverPort = proxy_extproxyport;
	}

	// try to find server
    if (proxy_getaddr(host, serverPort, &serverSockAddr) == 0)
	{
		SockSend(sock_client, "404 Host Not Found\n\n", 20);
		SockClose(sock_client);
		return 1;
	}

    sock_server = socket(AF_INET, SOCK_STREAM, 0);
#ifdef WINDOWS
	setsockopt(sock_server, IPPROTO_TCP, TCP_NODELAY, (char * ) &one, sizeof (int));
#endif

	// try to connect to server
    if ((Status = connect(sock_server, &serverSockAddr, sizeof(serverSockAddr))) < 0)
	{
		SockSend(sock_client, "404 Host Not Found\n\n", 20);
		SockClose(sock_client);
		return 1;
	}

	// send client's req to server
	SockSend(sock_server, buf, strlen(buf));

	//
	// now loop around relaying stuff between server and client
	//

	maxfd = (sock_client > sock_server ) ? sock_client : sock_server;
	for(;;)
	{
		FD_ZERO(&rfds);
		FD_SET(sock_client, &rfds);
		FD_SET(sock_server, &rfds);

		if ((n = select(maxfd+1, &rfds, NULL, NULL, NULL)) < 0)
		{
			SockError("proxy_handlereq(): select() failed while handling http req");
			//fprintf(logfp, "%s: select() failed!: ", prog);
			//fperror(logfp, "");
			return 1;
		}

		// got data from client - relay to server
		if(FD_ISSET(sock_client, &rfds))
		{
			if ((n = SockReceive(sock_client, buf, sizeof(buf))) <= 0)
				break;  // end of request

			if (SockSend(sock_server, buf, n) != n)
			{
				//fprintf(logfp, "%s: write to: %s failed: ",	prog, http->host);
				//fperror(logfp, "");
				return 1;
			}
			continue;
		}

		// got data from server - relay to client
		if (FD_ISSET(sock_server, &rfds))
		{
			if ((n = SockReceive(sock_server, buf, sizeof(buf))) < 0)
			{
				//fprintf(logfp, "%s: read from: %s failed: ", prog, http->host);
				//fperror(logfp, "");

//				eno = safe_strerror(errno);
//				sprintf(buf, CFAIL, http->hostport, eno);
//				freez(eno);
				//write_socket(m_SockClient, buf, strlen(buf), 0);
				return 1;
			}

			if (n == 0)
				break;		// got all from server

			/* just write */
			if (SockSend(sock_client, buf, n) != n)
			{
				//fprintf(logfp, "%s: write to client failed: ",	prog);
				//fperror(logfp, "");
				return 1;
			}
			continue;

		}	// 'if (got something from server)'
	}		// 'for (;;)' - relaying data between client and server

	// all done - close sockets and terminate this thread
	SockClose(sock_client);
	SockClose(sock_server);

	// see you later
	return 0;

}		// 'proxy_handlereq()'



static int proxy_sockgets(SOCKET sock, char *buf, int len)
{
	char *ptr = buf;
	char *ptr_end = ptr + len - 1;
	int error;

	while (ptr < ptr_end)
	{
		switch (SockReceive(sock, ptr, 1))
		{
		case 1:
			// got a char
			if (*ptr == '\r')
				continue;
			else if (*ptr == '\n')
			{
				*ptr = '\0';
				return ptr - buf;
			}
			else
			{
				ptr++;
				continue;
			}
		case 0:
			// connection closed at other end
			*ptr = '\0';
			return ptr - buf;
		default:
			// failure
#ifdef WINDOWS
			error = WSAGetLastError();
#endif
			return -1;
		}		// 'switch (recv)'
	}			// 'while ()'

	// buffer full - return what we've got
	return len;

}			// 'SockGets()'


static int proxy_getaddr(char *HostName, int Port, SockAddrType *Result)
{
    struct hostent*     Host;

#ifdef WINDOWS
    SOCKADDR_IN      Address;

    memset(Result, 0, sizeof(*Result));
    memset(&Address, 0, sizeof(Address));

    if ((Host = gethostbyname(HostName)) != NULL)
    {
        Address.sin_family  = AF_INET;
        Address.sin_port    = htons((short)Port);
        memcpy(&Address.sin_addr, Host->h_addr_list[0], Host->h_length);
        memcpy(Result, &Address, sizeof(Address));
    }

#else
	Result->sin_family=AF_INET;
	Result->sin_port=htons((unsigned short)Port);

	Host=gethostbyname(HostName);

	if(!Host)
	{
		unsigned long int addr=inet_addr(HostName);
		if(addr!=-1)
			Host=gethostbyaddr((char*)addr,sizeof(addr),AF_INET);

	    if(!Host)
	    {
			if(errno!=ETIMEDOUT)
				errno=-1; /* use h_errno */
			printf("Unknown host '%s' for server [%!s].", HostName);
			return(0);
		}
	}

	memcpy((char*)&Result->sin_addr,(char*)Host->h_addr,sizeof(Result->sin_addr));
#endif

    return Host != NULL;
}


static void LaunchThread(int (*func)(), void *parg)
{
#ifdef WINDOWS
	LPDWORD			tid;
	CreateThread(NULL, 0L, (void *)func, parg, 0L, &tid);
#else
	pthread_t               pth;
	pthread_create(&pth, NULL, func, parg);
#endif

}		// 'LaunchThread()'


static int SockBlock(int sock, int onoff)
{
#ifdef WINDOWS
	unsigned long	nbParm = onoff;
	return ioctlsocket(sock, FIONBIO, &nbParm);
#else
	return fcntl(sock, F_SETFL, (onoff ? O_NONBLOCK : 0));
#endif
}

