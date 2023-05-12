#include "utest.h"
#include "../mcmc.h"

#define MAX 1024
struct mc_valid {
    void *ctx;
    mcmc_resp_t r;
    short type;
    short res;
    short code;
    short rlen;
    char buf[MAX+1];
};

UTEST_F_SETUP(mc_valid) {
    utest_fixture->ctx = calloc(1, mcmc_size(MCMC_OPTION_BLANK));
    ASSERT_TRUE(utest_fixture->ctx);
}

UTEST_F_TEARDOWN(mc_valid) {
    int res = mcmc_parse_buf(utest_fixture->ctx, utest_fixture->buf,
            strlen(utest_fixture->buf), &utest_fixture->r);
    ASSERT_EQ(res, utest_fixture->res);
    ASSERT_EQ(utest_fixture->r.code, utest_fixture->code);
    ASSERT_EQ(utest_fixture->r.type, utest_fixture->type);
    if (utest_fixture->r.type == MCMC_RESP_META) {
        ASSERT_EQ(utest_fixture->r.rlen, utest_fixture->rlen);
    }
    free(utest_fixture->ctx);
}

// meta response test
#define M(d, b, t, c, r, l) \
    UTEST_F(mc_valid, d) { \
    do { \
        strncpy(utest_fixture->buf, b, MAX); \
        utest_fixture->type = t; \
        utest_fixture->res = r; \
        utest_fixture->code = c; \
        utest_fixture->rlen = l; \
    } while (0); \
    }

// none-meta response test
#define N(d, b, t, c, r) \
    UTEST_F(mc_valid, d) { \
    do { \
        strncpy(utest_fixture->buf, b, MAX); \
        utest_fixture->type = t; \
        utest_fixture->res = r; \
        utest_fixture->code = c; \
    } while (0); \
    }

// check that responses match their codes
M(metaend, "EN\r\n", MCMC_RESP_META, MCMC_CODE_END, MCMC_OK, 0)
M(metaexists, "EX\r\n", MCMC_RESP_META, MCMC_CODE_EXISTS, MCMC_OK, 0)
M(metaok, "HD\r\n", MCMC_RESP_META, MCMC_CODE_OK, MCMC_OK, 0)
M(metaok2, "HD O123 C123\r\n", MCMC_RESP_META, MCMC_CODE_OK, MCMC_OK, 11)
M(metanop, "MN\r\n", MCMC_RESP_META,  MCMC_CODE_NOP, MCMC_OK, 0)
//N(metadebug, "ME\r\n", MCMC_CODE_OK) // FIXME: needs code/type
M(metanotfound, "NF\r\n", MCMC_RESP_META, MCMC_CODE_NOT_FOUND, MCMC_OK, 0)
M(metanotstored, "NS\r\n", MCMC_RESP_META, MCMC_CODE_NOT_STORED, MCMC_OK, 0)
M(metavalue, "VA 2 t\r\nhi\r\n", MCMC_RESP_META, MCMC_CODE_OK, MCMC_OK, 3) // FIXME: does this make sense?
M(metavalue2, "VA 2\r\nho\r\n", MCMC_RESP_META, MCMC_CODE_OK, MCMC_OK, 0)
N(generic, "OK\r\n", MCMC_RESP_GENERIC, MCMC_CODE_OK, MCMC_OK)
N(end, "END\r\n", MCMC_RESP_END, MCMC_CODE_END, MCMC_OK)
//N(stat, "STAT\r\n", MCMC_CODE_STAT) // FIXME: unfinished
N(value, "VALUE key 0 2\r\nhi\r\n", MCMC_RESP_GET, MCMC_CODE_OK, MCMC_OK)
N(valuecas, "VALUE key 0 2 5\r\nhi\r\n", MCMC_RESP_GET, MCMC_CODE_OK, MCMC_OK)
N(stored, "STORED\r\n", MCMC_RESP_GENERIC, MCMC_CODE_STORED, MCMC_OK)
N(exists, "EXISTS\r\n", MCMC_RESP_GENERIC, MCMC_CODE_EXISTS, MCMC_OK)
N(deleted, "DELETED\r\n", MCMC_RESP_GENERIC, MCMC_CODE_DELETED, MCMC_OK)
N(touched, "TOUCHED\r\n", MCMC_RESP_GENERIC, MCMC_CODE_TOUCHED, MCMC_OK)
N(version, "VERSION 1.1.1\r\n", MCMC_RESP_VERSION, MCMC_CODE_VERSION, MCMC_OK)
N(notfound, "NOT_FOUND\r\n", MCMC_RESP_GENERIC, MCMC_CODE_NOT_FOUND, MCMC_OK)
N(notstored, "NOT_STORED\r\n", MCMC_RESP_GENERIC, MCMC_CODE_NOT_STORED, MCMC_OK)

// check some error conditions
N(shortres, "S\r\n", MCMC_RESP_FAIL, MCMC_ERR_SHORT, MCMC_ERR)

// check error messages
N(errorcode, "ERROR\r\n", MCMC_RESP_ERRMSG, MCMC_CODE_ERROR, MCMC_ERR)
N(errormsgcode, "ERROR shutdown not enabled\r\n", MCMC_RESP_ERRMSG, MCMC_CODE_ERROR, MCMC_ERR)
N(errorcli, "CLIENT_ERROR you did a bad thing\r\n", MCMC_RESP_ERRMSG,MCMC_CODE_CLIENT_ERROR, MCMC_ERR)
N(errorsrv, "SERVER_ERROR we could not help you\r\n", MCMC_RESP_ERRMSG,MCMC_CODE_SERVER_ERROR, MCMC_ERR)

#undef N

// TODO:
// - check more easy error conditions
// - check that mcmc_resp_t gets filled out appropriately
// - check too many / too few tokens
// - check key of all lengths

UTEST_MAIN()
