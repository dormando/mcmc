#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include "mcmc.h"

// NOTE: this _will_ change a bit for adding TLS support.

// A "reasonable" minimum buffer size to work with.
// Callers are allowed to create a buffer of any size larger than this.
// TODO: Put the math/documentation in here.
// This is essentially the largest return value status line possible.
// at least doubled for wiggle room.
#define MIN_BUFFER_SIZE 2048

#define FLAG_BUF_IS_ERROR 0x1
#define FLAG_BUF_IS_NUMERIC 0x2

#define STATE_DEFAULT 0 // looking for any kind of response
#define STATE_GET_RESP 1 // processing VALUE's until END
#define STATE_STAT_RESP 2 // processing STAT's until END
#define STATE_STAT_RESP_DONE 3

// TODO: add state machine state. fixes VALUE issue, some other stuff.
typedef struct mcmc_ctx {
    int fd;
    int gai_status; // getaddrinfo() last status.
    int last_sys_error; // last syscall error (connect/etc?)
    int sent_bytes_partial; // note for partially sent buffers.
    int request_queue; // supposed outstanding replies.
    int fail_code; // recent failure reason.
    int error; // latest error code.
    int response_type; // type of response value for user to handle.
    uint32_t status_flags; // internal only flags.
    int state;

    size_t buffer_used; // amount of bytes read into the buffer so far.
    char *buffer_head; // buffer pointer currently in use.
    char *buffer_request_end; // cached endpoint for current request

    // request response detail.
    mcmc_resp_t *resp;
    int value_offset; // how far into buffer_head the value appears.
    int value_buf_remain; // how much potential value remains inside main buf.
} mcmc_ctx_t;

// INTERNAL FUNCTIONS

static int _mcmc_parse_value_line(mcmc_ctx_t *ctx) {
    char *buf = ctx->buffer_head;
    // we know that "VALUE " has matched, so skip that.
    char *p = buf+6;
    char *en = ctx->buffer_request_end;

    // <key> <flags> <bytes> [<cas unique>]
    char *key = p;
    int keylen;
    p = memchr(p, ' ', en - p);
    if (p == NULL) {
        return MCMC_PARSE_ERROR;
    }

    keylen = p - key;

    // convert flags into something useful.
    char *n = NULL;
    errno = 0;
    uint32_t flags = strtoul(p, &n, 10);
    if ((errno == ERANGE) || (p == n) || (*n != ' ')) {
        return MCMC_PARSE_ERROR;
    }
    p = n;

    errno = 0;
    uint32_t bytes = strtoul(p, &n, 10);
    if ((errno == ERANGE) || (p == n)) {
        return MCMC_PARSE_ERROR;
    }
    p = n;

    // If next byte is a space, we read the optional CAS value.
    uint64_t cas = 0;
    if (*n == ' ') {
        errno = 0;
        cas = strtoull(p, &n, 10);
        if ((errno == ERANGE) || (p == n)) {
            return MCMC_PARSE_ERROR;
        }
    }

    // If we made it this far, we've parsed everything, stuff the details into
    // the context for fetching later.
    mcmc_resp_t *r = ctx->resp;
    r->value = buf + ctx->value_offset;
    r->vlen = bytes + 2; // add in the \r\n
    if (ctx->value_buf_remain >= r->vlen) {
        r->vlen_read = r->vlen;
    } else {
        r->vlen_read = ctx->value_buf_remain;
    }
    r->key = key;
    r->klen = keylen;
    r->flags = flags;
    r->cas = cas;
    r->type = MCMC_RESP_GET;
    ctx->state = STATE_GET_RESP;

    // NOTE: if value_offset < buffer_used, has part of the value in the
    // buffer already.

    return MCMC_OK;
}

