/*
 * net_demo.c — fbcurses UDP remote-rendering server demo.
 *
 * Starts the UDP server, runs a self-test that exercises the full
 * protocol, then enters fbNetRun() so external clients can drive
 * the framebuffer display.
 *
 * Build:
 *   make && sudo ./net_demo
 *
 * ── Protocol quick reference ────────────────────────────────────
 *
 *   Each UDP datagram holds one or more newline-separated commands:
 *     COMMAND,arg1,arg2,...\n
 *
 *   win_new takes exactly 4 args: col, row, cols, rows
 *   The server replies "ok,win_new,ID" — use that ID in later commands.
 *
 * ── Shell examples (send to the idle status window, ID 1) ───────
 *
 *   # Ping
 *   echo "ping" | nc -u -q1 127.0.0.1 9876
 *
 *   # Create a new window (server replies with the assigned ID)
 *   echo "win_new,2,2,60,12" | nc -u -q1 127.0.0.1 9876
 *   # -> ok,win_new,2
 *
 *   # Write to window 2 and flush — all in one datagram
 *   printf "colors,2,#00FF88,black\nprint_at,2,4,4,Hello UDP!\nwin_refresh,2\nflush\n" \
 *     | nc -u -q1 127.0.0.1 9876
 *
 *   # Progress bar animation on window 1
 *   for i in $(seq 0 5 100); do
 *     printf "progress,1,2,3,56,$i,#3399FF,#222244,1\nwin_refresh,1\nflush\n" \
 *       | nc -u -q1 127.0.0.1 9876
 *     sleep 0.1
 *   done
 *
 *   # Large font text
 *   echo "print_px,50,100,Hello,bright_cyan,black,none,16x32" \
 *     | nc -u -q1 127.0.0.1 9876
 *   echo "flush" | nc -u -q1 127.0.0.1 9876
 *
 *   # Toast notification
 *   echo "toast,success,2000,Deployment complete!" | nc -u -q1 127.0.0.1 9876
 *   echo "flush" | nc -u -q1 127.0.0.1 9876
 *
 * Press Esc or 'q' on the local console to exit.
 */

#define _POSIX_C_SOURCE 200809L
#include "fbcurses.h"
#include "fbnet.h"
#include "fbnet_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>

int doSelfTest = 0;

