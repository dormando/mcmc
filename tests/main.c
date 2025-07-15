#include "utest.h"
#define MCMC_TEST 1 // FIXME: re-run bear and remove this.
#include "../mcmc.h"

// token check.
struct mc_tc {
    int len;
    char *tok;
};

#define MAX_TC 100
struct mc_tokenize {
    mcmc_tokenizer_t t;
    int res; // result from tokenizer run
    int ntokens;
    uint64_t metaflags;
    const char *line;
    int llen;
    int ntc;
    struct mc_tc tc[MAX_TC];
};

UTEST_F_SETUP(mc_tokenize) {
    // TODO: check if utest is doing this for us.
    memset(utest_fixture, 0, sizeof(*utest_fixture));
}

UTEST_F_TEARDOWN(mc_tokenize) {
    if (utest_fixture->res == 0) {
        ASSERT_EQ(utest_fixture->t.ntokens, utest_fixture->ntokens);
        ASSERT_EQ(utest_fixture->t.metaflags, utest_fixture->metaflags);
        for (int x = 0; x < utest_fixture->ntc; x++) {
            struct mc_tc tc = utest_fixture->tc[x];
            ASSERT_EQ(_mcmc_token_len(utest_fixture->line, &utest_fixture->t, x), tc.len);
            ASSERT_STRNEQ(tc.tok, _mcmc_token(utest_fixture->line, &utest_fixture->t, x, NULL), tc.len);
        }
    }
    // else assume the main utest is doing some validation.
}

#define M(n, k) \
    do { \
        utest_fixture->line = line; \
        utest_fixture->llen = llen; \
        utest_fixture->ntc = n; \
        utest_fixture->ntokens = k; \
        memcpy(utest_fixture->tc, c, sizeof(c)); \
        utest_fixture->res = res; \
    } while(0); \

UTEST_F(mc_tokenize, asciiset) {
    const char *line = "set foo 5 10 2\r\n";
    int llen = strlen(line);
    struct mc_tc c[5] = {
        {3, "set"}, {3, "foo"}, {1, "5"}, {2, "10"}, {1, "2"},
    };

    int res = _mcmc_tokenize_meta(&utest_fixture->t, line, llen, 250, MCMC_PARSER_MAX_TOKENS-1);
    M(5, 5)
}

UTEST_F(mc_tokenize, asciiget) {
    const char *line = "get foobar\r\n";
    int llen = strlen(line);
    struct mc_tc c[2] = {
        {3, "get"}, {6, "foobar"},
    };
    int res = _mcmc_tokenize_meta(&utest_fixture->t, line, llen, 250, MCMC_PARSER_MAX_TOKENS-1);
    M(2, 2)
}

// give a shorter len than the string and ensure proper parsing
UTEST_F(mc_tokenize, asciishort) {
    const char *line = "one two three four\r\n";
    int llen = strlen(line) - 7;
    struct mc_tc c[3] = {
        {3, "one"}, {3, "two"}, {4, "thre"},
    };
    int res = _mcmc_tokenize_meta(&utest_fixture->t, line, llen, 999, MCMC_PARSER_MAX_TOKENS-1);
    M(3, 3)
}

#define mfbit(f) ((uint64_t)1 << (f - 65))

UTEST_F(mc_tokenize, metaget) {
    const char *line = "mg foo s t Oabcd\r\n";
    int llen = strlen(line);
    struct mc_tc c[5] = {
        {2, "mg"}, {3, "foo"}, {1, "s"}, {1, "t"}, {5, "Oabcd"},
    };
    utest_fixture->metaflags = mfbit('s') | mfbit('t') | mfbit('O');
    int res = _mcmc_tokenize_meta(&utest_fixture->t, line, llen, 2, MCMC_PARSER_MAX_TOKENS-1);
    M(5, 5);
}

UTEST_F(mc_tokenize, garbage) {
    const char *line = "??P <?? || @#@# !**! *!#$\r\n";
    int llen = strlen(line);
    int res = _mcmc_tokenize_meta(&utest_fixture->t, line, llen, 2, MCMC_PARSER_MAX_TOKENS-1);
    ASSERT_EQ(res, MCMC_NOK);
}

#undef M

