/**
 * VDJ network handler that contains a single thread to pselect() on all
 * sockets a CDJ listens to and handles interupts with signals.
 *
 * The FDs timeout at 120 seconds for no real reason.
 * An interval is set to fire SIGARARM every 200ms for CDJ status and keepalive fires every 8th alarm (1600ms)
 * The signal is converted to a write() on a self pipe which exits pselect()
 *
 * @author teknopaul
 */
#include <sys/select.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>

#include "vdj.h"
#include "vdj_discovery.h"


#define VDJ_PSELECT_TIMEOUT

static unsigned _Atomic vdj_pselect_running = ATOMIC_VAR_INIT(0);

typedef struct {
    vdj_discovery_ph            discovery_ph;
    vdj_discovery_unicast_ph    discovery_unicast_ph;
    vdj_update_ph               update_ph;
    vdj_beat_ph                 beat_ph;
    vdj_beat_unicast_ph                 beat_unicast_ph;
} vdj_handlers;

/**
 * alarm counter, we send 1 keepalive for every 8 status messages
 */
int keepalive_ticker = 0;

/**
 * pipe that is internal to this code, sends one byte at a time
 */
static int self_pipe[2];

// pselect() thread ought to be responsible for closing the file descriptor it monitors
static void
vdj_pselect_close_fds(vdj_t* v)
{
    vdj_close_sockets(v);
}

/**
 * return max file descriptor, required by pselect
 */
static int
vdj_pselect_max_fd(vdj_t* v)
{
    int fd_max = v->discovery_socket_fd;
    fd_max = fd_max > v->discovery_unicast_socket_fd ? fd_max : v->discovery_unicast_socket_fd;
    fd_max = fd_max > v->beat_socket_fd ? fd_max : v->beat_socket_fd;
    fd_max = fd_max > v->beat_unicast_socket_fd ? fd_max : v->beat_unicast_socket_fd;
    fd_max = fd_max > v->update_socket_fd ? fd_max : v->update_socket_fd;
    fd_max = fd_max > self_pipe[0] ? fd_max : self_pipe[0]; 
    return fd_max + 1;
}

// signal handling, horribly verbose

/**
 * signal handler that just writes 1 byte to self_pipe to exit pselect()
 * we have to write one byte so write the signal type, and we can extend this to other signals.
 */
static void
vdj_pselect_sig_handler(int sig)
{
    int errno_back = errno;

    uint8_t sig_c = sig;
    write(self_pipe[1], &sig_c, 1); // ignore errors, nothing doing

    errno = errno_back;
}

/**
 * setup the signal handling, blocks SIGALRM.
 * creates a timer that sends SIGALRM every 200ms
 */
static int
vdj_pselect_init_alarm()
{
    sigset_t blockset;
    struct itimerval interval;

    // block SIGALRM
    sigemptyset(&blockset);
    sigaddset(&blockset, SIGALRM);
    sigprocmask(SIG_BLOCK, &blockset, NULL);

    // setup up a signal handler for SIGALRM that will not fire
    struct sigaction act;
    memset (&act, 0, sizeof(act));
    act.sa_flags = SA_RESTART;
    act.sa_handler = vdj_pselect_sig_handler;
    sigemptyset(&act.sa_mask);
    if (sigaction(SIGALRM, &act, NULL)) {
        fprintf(stderr, "error: sigaction SIGALRM '%s'\n", strerror(errno));
        return CDJ_ERROR;
    }

    // setup an alarm to fire the signal, currently blocked
    interval.it_interval.tv_sec = 0;
    interval.it_interval.tv_usec = 200000;
    interval.it_value.tv_sec = 0;
    interval.it_value.tv_usec = 200000;
    // aka ualarm(0, 200000);
    if (setitimer(ITIMER_REAL, &interval, NULL) == -1) {
        fprintf(stderr, "error: setitimer '%s'\n", strerror(errno));
        return CDJ_ERROR;
    }

    return CDJ_OK;
}

/**
 * create a non-blocking pipe to ourselves, sighandler writes to it so we can immediatly exit pselect() 
 * on any signal.
 */
