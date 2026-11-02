/* /dev/hello: a minimal QNX 8.0 resource manager.
 * read() returns the current message; write() replaces it (1 to 63 bytes, all or nothing). */
#include <sys/iofunc.h> /* include before dispatch.h so the handlers get iofunc_ocb_t */
#include <sys/dispatch.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MESSAGE_SIZE 64
static char message[MESSAGE_SIZE] = "Hello from a QNX 8.0 resource manager\n";
static size_t message_len;

static int hello_read(resmgr_context_t *ctp, io_read_t *msg, iofunc_ocb_t *ocb)
{
    int status = iofunc_read_verify(ctp, msg, ocb, NULL); /* opened for reading? */
    if (status != EOK) return status;
    if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE) return ENOSYS;

    /* Serve the bytes from this open's offset on, so `cat` gets the text once and then EOF. */
    size_t offset = (size_t)ocb->offset;
    size_t n = offset < message_len ? message_len - offset : 0;
    if (n > _IO_READ_GET_NBYTES(msg)) n = _IO_READ_GET_NBYTES(msg);
    _IO_SET_READ_NBYTES(ctp, n);
    if (n == 0) return _RESMGR_NPARTS(0);
    SETIOV(ctp->iov, message + offset, n); /* the kernel copies straight into the client's buffer */
    ocb->offset += n;
    return _RESMGR_NPARTS(1);
}

static int hello_write(resmgr_context_t *ctp, io_write_t *msg, iofunc_ocb_t *ocb)
{
    int status = iofunc_write_verify(ctp, msg, ocb, NULL); /* opened for writing? */
    if (status != EOK) return status;
    if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE) return ENOSYS;

    size_t n = _IO_WRITE_GET_NBYTES(msg);
    if (n == 0 || n >= MESSAGE_SIZE) return EINVAL;
    /* The data follows the io_write_t header; refuse a client that sent less than it claims. */
    if ((size_t)ctp->info.srcmsglen < (size_t)ctp->offset + sizeof(msg->i) + n) return EBADMSG;
    char incoming[MESSAGE_SIZE];
    if (resmgr_msgget(ctp, incoming, n, sizeof(msg->i)) != (int)n) return EIO;
    memcpy(message, incoming, n); /* only after the whole message arrived */
    message[n] = '\0';
    message_len = n;

    _IO_SET_WRITE_NBYTES(ctp, n);
    ocb->attr->flags |= IOFUNC_ATTR_MTIME | IOFUNC_ATTR_CTIME;
    return _RESMGR_NPARTS(0);
}

int main(void)
{
    message_len = strlen(message);

    dispatch_t *dpp = dispatch_create(); /* channel that receives the clients' messages */
    if (!dpp) { perror("dispatch_create"); return EXIT_FAILURE; }

    /* Start from the default POSIX handlers (open, close, stat, ...) and override read and write. */
    resmgr_connect_funcs_t connect_funcs;
    resmgr_io_funcs_t io_funcs;
    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs, _RESMGR_IO_NFUNCS, &io_funcs);
    io_funcs.read = hello_read;
    io_funcs.read64 = hello_read;
    io_funcs.write = hello_write;
    io_funcs.write64 = hello_write;

    iofunc_attr_t attr; /* what `ls -l /dev/hello` shows: a character device, rw for everyone */
    iofunc_attr_init(&attr, S_IFCHR | 0666, NULL, NULL);

    resmgr_attr_t resmgr_attr = { .nparts_max = 1, .msg_max_size = 2048 };
    if (resmgr_attach(dpp, &resmgr_attr, "/dev/hello", _FTYPE_ANY, 0, &connect_funcs, &io_funcs, &attr) == -1) {
        perror("resmgr_attach");
        return EXIT_FAILURE;
    }

    dispatch_context_t *ctp = dispatch_context_alloc(dpp);
    if (!ctp) { perror("dispatch_context_alloc"); return EXIT_FAILURE; }
    for (;;) { /* receive a message, run its handler, reply, repeat */
        ctp = dispatch_block(ctp);
        if (!ctp) { perror("dispatch_block"); return EXIT_FAILURE; }
        dispatch_handler(ctp);
    }
}