#define MAX 1024
struct mc_valid {
    mcmc_resp_t r;
    short type;
    short res;
    short code;
    short rlen;
    char buf[MAX+1];
};

UTEST_F_SETUP(mc_valid) {
}

UTEST_F_TEARDOWN(mc_valid) {
    int res = mcmc_parse_buf(utest_fixture->buf,
            strlen(utest_fixture->buf), &utest_fixture->r);
    ASSERT_EQ(res, utest_fixture->res);
    ASSERT_EQ(utest_fixture->r.code, utest_fixture->code);
    ASSERT_EQ(utest_fixture->r.type, utest_fixture->type);
    if (utest_fixture->r.type == MCMC_RESP_META
            && utest_fixture->rlen != -99) {
        ASSERT_EQ(utest_fixture->r.rlen, utest_fixture->rlen);
    }
}

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

#define N(d, b, t, c, r) M(d, b, t, c, r, -99)

// check that responses match their codes
M(metaend, "EN\r\n", MCMC_RESP_META, MCMC_CODE_END, MCMC_OK, 0)
M(metaexists, "EX\r\n", MCMC_RESP_META, MCMC_CODE_EXISTS, MCMC_OK, 0)
M(metaok, "HD\r\n", MCMC_RESP_META, MCMC_CODE_OK, MCMC_OK, 0)
M(metanop, "MN\r\n", MCMC_RESP_META,  MCMC_CODE_NOP, MCMC_OK, 0)
//N(metadebug, "ME\r\n", MCMC_CODE_OK) // FIXME: needs code/type
M(metanotfound, "NF\r\n", MCMC_RESP_META, MCMC_CODE_NOT_FOUND, MCMC_OK, 0)
M(metanotstored, "NS\r\n", MCMC_RESP_META, MCMC_CODE_NOT_STORED, MCMC_OK, 0)
M(metavalue, "VA 2 t\r\nhi\r\n", MCMC_RESP_META, MCMC_CODE_OK, MCMC_OK, 1) // FIXME: does this make sense?
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

#undef M
#undef N

// explicitly test that meta rline starts in the right place and rlen is the
// correct length.
UTEST(metaresp, basic) {
    mcmc_resp_t resp = {0};
    char *rbuf = "HD t1 Ofoo\r\n";
    int res = mcmc_parse_buf(rbuf, strlen(rbuf), &resp);

    ASSERT_EQ(res, MCMC_OK);
    ASSERT_EQ(resp.type, MCMC_RESP_META);
    ASSERT_EQ(resp.code, MCMC_CODE_OK);

    ASSERT_EQ(resp.rlen, 7);
    ASSERT_STRNEQ("t1 Ofoo", resp.rline, resp.rlen);
}

UTEST(metaresp, space) {
    mcmc_resp_t resp = {0};
    char *rbuf = "HD     t1 Oquux\r\n";
    int res = mcmc_parse_buf(rbuf, strlen(rbuf), &resp);

    ASSERT_EQ(res, MCMC_OK);
    ASSERT_EQ(resp.type, MCMC_RESP_META);
    ASSERT_EQ(resp.code, MCMC_CODE_OK);

    ASSERT_EQ(resp.rlen, 8);
    ASSERT_STRNEQ("t1 Oquux", resp.rline, resp.rlen);
}

// TODO: also check with a VA and some value attached

// TODO:
// - check more easy error conditions
// - check that mcmc_resp_t gets filled out appropriately
// - check too many / too few tokens
// - check key of all lengths

struct mc_toktou32 {
    const char *tok;
    size_t tlen;
    int res;
    uint32_t out;
};
UTEST_F_SETUP(mc_toktou32) {
}
UTEST_F_TEARDOWN(mc_toktou32) {
    uint32_t out = 0;
    int res = mcmc_toktou32(utest_fixture->tok,
            utest_fixture->tlen, &out);
    ASSERT_EQ(res, utest_fixture->res);
    ASSERT_EQ(out, utest_fixture->out);
}

#define M(d, t, tl, r, o) \
    UTEST_F(mc_toktou32, d) { \
        if (tl == -1) { \
            utest_fixture->tlen = strlen(t); \
        } else { \
            utest_fixture->tlen = tl; \
        } \
        utest_fixture->tok = t; \
        utest_fixture->res = r; \
        utest_fixture->out = o; \
    }

