
This is microproxy, your ready-to-run, out-of-the-box, instant http proxy server.

Version: 1.0

Author: David McNab - david@rebirthing.co.nz - http://freeweb.sourceforge.net

--------------------------------------

Microproxy has two special features:

1) You can change the listening port, and/or assign or change a downstream proxy, in real-time

2) You can set the proxy in 'callback' mode, where on every http request, it will call a function of your choice. Your function can send back custom replies, or access data from another source,
or block certain websites (eg www.doubleclick.com), or log accesses, or whatever you want.

I've released it to the community to encourage programming creativity.

** Use it as a skeleton - a building block for you to create your own proxy. **

If you come up with an interesting use, please let me know: david@rebirthing.co.nz


The proxy itself is in proxy.c
demo.c illustrates a sample use.

These files compile under plain Windows 95/98/NT/2k, Windows cygwin and Linux.

Build notes:

- windows
    - you need to #define WINDOWS or add 'WINDOWS' to the preprocessor definitions
    - you need to link to wsock32.lib

- cygwin
    - it should build without any problems

- linux
    - on some linux distros, you'll need to link to /usr/lib/libpthread.a

Otherwise, it should build and run with no problems.

To use the demo, set your browser to use the proxy address 127.0.0.1 port 8888.
Try surfing common sites like Yahoo. Then, try www.microsoft.com and see what happens.

If you find any bugs in proxy.c, please let me know. A working patch would be appreciated, but
I'd be grateful to just know about the bug.

Enjoy! :)