// FIXME: This is broken for ASCII multiget.
// if we get VALUE back, we need to stay in ASCII GET read mode until an END
// is seen.
static int _mcmc_parse_response(mcmc_ctx_t *ctx) {
    char *buf = ctx->buffer_head;
    char *cur = buf;
    char *en = ctx->buffer_request_end;
    int rlen; // response code length.
    int more = 0;

    while (cur != en) {
        if (*cur == ' ') {
            more = 1;
            break;
        }
        cur++;
    }
    rlen = cur - buf;

    // incr/decr returns a number with no code :(
    // not checking length first since buf must have at least one char to
    // enter this function.
    if (buf[0] >= '0' && buf[0] <= '9') {
        // TODO: parse it as a number on request.
        // TODO: validate whole thing as digits here?
        ctx->status_flags |= FLAG_BUF_IS_NUMERIC;
        return MCMC_OK;
    }

    if (rlen < 2) {
        ctx->error = MCMC_PARSE_ERROR_SHORT;
        return MCMC_ERR;
    }

    int rv = -1;
    int fail = -1;
    mcmc_resp_t *r = ctx->resp;
    switch (rlen) {
        case 2:
            // meta, "OK"
            // FIXME: adding new return codes would make the client completely
            // fail. The rest of the client is agnostic to requests/flags for
            // meta.
            // can we make it agnostic for return codes outside of "read this
            // data" types?
            // As-is it should fail down to the "send the return code to the
            // user". not sure that's right.
            switch (buf[0]) {
            case 'E':
                if (buf[1] == 'N') {
                    rv = MCMC_MISS;
                } else if (buf[1] == 'X') {
                    fail = MCMC_FAIL_EXISTS;
                }
                break;
            case 'M':
                if (buf[1] == 'N') {
                    // specific return code so user can see pipeline end.
                    rv = MCMC_NOP;
                } else if (buf[1] == 'E') {
                    // ME is the debug output line.
                    rv = MCMC_OK;
                }
                break;
            case 'N':
                if (buf[1] == 'F') {
                    fail = MCMC_FAIL_NOT_FOUND;
                } else if (buf[1] == 'S') {
                    fail = MCMC_FAIL_NOT_STORED;
                }
                break;
            case 'O':
                if (buf[1] == 'K') {
                    // FIXME: think I really screwed myself changing
                    // everything to OK instead of HD.
                    // bare OK could mean RESP_META or RESP_GENERIC :/
                    rv = MCMC_OK;
                }
                break;
            case 'V':
                if (buf[1] == 'A') {
                    // VA <size> <flags>*\r\n
                    if (more) {
                        errno = 0;
                        char *n = NULL;
                        uint32_t vsize = strtoul(cur, &n, 10);
                        if ((errno == ERANGE) || (cur == n)) {
                            rv = MCMC_ERR;
                        } else {
                            r->value = ctx->buffer_head+ctx->value_offset;
                            r->vlen = vsize + 2; // tag in the \r\n.
                            if (ctx->value_buf_remain >= r->vlen) {
                                r->vlen_read = r->vlen;
                            } else {
                                r->vlen_read = ctx->value_buf_remain;
                            }
                            cur = n;
                            if (*cur != ' ') {
                                more = 0;
                            }
                            rv = MCMC_OK;
                        }
                    } else {
                        rv = MCMC_ERR;
                    }
                }
                break;
            }
            // maybe: if !rv and !fail, do something special?
            // if (more), they're flags. shove them in the right place.
            if (more) {
                r->rline = cur+1; // eat the space.
                r->rlen = en - cur;
            } else {
                r->rline = NULL;
                r->rlen = 0;
            }
            break;
        case 3:
            if (memcmp(buf, "END", 3) == 0) {
                if (ctx->state == STATE_STAT_RESP) {
                    // seen some STAT responses, now this completes it.
                    ctx->state = STATE_STAT_RESP_DONE;
                } else if (ctx->state == STATE_GET_RESP) {
                    // seen at least one VALUE line, so this can't "miss"
                    // TODO: need to quietly push the buffer?
                    ctx->state = STATE_DEFAULT;
                } else {
                    // END alone means one or more ASCII GET's but no VALUE's.
                    rv = MCMC_MISS;
                }
            }
            break;
        case 4:
            if (memcmp(buf, "STAT", 4) == 0) {
                rv = MCMC_OK;
                ctx->response_type = MCMC_RESP_STAT;
                ctx->state = STATE_STAT_RESP;
                // TODO: initialize stat reader mode.
            }
            break;
        case 5:
            if (memcmp(buf, "VALUE", 5) == 0) {
                if (more) {
                    // <key> <flags> <bytes> [<cas unique>]
                    rv = _mcmc_parse_value_line(ctx);
                } else {
                    rv = MCMC_ERR; // FIXME: parse error.
                }
            }
            break;
        case 6:
            if (memcmp(buf, "STORED", 6) == 0) {
                rv = MCMC_STORED;
            } else if (memcmp(buf, "EXISTS", 6) == 0) {
                fail = MCMC_FAIL_EXISTS;
            }
            break;
        case 7:
            if (memcmp(buf, "DELETED", 7) == 0) {
                rv = MCMC_DELETED;
            } else if (memcmp(buf, "TOUCHED", 7) == 0) {
                rv = MCMC_TOUCHED;
            } else if (memcmp(buf, "VERSION", 7) == 0) {
                rv = MCMC_VERSION;
                // TODO: prep the version line for return
            }
            break;
        case 9:
            if (memcmp(buf, "NOT_FOUND", 9) == 0) {
                fail = MCMC_FAIL_NOT_FOUND;
            }
            break;
        case 10:
            if (memcmp(buf, "NOT_STORED", 10) == 0) {
                fail = MCMC_FAIL_NOT_STORED;
            }
            break;
        default:
            // Unknown code, assume error.
            break;
    }

    if (fail != -1) {
        ctx->fail_code = fail;
        rv = MCMC_FAIL;
    } else if (rv == -1) {
        ctx->status_flags |= FLAG_BUF_IS_ERROR;
        rv = MCMC_ERR;
    }

    return rv;
}

