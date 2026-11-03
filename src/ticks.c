/* /dev/ticks: a resource manager that also does periodic work.
 * A monotonic timer delivers a pulse to the dispatch loop; read() returns how many have arrived. */
#include <sys/iofunc.h> /* include before dispatch.h so the handlers get iofunc_ocb_t */
#include <sys/dispatch.h>
#include <sys/neutrino.h>
#include <sys/siginfo.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define PRIORITY 20
#define PERIOD_NS 100000000L /* 100 ms */

static unsigned long ticks;

static int on_pulse(message_context_t *ctp, int code, unsigned flags, void *handle)
{
    (void)ctp; (void)code; (void)flags; (void)handle;
    ++ticks; /* same thread as the read handler, so no lock */
    return 0;
}

static int ticks_read(resmgr_context_t *ctp, io_read_t *msg, iofunc_ocb_t *ocb)
{
    int status = iofunc_read_verify(ctp, msg, ocb, NULL);
    if (status != EOK) return status;
    if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE) return ENOSYS;

    /* shortcut: formats on every read, so a client reading in small pieces could see two values;
     * keep a per-open snapshot in the OCB (step 8) when that matters. */
    static char text[32];
    size_t len = (size_t)snprintf(text, sizeof(text), "ticks=%lu\n", ticks);
    size_t offset = (size_t)ocb->offset;
    size_t n = offset < len ? len - offset : 0;
    if (n > _IO_READ_GET_NBYTES(msg)) n = _IO_READ_GET_NBYTES(msg);
    _IO_SET_READ_NBYTES(ctp, n);
    if (n == 0) return _RESMGR_NPARTS(0);
    SETIOV(ctp->iov, text + offset, n);
    ocb->offset += n;
    return _RESMGR_NPARTS(1);
}

int main(void)
{
    /* Fixed-priority scheduling: this thread must not fall behind lower-priority work. */
    struct sched_param param = { .sched_priority = PRIORITY };
    int error = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    if (error) { errno = error; perror("pthread_setschedparam"); return EXIT_FAILURE; }

    /* dispatch_create() would make the thread take the priority of each message it receives.
     * This channel keeps it at PRIORITY whoever calls; the other two flags are what dispatch_create() sets. */
    int chid = ChannelCreate(_NTO_CHF_FIXED_PRIORITY | _NTO_CHF_UNBLOCK | _NTO_CHF_DISCONNECT);
    if (chid == -1) { perror("ChannelCreate"); return EXIT_FAILURE; }
    dispatch_t *dpp = dispatch_create_channel(chid, 0);
    if (!dpp) { perror("dispatch_create_channel"); return EXIT_FAILURE; }

    resmgr_connect_funcs_t connect_funcs;
    resmgr_io_funcs_t io_funcs;
    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs, _RESMGR_IO_NFUNCS, &io_funcs);
    io_funcs.read = ticks_read;
    io_funcs.read64 = ticks_read;

    iofunc_attr_t attr;
    iofunc_attr_init(&attr, S_IFCHR | 0444, NULL, NULL);
    resmgr_attr_t resmgr_attr = { .nparts_max = 1, .msg_max_size = 2048 };
    if (resmgr_attach(dpp, &resmgr_attr, "/dev/ticks", _FTYPE_ANY, 0, &connect_funcs, &io_funcs, &attr) == -1) {
        perror("resmgr_attach");
        return EXIT_FAILURE;
    }

    /* Route one pulse code to on_pulse, and open a connection the timer can send through. */
    int code = pulse_attach(dpp, MSG_FLAG_ALLOC_PULSE, 0, on_pulse, NULL);
    if (code == -1) { perror("pulse_attach"); return EXIT_FAILURE; }
    int coid = message_connect(dpp, MSG_FLAG_SIDE_CHANNEL);
    if (coid == -1) { perror("message_connect"); return EXIT_FAILURE; }

    /* A pulse carries a priority too: it decides where the pulse waits in the channel's queue. */
    struct sigevent event;
    SIGEV_PULSE_INIT(&event, coid, PRIORITY, code, 0);
    timer_t timer;
    if (timer_create(CLOCK_MONOTONIC, &event, &timer) == -1) { perror("timer_create"); return EXIT_FAILURE; }
    struct itimerspec interval = { .it_value = {0, PERIOD_NS}, .it_interval = {0, PERIOD_NS} };
    if (timer_settime(timer, 0, &interval, NULL) == -1) { perror("timer_settime"); return EXIT_FAILURE; }

    dispatch_context_t *ctp = dispatch_context_alloc(dpp);
    if (!ctp) { perror("dispatch_context_alloc"); return EXIT_FAILURE; }
    for (;;) { /* a client message or a pulse: dispatch_handler runs whichever arrived */
        ctp = dispatch_block(ctp);
        if (!ctp) { perror("dispatch_block"); return EXIT_FAILURE; }
        dispatch_handler(ctp);
    }
}
