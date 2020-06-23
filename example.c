#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "mcmc.h"

// TODO: try writing a "simple_get()", set(), and meta() commands.

int main (int argc, char *agv[]) {
    // TODO: detect if C is pre-C11?
    printf("C version: %ld\n", __STDC_VERSION__);
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
    mcmc_resp_t resp;
    status = mcmc_read(c, rbuf, bufsize, &resp);
    if (status == MCMC_OK) {
        // OK means a response of some kind was read.
        char *val;
        // NOTE: is "it's not a miss, and vlen is 0" enough to indicate that
        // a 0 byte value was returned?
        if (resp.vlen != 0) {
            if (resp.vlen == resp.vlen_read) {
                val = resp.value;
            } else {
                val = malloc(resp.vlen);
                int read = 0;
                do {
                    status = mcmc_read_value(c, val, resp.vlen, &read);
                } while (status == MCMC_WANT_READ);
            }
            if (resp.vlen > 0) {
                val[resp.vlen-1] = '\0';
                printf("Response value: %s\n", val);
            }
        }
        switch (resp.type) {
            case MCMC_RESP_FAIL:
                // resp.fail_code
                break;
            case MCMC_RESP_GET:
                break;
            case MCMC_RESP_MISS: // ascii or meta miss.
                break;
            case MCMC_RESP_META: // any meta command. they all return the same.
                break;
            case MCMC_RESP_STAT:
                // STAT responses. need to call mcmc_read() in loop until
                // we get an end signal.
                break;
            default:
                // TODO: type -> str func.
                fprintf(stderr, "unknown response type: %d\n", resp.type);
                break;
        }
    } else {
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

    status = mcmc_disconnect(c);
    // The only free'ing needed.
    free(c);

    // TODO: stats example.

    // TODO: nonblock example.

    return 0;
}
