#include "utest.h"
#include "../mcmc.h"

#define MAX 1024
struct mc_valid {
    void *ctx;
    mcmc_resp_t r;
    short res;
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
    free(utest_fixture->ctx);
}

#define N(d, b, c) \
    UTEST_F(mc_valid, d) { \
    do { \
        strncpy(utest_fixture->buf, b, MAX); \
        utest_fixture->res = c; \
    } while (0); \
    }

// check that responses match their codes
N(metaend, "EN\r\n", MCMC_CODE_END)
N(metaexists, "EX\r\n", MCMC_CODE_EXISTS)
N(metaok, "HD\r\n", MCMC_CODE_OK)
N(metanop, "MN\r\n", MCMC_CODE_NOP)
//N(metadebug, "ME\r\n", MCMC_CODE_OK) // FIXME: needs code/type
N(metanotfound, "NF\r\n", MCMC_CODE_NOT_FOUND)
N(metanotstored, "NS\r\n", MCMC_CODE_NOT_STORED)
N(metavalue, "VA 2 t\r\nhi\r\n", MCMC_CODE_OK) // FIXME: does this make sense?
N(metavalue2, "VA 2\r\nho\r\n", MCMC_CODE_OK)
N(generic, "OK\r\n", MCMC_CODE_OK)
N(end, "END\r\n", MCMC_CODE_END)
//N(stat, "STAT\r\n", MCMC_CODE_STAT) // FIXME: unfinished
N(value, "VALUE key 0 2\r\nhi\r\n", MCMC_CODE_OK)
N(valuecas, "VALUE key 0 2 5\r\nhi\r\n", MCMC_CODE_OK)
N(stored, "STORED\r\n", MCMC_CODE_STORED)
N(exists, "EXISTS\r\n", MCMC_CODE_EXISTS)
N(deleted, "DELETED\r\n", MCMC_CODE_DELETED)
N(touched, "TOUCHED\r\n", MCMC_CODE_TOUCHED)
N(version, "VERSION 1.1.1\r\n", MCMC_CODE_VERSION)
N(notfound, "NOT_FOUND\r\n", MCMC_CODE_NOT_FOUND)
N(notstored, "NOT_STORED\r\n", MCMC_CODE_NOT_STORED)

// check some error conditions
N(shortres, "S\r\n", -MCMC_ERR_SHORT)

#undef N

// TODO:
// - check more easy error conditions
// - check that mcmc_resp_t gets filled out appropriately
// - check too many / too few tokens
// - check key of all lengths

UTEST_MAIN()
