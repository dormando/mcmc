#ifndef MCMC_HEADER
#define MCMC_HEADER

#define MCMC_OK 0
#define MCMC_ERR -1
#define MCMC_NOT_CONNECTED 1
#define MCMC_CONNECTED 2
#define MCMC_CONNECTING 3 // nonblock mode.
#define MCMC_WANT_WRITE 4
#define MCMC_WANT_READ 5
#define MCMC_MISS 6
#define MCMC_HAS_RESULT 7
// TODO: either internally set a flag for "ok" or "not ok" and use a func,
// or use a bitflag here (1<<6) for "OK", (1<<5) for "FAIL", etc.
// or, we directly return "OK" or "FAIL" and you can ask for specific error.
#define MCMC_STORED 8
#define MCMC_FAIL_EXISTS 9
#define MCMC_DELETED 10
#define MCMC_TOUCHED 11
#define MCMC_VERSION 12
#define MCMC_FAIL_NOT_FOUND 13
#define MCMC_FAIL_NOT_STORED 14
#define MCMC_FAIL 15
#define MCMC_NOP 16
#define MCMC_PARSE_ERROR_SHORT 17
#define MCMC_PARSE_ERROR 18

// response types
#define MCMC_RESP_GET 100
#define MCMC_RESP_META 101
// TODO: RESP_NUMERIC for incr/decr response? or copy as value?
#define MCMC_RESP_STAT 102
#define MCMC_RESP_GENERIC 103

#define MCMC_OPTION_BLANK 0

// convenience defines. if you want to save RAM you can set these smaller and
// error handler will only copy what you ask for.
#define MCMC_ERROR_CODE_MAX 32
#define MCMC_ERROR_MSG_MAX 512

size_t mcmc_size(int options);
size_t mcmc_min_buffer_size(int options);
int mcmc_connect(void *c, char *host, char *port, int options);
int mcmc_send_request(void *c, char *request, int len, int count);
int mcmc_read(void *c, char *buf, size_t bufsize);
char *mcmc_value(void *c);
int mcmc_has_value(void *c, size_t *vsize, int *ready);
int mcmc_read_value(void *c, char *val, const size_t vsize, int *read);
char *mcmc_buffer_consume(void *c, int *remain);
int mcmc_disconnect(void *c);
void mcmc_get_error(void *c, char *code, size_t clen, char *msg, size_t mlen);
int mcmc_fail_code(void *c);
int mcmc_resp_type(void *c);
int mcmc_resp_get(void *c, char **key, size_t *keylen, uint32_t *flags, uint64_t *cas);
int mcmc_resp_meta(void *c, char *rflags, size_t *rlen);

#endif