static void msleep(int ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* Send a UDP packet to localhost and optionally print the reply */
static void send_cmd(int sock, uint16_t port, const char *cmd, bool printReply)
{
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family      = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port        = htons(port);

    sendto(sock, cmd, strlen(cmd), 0, (struct sockaddr *)&dst, sizeof(dst));

    if (printReply) {
        /* Short wait for reply */
        fd_set fds; FD_ZERO(&fds); FD_SET(sock, &fds);
        struct timeval tv = {0, 50000};
        if (select(sock+1, &fds, NULL, NULL, &tv) > 0) {
            char reply[512];
            ssize_t n = recv(sock, reply, sizeof(reply)-1, 0);
            if (n > 0) { reply[n]='\0'; printf("  reply: %s", reply); }
        }
    }
}

/* ── Self-test: drive the server from the same process ───────────────
   Sends UDP packets to the local server socket and calls fbNetProcess
   after each one so they are handled in-process.                     */
static void runSelfTest(uint16_t port, fbNetServer *srv, fbScreen *scr)
{
    /* Client socket bound to any ephemeral port */
    int csock = socket(AF_INET, SOCK_DGRAM, 0);
    if (csock < 0) { perror("client socket"); return; }

    struct sockaddr_in local = {0};
    local.sin_family      = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port        = 0;
    bind(csock, (struct sockaddr *)&local, sizeof(local));

    struct timeval tv = {0, 100000};
    setsockopt(csock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    printf("[net_demo] Running self-test on port %u\n", port);

/* Send a command and immediately process it server-side */
#define CMD(str) \
    send_cmd(csock, port, str, true); \
    fbNetProcess(srv);                \
    fbFlush(scr);                     \
    msleep(80)

    /* ── Connectivity check ────────────────────────────────────── */
    CMD("ping");
    CMD("screen_size");
    CMD("version");
    CMD("fonts");

    /* ── Create main demonstration window ──────────────────────── */
    /* win_new takes exactly 4 args: col,row,cols,rows              */
    CMD("win_new,1,1,78,22");
    /* Server assigns ID 1 — all commands below use that handle     */

    CMD("title_bar,1,\"fbcurses UDP Remote Rendering\",double,cyan,black,cyan");
    CMD("colors,1,white,black");
    CMD("attr,1,none");
    CMD("print_at,1,2,3,\"fbcurses UDP server — send commands from another terminal!\"");
    CMD("print_at,1,2,4,\"Every command is CSV over UDP on port 9876\"");
    CMD("win_refresh,1");
    CMD("flush");
    msleep(300);

    /* ── Colour palette ─────────────────────────────────────────── */
    CMD("print_at,1,2,6,\"Colour palette:\"");
    CMD("attr,1,bold");
    {
        static const char *cols[] = {
            "red","green","blue","yellow","magenta","cyan",
            "bright_red","bright_green","bright_blue","bright_yellow"
        };
        char buf[128];
        for (int i = 0; i < 10; i++) {
            snprintf(buf, sizeof(buf), "colors,1,%s,black", cols[i]);
            send_cmd(csock, port, buf, false);
            fbNetProcess(srv);
            snprintf(buf, sizeof(buf), "print_at,1,%d,7,\"###\"", 18 + i*4);
            send_cmd(csock, port, buf, false);
            fbNetProcess(srv);
        }
    }
    CMD("win_refresh,1");
    CMD("flush");
    msleep(300);

    /* ── Progress bar ────────────────────────────────────────────── */
    CMD("colors,1,gray,black");
    CMD("attr,1,none");
    CMD("print_at,1,2,9,\"Progress:\"");
    {
        char buf[128];
        for (int pct = 0; pct <= 100; pct += 5) {
            snprintf(buf, sizeof(buf),
                     "progress,1,12,9,58,%d,#3399FF,#222244,1", pct);
            send_cmd(csock, port, buf, false);
            fbNetProcess(srv);
            send_cmd(csock, port, "win_refresh,1", false);
            fbNetProcess(srv);
            send_cmd(csock, port, "flush", false);
            fbNetProcess(srv);
            fbFlush(scr);
            msleep(25);
        }
    }

    /* ── Gauges ─────────────────────────────────────────────────── */
    CMD("print_at,1,2,11,\"Gauges:\"");
    {
        static const char *gcols[] = {
            "#FF4444","#FF8800","#FFCC00","#88FF00",
            "#00FFCC","#0099FF","#9944FF","#FF44CC"
        };
        static const int gvals[] = { 80,60,45,90,30,70,55,40 };
        char buf[128];
        for (int g = 0; g < 8; g++) {
            snprintf(buf, sizeof(buf),
                     "gauge,1,%d,12,6,%d,100,%s,#111122",
                     10 + g*8, gvals[g], gcols[g]);
            send_cmd(csock, port, buf, false);
            fbNetProcess(srv);
        }
    }
    CMD("win_refresh,1");
    CMD("flush");
    msleep(400);

    /* ── Sparkline ──────────────────────────────────────────────── */
    CMD("print_at,1,2,19,\"Sparkline:\"");
    CMD("sparkline,1,13,19,40,#00CCFF,black,"
        "0.1,0.3,0.5,0.7,0.9,0.8,0.6,0.4,0.2,0.3,"
        "0.5,0.8,1.0,0.9,0.7,0.5,0.3,0.1,0.2,0.4,"
        "0.6,0.8,0.7,0.5,0.3,0.4,0.6,0.9,1.0,0.8,"
        "0.6,0.4,0.2,0.1,0.3,0.5,0.7,0.9,0.8,0.6");
    CMD("win_refresh,1");
    CMD("flush");
    msleep(400);

    /* ── Border styles ──────────────────────────────────────────── */
    CMD("box,1,2,21,20,3,single,#446688");
    CMD("box,1,24,21,20,3,double,#886644");
    CMD("box,1,46,21,20,3,rounded,#448866");
    CMD("print_at,1,3,22,\"single\"");
    CMD("print_at,1,25,22,\"double\"");
    CMD("print_at,1,47,22,\"rounded\"");
    CMD("win_refresh,1");
    CMD("flush");
    msleep(400);

    /* ── Pixel drawing: concentric circles ─────────────────────── */
    CMD("clear,black");
    {
        char buf[128];
        int cx = fbWidth(scr) / 2;
        int cy = fbHeight(scr) / 2;
        for (int i = 0; i < 12; i++) {
            snprintf(buf, sizeof(buf),
                     "circle,%d,%d,%d,#%02X%02X%02X",
                     cx, cy, 40 + i*18,
                     (i*22)&0xFF, (180-i*14)&0xFF, (255-i*18)&0xFF);
            send_cmd(csock, port, buf, false);
            fbNetProcess(srv);
        }
    }
    CMD("flush");
    msleep(600);

    /* ── Multi-font pixel text ──────────────────────────────────── */
    CMD("clear,black");
    CMD("print_px,40,40,\"VGA 8x16\",white,black,none,vga");
    CMD("print_px,40,70,\"Bold 8x16\",bright_yellow,black,bold,bold8");
    CMD("print_px,40,100,\"Thin 5x8\",cyan,black,none,thin5");
    CMD("print_px,40,120,\"Narrow 6x12\",bright_green,black,none,narrow6");
    CMD("print_px,40,148,\"Block 8x16\",bright_red,black,none,block8");
    CMD("print_px,40,178,\"LCD  0123456789\",bright_cyan,black,none,lcd7");
    CMD("print_px,40,210,\"12x24 large font\",bright_cyan,black,none,12x24");
    CMD("print_px,40,250,\"16x32 large font\",bright_magenta,black,none,16x32");
    CMD("print_px,40,300,\"24x48 LARGE\",bright_red,black,none,24x48");
    CMD("flush");
    msleep(1200);

    /* ── Toast notifications ────────────────────────────────────── */
    CMD("toast,info,1200,\"Self-test complete!\"");
    CMD("flush");
    msleep(300);
    CMD("toast,success,1500,\"All UDP commands working correctly\"");
    CMD("flush");
    msleep(300);

    /* ── Idle display: rebuild a clean status window ────────────── */
    CMD("win_del,1");
    CMD("clear,black");
    /* win_new col,row,cols,rows — server will assign ID 1 again    */
    CMD("win_new,0,0,80,5");
    {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "title_bar,1,\"fbcurses UDP Server  port %u\","
                 "double,cyan,black,cyan", port);
        send_cmd(csock, port, buf, false);
        fbNetProcess(srv);
    }
    CMD("colors,1,white,black");
    CMD("print_at,1,2,2,\"Server ready — send commands from another terminal\"");
    {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "print_at,1,2,3,\"UDP port %u  |  Press Esc or q to quit\"",
                 port);
        send_cmd(csock, port, buf, false);
        fbNetProcess(srv);
    }
    CMD("win_refresh,1");
    CMD("flush");

    printf("[net_demo] Self-test done. Entering event loop.\n");
    close(csock);

#undef CMD
}