static int
vdj_pselect_self_pipe()
{
    int flags;

    if (pipe(self_pipe) == -1) {
        return CDJ_ERROR;
    }
    flags = fcntl(self_pipe[0], F_GETFL);
    if (flags == -1) {
        return CDJ_ERROR;
    }
    flags |= O_NONBLOCK;
    if (fcntl(self_pipe[0], F_SETFL, flags) == -1) {
        return CDJ_ERROR;
    }
    flags = fcntl(self_pipe[1], F_GETFL);
    if (flags == -1) {
        return CDJ_ERROR;
    }
    flags |= O_NONBLOCK;
    if (fcntl(self_pipe[1], F_SETFL, flags) == -1) {
        return CDJ_ERROR;
    }
    return  CDJ_OK; 
}

/**
 * business logic that results from signals and timeouts
 */
static void
vdj_handle_managed_timeout(vdj_t* v, vdj_handlers* handlers, uint8_t sig)
{
    if ( sig == SIGALRM && vdj_pselect_running ) {
        vdj_send_status(v);
        if (++keepalive_ticker % 8 == 0) {
            vdj_send_keepalive(v);
            vdj_expire_players(v);
        }
        vdj_expire_play_state(v);
    }
    if ( sig == SIGQUIT ) {
        vdj_pselect_running = 0;
    }
    // TODO might be good to handle master in signals
    if ( sig == SIGUSR1 ) {
        //noop
    }
    if ( sig == SIGUSR2 ) {
        //noop
    }
}

/**
 * handler for all sockets, inc self_pipe.  This code reads the network packets
 * and defers business login in vdj_handle_managed_*_datagram() methods
 */
static void
vdj_pselect_handler_socket(vdj_t* v, vdj_handlers* handlers, fd_set *readfds)
{
    uint8_t sig;
    ssize_t len;
    uint8_t packet[1500];

    // beats
    if ( FD_ISSET(v->beat_socket_fd, readfds) ) {
        len = recv(v->beat_socket_fd, packet, 1500, 0);
        if (len == -1) {
            fprintf(stderr, "error: beat_socket_fd read '%s'\n", strerror(errno));
        } else {
            if ( ! cdj_validate_header(packet, len) ) {
                vdj_handle_managed_beat_datagram(v, handlers->beat_ph, packet, len);
            }
        }
    }

    if ( FD_ISSET(v->discovery_socket_fd, readfds) ) {
        len = recv(v->discovery_socket_fd, packet, 1500, 0);
        if (len == -1) {
            fprintf(stderr, "error: discovery_socket_fd read '%s'\n", strerror(errno));
        } else {
            if ( ! cdj_validate_header(packet, len) ) {
                vdj_handle_managed_discovery_datagram(v, handlers->discovery_ph, packet, len);
            }
        }
    }

    if ( FD_ISSET(v->discovery_unicast_socket_fd, readfds) ) {
        len = recv(v->discovery_unicast_socket_fd, packet, 1500, 0);
        if (len == -1) {
            fprintf(stderr, "error: discovery_unicast_socket_fd read '%s'\n", strerror(errno));
        } else {
            if ( ! cdj_validate_header(packet, len) ) {
                vdj_handle_managed_discovery_unicast_datagram(v, handlers->discovery_unicast_ph, packet, len);
            }
        }

    }

    // beat unicast (master handoff)
    if ( FD_ISSET(v->beat_unicast_socket_fd, readfds) ) {
        len = recv(v->beat_unicast_socket_fd, packet, 1500, 0);
        if (len == -1) {
            fprintf(stderr, "error: beat_socket_fd read '%s'\n", strerror(errno));
        } else {
            if ( ! cdj_validate_header(packet, len) ) {
                vdj_handle_managed_beat_unicast_datagram(v, handlers->beat_unicast_ph, packet, len);
            }
        }
    }

    // status
    if ( FD_ISSET(v->update_socket_fd, readfds) ) {
        len = recv(v->update_socket_fd, packet, 1500, 0);
        if (len == -1) {
            fprintf(stderr, "error: update_socket_fd read '%s'\n", strerror(errno));
        } else {
            if ( ! cdj_validate_header(packet, len) ) {
                vdj_handle_managed_update_datagram(v, handlers->update_ph, packet, len);
            }
        }
    }

    // signal
    if ( FD_ISSET(self_pipe[0], readfds) ) {
        if (-1 == read(self_pipe[0], &sig, 1) ) {
            fprintf(stderr, "error: self pipe read '%s'\n", strerror(errno));
        } else {
            vdj_handle_managed_timeout(v, handlers, sig);
        }
    }
}

