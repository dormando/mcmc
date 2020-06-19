#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "mcmc.h"

// TODO: try writing a "simple_get()", set(), and meta() commands.

int main (int argc, char *agv[]) {

    void *c = malloc(mcmc_size(MCMC_OPTION_BLANK));
    // we only "need" the minimum buf size.
    // buffers large enough to fit return values result in fewer syscalls.
    size_t bufsize = mcmc_min_buffer_size(MCMC_OPTION_BLANK) * 2;
    // buffers are also generally agnostic to clients. The buffer must be
    // held and re-used when required by the API. When the buffer is empty,
    // it may be released to a pool or reused with other connections.
    char *rbuf = malloc(bufsize);

    int status;

    // API is blocking by default.
    status = mcmc_connect(c, "127.0.0.1", "11211", MCMC_OPTION_BLANK);

    if (status != MCMC_CONNECTED) {
        // TODO: mc_strerr(c);
        fprintf(stderr, "Failed to connect to memcached\n");
        return -1;
    }

    // provide a buffer, the buffer length, and the number of responses
    // expected. ie; if pipelining many requests, or using noreply semantics.
    // FIXME: not confident "number of expected responses" is worth tracking
    // internally.
    status = mcmc_send_request(c, "get foo\r\n", 9, 1);

    if (status != MCMC_OK) {
        fprintf(stderr, "Failed to send request to memcached\n");
        return -1;
    }

    // buffer shouldn't change until the read is completed.
    status = mcmc_read(c, rbuf, bufsize);
    if (status == MCMC_OK) {
        char *val;
        // should be a few options:
        // 1) always grab the value into memory here.
        // 2) stream the value by reading it in chunks.
        // (reset read to do this)
        // 3) call mcmc_is_value_ready(c) and grab it without copying.
        size_t vsize = 0;
        int ready = 0; // whole value has been read already.
        if (mcmc_has_value(c, &vsize, &ready) == MCMC_OK) {
            if (ready) {
                val = mcmc_value(c);
            } else {
                // NOTE: with this approach we can't avoid memcpy'ing part of
                // the value.
                val = malloc(vsize);
                int read = 0;
                do {
                    status = mcmc_read_value(c, val, vsize, &read);
                } while (status == MCMC_WANT_READ);
            }
        }

        switch (mcmc_resp_type(c)) {
            case MCMC_RESP_GET:
                // use mcmc_resp_get(c, etc);
                break;
            case MCMC_RESP_META:
                // use mcmc_resp_meta(c, rline, &rlen);
                break;
            case MCMC_RESP_STAT:
                // use mcmc_resp_stat(c, rline, &rlen) while res ==
                // MCMC_CONTINUE
                break;
            default:
                fprintf(stderr, "Unknown response type: %d\n", mcmc_resp_type(c));
                break;
        }

        // we now have a valid value and result line.
        // copy them if necessary, as they point into the buffer supplied.
        
        // options here:
        // 1) minimal copying: we have rline, val (from whatever source) in
        // the buffer. there may be bits left in the buffer.
        // 2) copy off rline/val and keep the buffer.
    } else if (status == MCMC_MISS) {
        // miss.
    } else if (status == MCMC_FAIL) {
        int code = mcmc_fail_code(c);
        // MCMC_FAIL_NOT_STORED, MCMC_FAIL_NOT_FOUND, MCMC_FAIL_EXISTS, etc.
        // TODO: code_str() ?
    } else if (status == MCMC_ERR) {
        // some kind of command specific error code (management commands)
        // or protocol error status.
        char code[MCMC_ERROR_CODE_MAX];
        char msg[MCMC_ERROR_MSG_MAX];
        mcmc_get_error(c, code, MCMC_ERROR_CODE_MAX, msg, MCMC_ERROR_MSG_MAX);
        fprintf(stderr, "Got error from mc: code [%s] msg: [%s]\n", code, msg);
        // some errors don't have a msg. in this case msg[0] will be \0
    }

    int remain = 0;
    // advance us to the next command in the buffer, or ready for the next
    // mc_read().
    char *newbuf = mcmc_buffer_consume(c, &remain);
    if (remain == 0) {
        assert(newbuf == NULL);
        // we're done.
    } else {
        // there're still some bytes unconsumed by the client.
        // ensure the next time we call the client, the buffer has those
        // bytes at the front still.
        // NOTE: this _could_ be an entirely different buffer if we copied
        // the data off. The client is just tracking the # of bytes it
        // didn't gobble.
        // In this case we shuffle the bytes back to the front of our read
        // buffer.
        memmove(rbuf, newbuf, remain);
    }

    status == mcmc_disconnect(c);
    // The only free'ing needed.
    free(c);

    // TODO: stats example.

    // TODO: nonblock example.

    return 0;
}