// EXTERNAL API

size_t mcmc_size(int options) {
    return sizeof(mcmc_ctx_t);
}

// Allow returning this dynamically based on options set.
// FIXME: it might be more flexible to call this after mcmc_connect()...
// but this is probably more convenient for the caller if it's less dynamic.
size_t mcmc_min_buffer_size(int options) {
    return MIN_BUFFER_SIZE;
}

// TODO:
// - option for connecting 4 -> 6 or 6 -> 4
// connect_unix()
// connect_bind_tcp()
// ^ fill an internal struct from the stack and call into this central
// connect?
int mcmc_connect(void *c, char *host, char *port, int options) {
    mcmc_ctx_t *ctx = (mcmc_ctx_t *)c;

    int s;
    int sock;
    int res = MCMC_OK;
    struct addrinfo hints;
    struct addrinfo *ai;
    struct addrinfo *next;

    // Since our cx memory was likely malloc'ed, ensure we start clear.
    memset(ctx, 0, sizeof(mcmc_ctx_t));
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    s = getaddrinfo(host, port, &hints, &ai);

    if (s != 0) {
        hints.ai_family = AF_INET6;
        s = getaddrinfo(host, port, &hints, &ai);
        if (s != 0) {
            // TODO: gai_strerror(s)
            ctx->gai_status = s;
            res = MCMC_ERR;
            goto end;
        }
    }

    for (next = ai; next != NULL; next = next->ai_next) {
        sock = socket(next->ai_family, next->ai_socktype,
                next->ai_protocol);
        if (sock == -1)
            continue;

        // TODO: NONBLOCK
        // TODO: BIND local port.
        if (connect(sock, next->ai_addr, next->ai_addrlen) != -1)
            break;

        close(sock);
    }

    // TODO: cache last connect status code?
    if (next == NULL) {
        res = MCMC_ERR;
        goto end;
    }

    ctx->fd = sock;
    res = MCMC_CONNECTED;
end:
    if (ai) {
        freeaddrinfo(ai);
    }
    return res;
}

// NOTE: if WANT_WRITE returned, call with same arguments.
// FIXME: len -> size_t?
int mcmc_send_request(void *c, char *request, int len, int count) {
    mcmc_ctx_t *ctx = (mcmc_ctx_t *)c;

    // adjust our send buffer by how much has already been sent.
    char *r = request + ctx->sent_bytes_partial;
    int l = len - ctx->sent_bytes_partial;
    int sent = send(ctx->fd, r, l, 0);
    if (sent == -1) {
        return MCMC_ERR;
    }

    if (sent < len) {
        ctx->sent_bytes_partial += sent;
        return MCMC_WANT_WRITE;
    } else {
        ctx->request_queue += count;
        ctx->sent_bytes_partial = 0;
    }

    return MCMC_OK;
}