/**
 * reset the fds list (its changed on every loop by select() )
 */
static void
vdj_pselect_reset_fds(vdj_t* v, fd_set *readfds)
{
    FD_ZERO(readfds);
    FD_SET(v->discovery_socket_fd,         readfds);
    FD_SET(v->discovery_unicast_socket_fd, readfds);
    FD_SET(v->beat_socket_fd,              readfds);
    FD_SET(v->beat_unicast_socket_fd,      readfds);
    FD_SET(v->update_socket_fd,            readfds);
    FD_SET(self_pipe[0],                   readfds);
}

/**
 * The main loop of the thread, this waits on various file descriptors if any of them can be read
 * vdj_pselect_handler_socket() is called to read the packets.
 */
static void*
vdj_pselect_loop(void* arg)
{
    vdj_thread_info* tinfo = arg;
    vdj_t* v = tinfo->v;
    vdj_handlers* handlers = tinfo->handler;

    int rv;
    int nfds;
    fd_set readfds;
    sigset_t emptyset;
    struct timespec timeout = {0};

    if (vdj_pselect_self_pipe() == CDJ_ERROR ) {
        fprintf(stderr, "error: creating self_pipe\n");
    }
    nfds = vdj_pselect_max_fd(v);

    vdj_pselect_init_alarm();

    sigemptyset(&emptyset);

    while (vdj_pselect_running) {

        vdj_pselect_reset_fds(v, &readfds);
        timeout.tv_sec = 120L;
        timeout.tv_nsec = 0L;

        rv = pselect(nfds, &readfds, NULL, NULL, &timeout, &emptyset);
        if (rv == -1) {
            if (errno == EINTR) {
                // loop, interrupted by signal
                continue;
            } else {
                fprintf(stderr, "error: pselect '%s'\n", strerror(errno));
            }
        }
        else if (rv == 0) {
            // timeout, just loop
            continue;
        }
        else if (rv > 0 ) {
            vdj_pselect_handler_socket(v, handlers, &readfds);
        }
    }

    vdj_pselect_close_fds(v);

    return NULL;
}

/**
 * Initialize a single thread to handle all incomming messages, all the supplied handlers
 * will run on the same thread, so they ought to be relativly fast.
 */
int
vdj_pselect_init(vdj_t* v, 
    vdj_discovery_ph discovery_ph,
    vdj_discovery_unicast_ph discovery_unicast_ph,
    vdj_beat_ph beat_ph,
    vdj_beat_unicast_ph beat_unicast_ph,
    vdj_update_ph update_ph
    )
{

    vdj_handlers* handlers = (vdj_handlers*) calloc(1, sizeof(vdj_handlers));
    handlers->discovery_ph = discovery_ph;
    handlers->discovery_unicast_ph = discovery_unicast_ph;
    handlers->beat_ph = beat_ph;
    handlers->beat_unicast_ph = beat_unicast_ph;
    handlers->update_ph = update_ph;

    vdj_pselect_running = 1;

    pthread_t thread_id;
    vdj_thread_info* tinfo = (vdj_thread_info*) calloc(1, sizeof(vdj_thread_info));
    tinfo->v = v;
    tinfo->handler = handlers;

    return pthread_create(&thread_id, NULL, &vdj_pselect_loop, tinfo);
}

void
vdj_pselect_stop(vdj_t* v)
{
    int sig = SIGQUIT;
    write(self_pipe[1], &sig, 1);
}