/* ── Signal handler ─────────────────────────────────────────────────
   fbcurses.c registers its own SIGINT/SIGTERM handler via fbInit()
   that calls fbShutdown() on the way out.  We chain onto that by
   calling exit() here, which triggers atexit() -> fbShutdown().
   fbNetRun()'s select() is interrupted by the signal (EINTR), the
   loop condition !srv->stop re-evaluates, and since fbShutdown has
   cleared the screen we just need the process to exit cleanly.
   We also call fbNetStop() via the global pointer so the loop exits
   even if the signal arrives between select() calls.               */
static fbNetServer *_g_srv = NULL;

static void _sigStop(int sig)
{
    (void)sig;
    if (_g_srv) fbNetStop(_g_srv);
    exit(0);   /* triggers atexit -> fbShutdown -> restores terminal  */
}


int main(int argc, char *argv[])
{
    uint16_t port = 9876;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-p") == 0)
			port = (uint16_t)atoi(argv[i+1]);
		else if (strcmp(argv[i], "-t") == 0)
			doSelfTest = true;
		else if (argv[i][0] == '-') {
			printf("Unknown argument. '%s'\n", argv[i]);
			printf("Usage: net_demo [-p portNum] [-t]\n");
			printf("   -p portNum, Set port number to use, else 9876 is used.\n");
			printf("   -t Runs self test before it starts listening.\n");
			return 0;
		}
	}

    if (argc > 1) port = (uint16_t)atoi(argv[1]);

    fbScreen *scr = fbInit(NULL);
    if (!scr) {
        fprintf(stderr, "fbInit: %s\n", fbGetError());
        return 1;
    }

    fbNetServer *srv = fbNetOpen(scr, port);
    { struct sigaction _sa; _sa.sa_handler = _sigStop;
      sigemptyset(&_sa.sa_mask); _sa.sa_flags = 0;
      sigaction(SIGINT, &_sa, NULL); sigaction(SIGTERM, &_sa, NULL); }
    if (!srv) {
        fprintf(stderr, "fbNetOpen: could not bind port %u\n", port);
        fbShutdown(scr);
        return 1;
    }

    _g_srv = srv;   /* give signal handler access to the server */
    fbNetSetLog(srv, FB_NET_LOG_COMMANDS, NULL);

    /* Join the default multicast group so the server also receives
       packets sent to 239.76.66.49 (broadcast to all displays).   */
    if (fbNetJoinMulticast(srv, FB_NET_MCAST_ALL)) {
        printf("[net_demo] Joined multicast group %s\n", FB_NET_MCAST_ALL);
    } else {
        printf("[net_demo] Multicast join skipped "
               "(may need to be root or have a multicast-capable interface)\n");
    }

    printf("[net_demo] fbcurses UDP server on port %u\n", fbNetPort(srv));
    printf("[net_demo] Running self-test, then entering event loop.\n");
    printf("[net_demo] Press Esc or 'q' on the framebuffer console to quit.\n");

	if (doSelfTest == 1)
		runSelfTest(fbNetPort(srv), srv, scr);

    /* Blocking event loop — handles external clients */
    fbNetRun(srv, true);

    printf("[net_demo] Shutting down. Packets: in=%llu err=%llu\n",
           (unsigned long long)fbNetPacketsIn(srv),
           (unsigned long long)fbNetPacketsErr(srv));

    _g_srv = NULL;
    fbNetClose(srv);
    fbShutdown(scr);
    return 0;
}