int mcmc_read(void *c, char *buf, size_t bufsize, mcmc_resp_t *r) {
    mcmc_ctx_t *ctx = (mcmc_ctx_t *)c;

    // adjust buffer by how far we've already consumed.
    char *b = buf + ctx->buffer_used;
    size_t l = bufsize - ctx->buffer_used;

    int read = recv(ctx->fd, b, l, 0);
    if (read == 0) {
        return MCMC_NOT_CONNECTED;
    }
    if (read == -1) {
        return MCMC_ERR;
    }

    ctx->buffer_used += read;

    // Always scan from the start of the original buffer.
    char *el = memchr(buf, '\n', ctx->buffer_used);
    if (!el) {
        return MCMC_WANT_READ;
    }

    // Noting where a potential value would start inside the buffer.
    if (el - b < ctx->buffer_used) {
        ctx->value_offset = el - b + 1;
        ctx->value_buf_remain = ctx->buffer_used - ctx->value_offset;
    } else {
        ctx->value_offset = 0;
        ctx->value_buf_remain = 0;
    }

    // FIXME: the server must be stricter in what it sends back. should always
    // have a \r. check for it and fail?
    if (el != buf && *(el-1) == '\r') {
        ctx->buffer_request_end = el-1; // back up to the \r character.
    } else {
        ctx->buffer_request_end = el;
    }
    ctx->buffer_head = buf;
    // TODO: handling for nonblock case.

    // We have a result line. Now pass it through the parser.
    // Then we indicate to the user that a response is ready.
    ctx->resp = r;
    return _mcmc_parse_response(ctx);
}

void mcmc_get_error(void *c, char *code, size_t clen, char *msg, size_t mlen) {
    code[0] = '\0';
    msg[0] = '\0';
}

// read into the buffer, up to a max size of vsize.
// will read (vsize-read) into the buffer pointed to by (val+read).
// you are able to stream the value into different buffers, or process the
// value and reuse the same buffer, by adjusting vsize and *read between
// calls.
// vsize must not be larger than the remaining value size pending read.
int mcmc_read_value(void *c, char *val, const size_t vsize, int *read) {
    mcmc_ctx_t *ctx = (mcmc_ctx_t *)c;
    size_t l;

    if (ctx->value_buf_remain) {
        int tocopy = ctx->value_buf_remain > vsize ? vsize : ctx->value_buf_remain;
        memcpy(val + *read, ctx->buffer_head+ctx->value_offset, tocopy);
        ctx->value_buf_remain -= tocopy;
        *read += tocopy;
        if (ctx->value_buf_remain) {
            // FIXME: think we need a specific code for "value didn't fit"
            return MCMC_WANT_READ;
        }
    }

    char *v = val + *read;
    l = vsize - *read;

    int r = recv(ctx->fd, v, l, 0);
    if (r == 0) {
        // TODO: some internal disconnect work?
        return MCMC_NOT_CONNECTED;
    }
    if (r == -1) {
        return MCMC_ERR;
    }

    *read += r;

    if (*read < vsize) {
        return MCMC_WANT_READ;
    } else {
        return MCMC_OK;
    }
}

char *mcmc_buffer_consume(void *c, int *remain) {
    mcmc_ctx_t *ctx = (mcmc_ctx_t *)c;
    int used = ctx->buffer_used;
    // first, advance buffer to end of the current request.
    char *buf = ctx->buffer_request_end;
    used -= buf - ctx->buffer_head;

    // TODO: what parts of ctx do we need to advance/clear out?
    // buffer_head, _used, etc?
    ctx->request_queue--;
    ctx->response_type = 0;

    // If there was a value, advance over everything.
    // FIXME: need better internal indicator for how far to jump forward?
    /*if (ctx->status_flags & FLAG_BUF_HAS_VALUE) {
        if (used >= ctx->vsize) {
            used -= ctx->vsize;
            buf += ctx->vsize;
        } else {
            buf += used;
            used = 0;
        }
    }*/
    // FIXME: need to stay in value read mode.
    ctx->status_flags = 0;

    if (used) {
        *remain = used;
        return buf;
    } else {
        return NULL;
    }
}

int mcmc_disconnect(void *c) {
    mcmc_ctx_t *ctx = (mcmc_ctx_t *)c;

    // FIXME: I forget if 0 can be valid.
    if (ctx->fd != 0) {
        close(ctx->fd);
        return MCMC_OK;
    } else {
        return MCMC_NOT_CONNECTED;
    }
}