M(toolong, "9876", 5000, -2, 0)
M(neglen, "5678", -2, -2, 0)
M(small, "1234", 3, 0, 123)
M(junk, "asdf", -1, -3, 0)
M(midjunk, "1234foo5678", -1, -3, 0)
M(neg, "-5", -1, -3, 0)
M(oneover, "4294967296", -1, -1, 0)
M(max, "4294967295", -1, 0, 4294967295)
M(overflow, "99999999999", -1, -1, 0)
M(five, "5", 1, 0, 5)
M(zero, "0", 1, 0, 0)
M(manyzero, "00000", -1, 0, 0)
M(series, "12345678", -1, 0, 12345678)

#undef M

struct mc_tokto32 {
    const char *tok;
    size_t tlen;
    int res;
    int32_t out;
};
UTEST_F_SETUP(mc_tokto32) {
}
UTEST_F_TEARDOWN(mc_tokto32) {
    int32_t out = 0;
    int res = mcmc_tokto32(utest_fixture->tok,
            utest_fixture->tlen, &out);
    ASSERT_EQ(res, utest_fixture->res);
    ASSERT_EQ(out, utest_fixture->out);
}

#define M(d, t, tl, r, o) \
    UTEST_F(mc_tokto32, d) { \
        if (tl == -1) { \
            utest_fixture->tlen = strlen(t); \
        } else { \
            utest_fixture->tlen = tl; \
        } \
        utest_fixture->tok = t; \
        utest_fixture->res = r; \
        utest_fixture->out = o; \
    }

M(toolong, "9876", 5000, -2, 0)
M(neglen, "5678", -2, -2, 0)
M(small, "1234", 3, 0, 123)
M(junk, "asdf", -1, -3, 0)
M(midjunk, "1234foo5678", -1, -3, 0)
M(neg, "-5", -1, 0, -5)
M(max, "2147483647", -1, 0, INT32_MAX)
M(oneover, "2147483648", -1, -1, 0)
M(min, "-2147483648", -1, 0, INT32_MIN)
M(oneunder, "-2147483649", -1, -1, 0)
M(overflow, "99999999999", -1, -1, 0)
M(underflow, "-9999999999", -1, -1, 0)
M(five, "5", 1, 0, 5)
M(zero, "0", 1, 0, 0)
M(manyzero, "00000", -1, 0, 0)
M(series, "12345678", -1, 0, 12345678)

#undef M

// TODO:
// mcmc_toktou64

struct mc_tokto64 {
    const char *tok;
    size_t tlen;
    int res;
    int64_t out;
};
UTEST_F_SETUP(mc_tokto64) {
}
UTEST_F_TEARDOWN(mc_tokto64) {
    int64_t out = 0;
    int res = mcmc_tokto64(utest_fixture->tok,
            utest_fixture->tlen, &out);
    ASSERT_EQ(res, utest_fixture->res);
    ASSERT_EQ(out, utest_fixture->out);
}

#define M(d, t, tl, r, o) \
    UTEST_F(mc_tokto64, d) { \
        if (tl == -1) { \
            utest_fixture->tlen = strlen(t); \
        } else { \
            utest_fixture->tlen = tl; \
        } \
        utest_fixture->tok = t; \
        utest_fixture->res = r; \
        utest_fixture->out = o; \
    }

M(negzerolen, "-1234", 0, 0, 0)
M(toolong, "9876", 5000, -2, 0)
M(neglen, "5678", -2, -2, 0)
M(small, "1234", 3, 0, 123)
M(junk, "asdf", -1, -3, 0)
M(midjunk, "1234foo5678", -1, -3, 0)
M(neg, "-5", -1, 0, -5)
M(max, "2147483647", -1, 0, INT32_MAX)
M(max64, "9223372036854775807", -1, 0, INT64_MAX)
M(oneover32, "2147483648", -1, 0, 2147483648LL)
M(min, "-2147483648", -1, 0, INT32_MIN)
M(min64, "-9223372036854775808", -1, 0, INT64_MIN)
M(oneunder32, "-2147483649", -1, 0, -2147483649LL)
M(overflow, "9999999999999999999", -1, -1, 0)
M(underflow, "-9999999999999999999", -1, -1, 0)
M(five, "5", 1, 0, 5)
M(zero, "0", 1, 0, 0)
M(manyzero, "00000", -1, 0, 0)
M(series, "12345678", -1, 0, 12345678)

