#include "utest.h"
#include "../mcmc.h"

struct tmc_fixt {
    void *ctx;
};

UTEST_F_SETUP(tmc_fixt) {
    utest_fixture->ctx = calloc(1, mcmc_size(MCMC_OPTION_BLANK));
    ASSERT_TRUE(utest_fixture->ctx);
}

UTEST_F_TEARDOWN(tmc_fixt) {
    free(utest_fixture->ctx);
}

UTEST_F(tmc_fixt, trial) {
    mcmc_resp_t r;
    char goodbuf[] = "VALUE foo 0 2\r\nhi\r\n";
    char badbuf[] = "bad response\r\n";
    int res = mcmc_parse_buf(utest_fixture->ctx, goodbuf, strlen(goodbuf), &r);
    ASSERT_GE(res, MCMC_OK);

    res = mcmc_parse_buf(utest_fixture->ctx, badbuf, strlen(badbuf), &r);
    ASSERT_LT(res, MCMC_OK);
}

UTEST_MAIN()