#undef M

struct mc_toktou64 {
    const char *tok;
    size_t tlen;
    int res;
    uint64_t out;
};
UTEST_F_SETUP(mc_toktou64) {
}
UTEST_F_TEARDOWN(mc_toktou64) {
    uint64_t out = 0;
    int res = mcmc_toktou64(utest_fixture->tok,
            utest_fixture->tlen, &out);
    ASSERT_EQ(res, utest_fixture->res);
    ASSERT_EQ(out, utest_fixture->out);
}

#define M(d, t, tl, r, o) \
    UTEST_F(mc_toktou64, d) { \
        if (tl == -1) { \
            utest_fixture->tlen = strlen(t); \
        } else { \
            utest_fixture->tlen = tl; \
        } \
        utest_fixture->tok = t; \
        utest_fixture->res = r; \
        utest_fixture->out = o; \
    }

M(zerolen, "1234", 0, 0, 0)
M(toolong, "9876", 5000, -2, 0)
M(neglen, "5678", -2, -2, 0)
M(small, "1234", 3, 0, 123)
M(junk, "asdf", -1, -3, 0)
M(midjunk, "1234foo5678", -1, -3, 0)
M(neg, "-5", -1, -3, 0)
M(oneover32, "4294967296", -1, 0, UINT32_MAX+1LL)
M(max32, "4294967295", -1, 0, 4294967295)
M(max64,    "18446744073709551615", -1, 0, UINT64_MAX)
M(overflow, "99999999999999999999", -1, -1, 0)
M(five, "5", 1, 0, 5)
M(zero, "0", 1, 0, 0)
M(manyzero, "00000", -1, 0, 0)
M(series, "12345678", -1, 0, 12345678)

#undef M

UTEST(mflag, has) {
    const char *l = "mg foo s t v f O1234 k N5000\r\n";
    size_t len = strlen(l);
    mcmc_tokenizer_t t = {0};
    int res = _mcmc_tokenize_meta(&t, l, len, 2, 24);
    ASSERT_EQ(res, 0);
    ASSERT_EQ(mcmc_token_has_flag(l, &t, 's'), MCMC_OK);
}

// TODO: test failures
UTEST(mcmc_token, get_u32) {
    const char *l = "set f 1 22 333\r\n";
    size_t len = strlen(l);
    mcmc_tokenizer_t t = {0};
    int res = _mcmc_tokenize_meta(&t, l, len, 255, 24);
    ASSERT_EQ(res, 0);
    uint32_t num = 0;
    ASSERT_EQ(mcmc_token_get_u32(l, &t, 2, &num), MCMC_OK);
    ASSERT_EQ(num, 1);
    ASSERT_EQ(mcmc_token_get_u32(l, &t, 3, &num), MCMC_OK);
    ASSERT_EQ(num, 22);
    ASSERT_EQ(mcmc_token_get_u32(l, &t, 4, &num), MCMC_OK);
    ASSERT_EQ(num, 333);
}

// TODO: test failures
UTEST(mcmc_token, flag_int_u32) {
    const char *l = "mg foo C1234 O9999 N300\r\n";
    size_t len = strlen(l);
    mcmc_tokenizer_t t = {0};
    int res = _mcmc_tokenize_meta(&t, l, len, 2, 24);
    ASSERT_EQ(res, 0);
    uint32_t num = 0;
    ASSERT_EQ(mcmc_token_get_flag_u32(l, &t, 'C', &num), MCMC_OK);
    ASSERT_EQ(num, 1234);
    ASSERT_EQ(mcmc_token_get_flag_u32(l, &t, 'O', &num), MCMC_OK);
    ASSERT_EQ(num, 9999);
    ASSERT_EQ(mcmc_token_get_flag_u32(l, &t, 'N', &num), MCMC_OK);
    ASSERT_EQ(num, 300);
}

UTEST_MAIN()
