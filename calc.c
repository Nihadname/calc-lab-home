// Nihad Ibadzade 251ADB064
// https://github.com/Nihadname/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: Cursor Grok. I asked it to help with the parser and reading the file. this chat

// output name is like task1_nihad_ibadzade_251ADB064.txt

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define STU_FIRST "nihad"
#define STU_LAST "ibadzade"
#define STU_ID "251ADB064"

// max and min for long long,didnt want to include limits.h
#define LL_MAX 9223372036854775807LL
#define LL_MIN (-9223372036854775807LL - 1)

typedef struct {
    int is_float;
    long long ival;
    double dval;
} Value;

typedef enum {
    TOK_EOF = 0,
    TOK_NL,
    TOK_NUM,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_POW,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_BAD
} TokKind;

typedef struct {
    TokKind kind;
    size_t pos;
    Value val;
} Token;

typedef struct {
    char *src;
    size_t len;
    size_t i;
    Token cur;
    int failed;
    size_t err_pos;
} Parser;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Buf;

static Value num_int(long long x) {
    Value v;
    v.is_float = 0;
    v.ival = x;
    v.dval = 0.0;
    return v;
}

static Value num_float(double x) {
    Value v;
    v.is_float = 1;
    v.ival = 0;
    v.dval = x;
    return v;
}

static double as_double(Value v) {
    return v.is_float ? v.dval : (double)v.ival;
}

static int is_zero(Value v) {
    if (v.is_float) {
        return v.dval == 0.0;
    }
    return v.ival == 0;
}

static void fail_at(Parser *p, size_t pos) {
    // keep the first error,a leter one should not replace it
    if (!p->failed) {
        p->failed = 1;
        p->err_pos = pos;
    }
}

// read the file once. when the buffer is full i double it, so a big file is not copied again and again.
// ai: prompt "how do i read a whole file in c if i dont know the size". i used the realloc *2 idea.
static char *read_all(FILE *fp, size_t *out_len) {
    size_t cap = 4096;
    size_t n = 0;
    char *buf = malloc(cap);
    if (!buf) {
        return NULL;
    }
    for (;;) {
        if (n == cap) {
            if (cap > (size_t)-1 / 2) {
                free(buf);
                return NULL;
            }
            size_t ncap = cap * 2;
            char *nb = realloc(buf, ncap);
            if (!nb) {
                free(buf);
                return NULL;
            }
            buf = nb;
            cap = ncap;
        }
        size_t got = fread(buf + n, 1, cap - n, fp);
        n += got;
        if (got == 0) {
            if (ferror(fp)) {
                free(buf);
                return NULL;
            }
            break;
        }
    }
    char *nb = realloc(buf, n + 1);
    if (!nb) {
        free(buf);
        return NULL;
    }
    nb[n] = '\0';
    *out_len = n;
    return nb;
}


static void scan_number(Parser *p) {
    size_t start = p->i;
    size_t pos = p->i + 1;
    long long ival = 0;
    int overflow = 0;

    while(p->i < p->len && isdigit((unsigned char)p->src[p->i])) {
        int d = p->src[p->i] - '0';
        if(!overflow) {
            if (ival > (LL_MAX - d) / 10) {
                overflow = 1;
            } else {
                ival = ival*10 + d;
            }
        }
        p->i++;
    }

    // a float needs digits, a dot, then more digits. 5. and .5 are not ok, the dot stays and becomes a bad char. 1.5e3 fails on the e.
    int is_float = 0;
    if(p->i < p->len && p->src[p->i]=='.' &&
        p->i+1 < p->len && isdigit((unsigned char)p->src[p->i+1])) {
        is_float = 1;
        p->i++;
        while(p->i < p->len && isdigit((unsigned char)p->src[p->i])) {
            p->i++;
        }
    }

    if(is_float) {
        // put a temporary 0 so strtod can read it,i dont copy the number
        char saved = p->src[p->i];
        p->src[p->i] = '\0';
        char *end = NULL;
        double d = strtod(p->src + start, &end);
        int bad = (end != p->src + p->i) || !isfinite(d);
        p->src[p->i] = saved;
        if (bad) {
            p->cur.kind = TOK_BAD;
            p->cur.pos = pos;
            fail_at(p, pos);
            return;
        }
        p->cur.kind = TOK_NUM;
        p->cur.pos = pos;
        p->cur.val = num_float(d);
        return;
    }

    if(overflow) {
        // number dont fit in long long just error instead of wrapping
        p->cur.kind = TOK_BAD;
        p->cur.pos = pos;
        fail_at(p, pos);
        return;
    }

    p->cur.kind = TOK_NUM;
    p->cur.pos = pos;
    p->cur.val = num_int(ival);
}

static void advance(Parser *p) {
    if(p->failed) {
        return;
    }
    p->cur.val = num_int(0);

    while(p->i < p->len && (p->src[p->i]==' ' || p->src[p->i]=='\t')) {
        p->i++;
    }
    if (p->i >= p->len) {
        //no newline at the end, so the error is one past the last char
        p->cur.kind = TOK_EOF;
        p->cur.pos = p->len + 1;
        return;
    }

    char c = p->src[p->i];
    size_t pos = p->i + 1;
    if(c=='\n') {
        p->cur.kind = TOK_NL;
        p->cur.pos = pos;
        p->i++;
        return;
    }
    if(c=='+') {
        p->cur.kind = TOK_PLUS;
        p->cur.pos = pos;
        p->i++;
        return;
    }
    if (c=='-') {
        p->cur.kind = TOK_MINUS;
        p->cur.pos = pos;
        p->i++;
        return;
    }
    if(c == '/') {
        p->cur.kind = TOK_SLASH;
        p->cur.pos = pos;
        p->i++;
        return;
    }
    if(c=='(') {
        p->cur.kind = TOK_LPAREN;
        p->cur.pos = pos;
        p->i++;
        return;
    }
    if (c == ')') {
        p->cur.kind = TOK_RPAREN;
        p->cur.pos = pos;
        p->i++;
        return;
    }
    if(c=='*') {
        // two stars together is **, one star is just multiply. a space between them is not **
        if(p->i+1 < p->len && p->src[p->i+1]=='*') {
            p->cur.kind = TOK_POW;
            p->cur.pos = pos;
            p->i += 2;
        } else {
            p->cur.kind = TOK_STAR;
            p->cur.pos = pos;
            p->i++;
        }
        return;
    }
    if(isdigit((unsigned char)c)) {
        scan_number(p);
        return;
    }

    // anything else (# in the middle, a letter, a dot) is  invalid
    p->cur.kind = TOK_BAD;
    p->cur.pos = pos;
    p->i++;
}

// abs, but the most negative long long cant be negated normally so i do it like this
static unsigned long long uabs_ll(long long a) {
    if(a >= 0) {
        return (unsigned long long)a;
    }
    return (unsigned long long)(-(a + 1)) + 1ULL;
}

// multiply into *out, or -1 if the result dont fit
static int mul_ll(long long a, long long b, long long *out) {
    if(a==0 || b==0) {
        *out = 0;
        return 0;
    }
    unsigned long long ua = uabs_ll(a);
    unsigned long long ub = uabs_ll(b);
    if (ub != 0 && ua > ~0ULL / ub) {
        return -1;
    }
    unsigned long long ur = ua * ub;
    int neg = (a<0) ^ (b<0);
    if(!neg) {
        if (ur > (unsigned long long)LL_MAX) {
            return -1;
        }
        *out = (long long)ur;
        return 0;
    }
    if (ur > (unsigned long long)LL_MAX + 1ULL) {
        return -1;
    }
    if (ur == (unsigned long long)LL_MAX + 1ULL) {
        *out = LL_MIN;
        return 0;
    }
    *out = -(long long)ur;
    return 0;
}

static int add_ll(long long a, long long b, long long *out) {
    if ((b > 0 && a > LL_MAX - b) || (b < 0 && a < LL_MIN - b)) {
        return -1;
    }
    *out = a + b;
    return 0;
}

static int sub_ll(long long a, long long b, long long *out) {
    if ((b > 0 && a < LL_MIN + b) || (b < 0 && a > LL_MAX + b)) {
        return -1;
    }
    *out = a - b;
    return 0;
}

// if the number had a dot its a double, otherwise long long. if one side is float i do the math in double.
// () doesnt change the type, it just returns whats inside. int/int cuts toward 0, (0-7)/2 is -3.
// negative power is an error only when both are ints. if one is float i use pow() instead.
static Value apply_add(Parser *p, Value a, Value b, int is_sub, size_t op_pos) {
    if (a.is_float || b.is_float) {
        double r = is_sub ? as_double(a) - as_double(b) : as_double(a) + as_double(b);
        if (!isfinite(r)) {
            fail_at(p, op_pos);
            return num_int(0);
        }
        return num_float(r);
    }
    long long r = 0;
    int rc = is_sub ? sub_ll(a.ival, b.ival, &r) : add_ll(a.ival, b.ival, &r);
    if (rc != 0) {
        fail_at(p, op_pos);
        return num_int(0);
    }
    return num_int(r);
}

static Value apply_mul(Parser *p, Value a, Value b, size_t op_pos) {
    if (a.is_float || b.is_float) {
        double r = as_double(a) * as_double(b);
        if (!isfinite(r)) {
            fail_at(p, op_pos);
            return num_int(0);
        }
        return num_float(r);
    }
    long long r = 0;
    if (mul_ll(a.ival, b.ival, &r) != 0) {
        fail_at(p, op_pos);
        return num_int(0);
    }
    return num_int(r);
}

// zero is already checked before this, so this is not the /0 error
static Value apply_div(Parser *p, Value a, Value b, size_t op_pos) {
    if (a.is_float || b.is_float) {
        double r = as_double(a) / as_double(b);
        if (!isfinite(r)) {
            fail_at(p, op_pos);
            return num_int(0);
        }
        return num_float(r);
    }
    if (a.ival == LL_MIN && b.ival == -1) {
        fail_at(p, op_pos);
        return num_int(0);
    }
    return num_int(a.ival / b.ival);
}

// exp is >= 0. anything ** 0 is 1, even 0**0. i square it so its not a slow loop.
static int ipow(long long base, long long exp, long long *out) {
    long long result = 1;
    unsigned long long e = (unsigned long long)exp;
    while (e > 0) {
        if (e & 1ULL) {
            if (mul_ll(result, base, &result) != 0) {
                return -1;
            }
        }
        e >>= 1;
        if (e > 0 && mul_ll(base, base, &base) != 0) {
            return -1;
        }
    }
    *out = result;
    return 0;
}

static Value apply_pow(Parser *p, Value base, Value exp, size_t op_pos, size_t exp_pos) {
    if(!base.is_float && !exp.is_float) {
        if(exp.ival < 0) {
            // like 2 ** (1 - 2). the value is negative but i point at the start of the exponent, not the minus
            fail_at(p, exp_pos);
            return num_int(0);
        }
        long long r = 0;
        if (ipow(base.ival, exp.ival, &r) != 0) {
            fail_at(p, op_pos);
            return num_int(0);
        }
        return num_int(r);
    }
    double r = pow(as_double(base), as_double(exp));
    if (!isfinite(r)) {
        // 10.0 ** 400 is inf, (0-8) ** 0.5 is nan. both point at the **
        fail_at(p, op_pos);
        return num_int(0);
    }
    return num_float(r);
}

static Value parse_expr(Parser *p);

// expr is + and -, it calls term. term is * and /, it calls power. power is **. so ** is done before * / and those before + -.
// + - * / use a loop and update the left side first, so 7-2-1 is (7-2)-1. thats left to right.
// ** dont use a loop. power() calls itself for the exponent, so 2**3**2 is 2**(3**2). thats why ** goes to the right.
// ai: prompt was the grammar in the lab (expr, term, power, primary)
static Value parse_primary(Parser *p) {
    if(p->failed) {
        return num_int(0);
    }
    if(p->cur.kind == TOK_NUM) {
        Value v = p->cur.val;
        advance(p);
        return v;
    }
    if(p->cur.kind == TOK_LPAREN) {
        advance(p);
        Value v = parse_expr(p);
        if (p->failed) {
            return v;
        }
        if(p->cur.kind != TOK_RPAREN) {
            // missing ), this is the newline or the end of the file
            fail_at(p, p->cur.pos);
            return v;
        }
        advance(p);
        return v;
    }
    // not a number and not '('. this is -5, or 7 + * 3, or (), or a bad char. the position is that token.
    fail_at(p, p->cur.pos);
    return num_int(0);
}

static Value parse_power(Parser *p) {
    Value left = parse_primary(p);
    if(p->failed || p->cur.kind != TOK_POW) {
        return left;
    }
    size_t op_pos = p->cur.pos;
    advance(p);
    // remember where the exponent starts, before i parse it
    size_t exp_pos = p->cur.pos;
    Value exp = parse_power(p);
    if(p->failed) {
        return left;
    }
    return apply_pow(p, left, exp, op_pos, exp_pos);
}

static Value parse_term(Parser *p) {
    Value left = parse_power(p);
    while(!p->failed && (p->cur.kind==TOK_STAR || p->cur.kind==TOK_SLASH)) {
        int op = p->cur.kind;
        size_t op_pos = p->cur.pos;
        advance(p);
        // start of the thing we divide by. 10/0 points at 0, 8/(...) points at the ( if the inside is 0.
        size_t right_pos = p->cur.pos;
        Value right = parse_power(p);
        if(p->failed) {
            return left;
        }
        if(op==TOK_SLASH && is_zero(right)) {
            fail_at(p, right_pos);
            return left;
        }
        if(op == TOK_STAR) {
            left = apply_mul(p, left, right, op_pos);
        } else {
            left = apply_div(p, left, right, op_pos);
        }
    }
    return left;
}

static Value parse_expr(Parser *p) {
    Value left = parse_term(p);
    while(!p->failed && (p->cur.kind==TOK_PLUS || p->cur.kind==TOK_MINUS)) {
        int op = p->cur.kind;
        size_t op_pos = p->cur.pos;
        advance(p);
        Value right = parse_term(p);
        if (p->failed) {
            return left;
        }
        left = apply_add(p, left, right, op==TOK_MINUS, op_pos);
    }
    return left;
}

static int buf_append(Buf *b, const char *s, size_t n) {
    if (n > (size_t)-1 - 1 || b->len > (size_t)-1 - 1 - n) {
        return -1;
    }
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 64;
        while (b->len + n + 1 > cap) {
            if (cap > (size_t)-1 / 2) {
                return -1;
            }
            cap *= 2;
        }
        char *nd = realloc(b->data, cap);
        if (!nd) {
            return -1;
        }
        b->data = nd;
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

static int append_value(Buf *b, Value v) {
    char tmp[128];
    int n;
    if (v.is_float) {
        double d = v.dval;
        // -0 prints weird, its still zero so i make it normal 0
        if (d == 0.0) {
            d = 0.0;
        }
        n = snprintf(tmp, sizeof tmp, "%.15g\n", d);
    } else {
        n = snprintf(tmp, sizeof tmp, "%lld\n", v.ival);
    }
    if (n < 0 || (size_t)n >= sizeof tmp) {
        return -1;
    }
    return buf_append(b, tmp, (size_t)n);
}

// 0 = ok, 1 = the file has an ERROR (thats still a normal result), -1 = out of memory.
// i keep the results here and write them at the end. if a later line fails, the earlier answers must not be written.
static int evaluate(char *src, size_t len, Buf *out, size_t *err_pos) {
    Parser p;
    memset(&p, 0, sizeof p);
    p.src = src;
    p.len = len;

    while(p.i < p.len && !p.failed) {
        while(p.i < p.len && (src[p.i]==' ' || src[p.i]=='\t')) {
            p.i++;
        }
        if (p.i >= p.len) {
            break;
        }
        if(src[p.i]=='\n') {
            p.i++;
            continue;
        }
        // # at the start of the line (spaces before it are ok) is a comment. the chars still count for the position.
        // a # after a real expression is not a comment, its just a bad character.
        if(src[p.i]=='#') {
            while(p.i < p.len && src[p.i] != '\n') {
                p.i++;
            }
            if(p.i < p.len && src[p.i]=='\n') {
                p.i++;
            }
            continue;
        }

        advance(&p);
        Value v = parse_expr(&p);
        if(p.failed) {
            break;
        }
        if(p.cur.kind != TOK_NL && p.cur.kind != TOK_EOF) {
            // extra ), or another number, or a # that is not at the start
            fail_at(&p, p.cur.pos);
            break;
        }
        if (append_value(out, v) != 0) {
            return -1;
        }
    }

    if (p.failed) {
        *err_pos = p.err_pos;
        return 1;
    }
    return 0;
}

static char *dupstr(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) {
        memcpy(p, s, n);
    }
    return p;
}

static char *file_stem(const char *path) {
    const char *base = path;
    for (const char *s = path; *s; s++) {
        if (*s == '/') {
            base = s + 1;
        }
    }
    size_t n = strlen(base);
    if (n >= 4 && memcmp(base + n - 4, ".txt", 4) == 0) {
        n -= 4;
    }
    char *out = malloc(n + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, base, n);
    out[n] = '\0';
    return out;
}

static char *dir_stem(const char *path) {
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/') {
        n--;
    }
    size_t start = 0;
    for (size_t i = 0; i < n; i++) {
        if (path[i] == '/') {
            start = i + 1;
        }
    }
    size_t len = n - start;
    char *out = malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, path + start, len);
    out[len] = '\0';
    return out;
}

static char *join_path(const char *dir, const char *name) {
    size_t n = strlen(dir) + 1 + strlen(name) + 1;
    char *s = malloc(n);
    if (!s) {
        return NULL;
    }
    snprintf(s, n, "%s/%s", dir, name);
    return s;
}

static char *result_path(const char *outdir, const char *stem) {
    size_t n = strlen(outdir) + strlen(stem) + strlen(STU_FIRST) + strlen(STU_LAST) +
               strlen(STU_ID) + 9;
    char *s = malloc(n);
    if (!s) {
        return NULL;
    }
    snprintf(s, n, "%s/%s_%s_%s_%s.txt", outdir, stem, STU_FIRST, STU_LAST, STU_ID);
    return s;
}

static char *default_dir_from_stem(const char *stem) {
    size_t n = strlen(stem) + strlen(STU_FIRST) + strlen(STU_ID) + 3;
    char *s = malloc(n);
    if (!s) {
        return NULL;
    }
    snprintf(s, n, "%s_%s_%s", stem, STU_FIRST, STU_ID);
    return s;
}

static int ensure_dir(const char *path) {
    if (mkdir(path, 0755) == 0) {
        return 0;
    }
    if (errno == EEXIST) {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            return 0;
        }
    }
    fprintf(stderr, "calc: cannot create output directory '%s'\n", path);
    return -1;
}

static int write_all(const char *path, const char *data, size_t len) {
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return -1;
    }
    if (len > 0 && fwrite(data, 1, len, fp) != len) {
        fclose(fp);
        remove(path);
        return -1;
    }
    if (fclose(fp) != 0) {
        remove(path);
        return -1;
    }
    return 0;
}

// 0 if the result file got written, even when the text is ERROR.-1 means  if i couldnt read or write.
static int process_file(const char *in_path, const char *outdir) {
    struct stat st;
    if (stat(in_path, &st) != 0) {
        fprintf(stderr, "calc: cannot open '%s': %s\n", in_path, strerror(errno));
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "calc: '%s' is not a file\n", in_path);
        return -1;
    }

    FILE *fp = fopen(in_path, "rb");
    if (!fp) {
        fprintf(stderr, "calc: cannot open '%s': %s\n", in_path, strerror(errno));
        return -1;
    }
    size_t len = 0;
    char *src = read_all(fp, &len);
    fclose(fp);
    if (!src) {
        fprintf(stderr, "calc: cannot read '%s'\n", in_path);
        return -1;
    }

    Buf out;
    memset(&out, 0, sizeof out);
    size_t err_pos = 0;
    int ev = evaluate(src, len, &out, &err_pos);
    free(src);
    if (ev < 0) {
        free(out.data);
        fprintf(stderr, "calc: out of memory\n");
        return -1;
    }

    char *stem = file_stem(in_path);
    char *path = stem ? result_path(outdir, stem) : NULL;
    free(stem);
    if (!path) {
        free(out.data);
        fprintf(stderr, "calc: out of memory\n");
        return -1;
    }

    int rc;
    if (ev == 1) {
        char line[64];
        int n = snprintf(line, sizeof line, "ERROR:%zu\n", err_pos);
        if (n < 0 || (size_t)n >= sizeof line) {
            fprintf(stderr, "calc: cannot write '%s'\n", path);
            free(out.data);
            free(path);
            return -1;
        }
        rc = write_all(path, line, (size_t)n);
    } else {
        rc = write_all(path, out.data ? out.data : "", out.len);
    }
    free(out.data);
    if (rc != 0) {
        fprintf(stderr, "calc: cannot write '%s'\n", path);
        free(path);
        return -1;
    }
    free(path);
    return 0;
}

static int cmp_str(const void *a, const void *b) {
    const char *const *sa = a;
    const char *const *sb = b;
    return strcmp(*sa, *sb);
}

static int process_dir(const char *dir, const char *outdir) {
    DIR *dp = opendir(dir);
    if (!dp) {
        fprintf(stderr, "calc: cannot open directory '%s': %s\n", dir, strerror(errno));
        return -1;
    }

    char **names = NULL;
    size_t len = 0;
    size_t cap = 0;
    int failed = 0;
    errno = 0;
    struct dirent *ent;
    while ((ent = readdir(dp)) != NULL) {
        const char *nm = ent->d_name;
        size_t nl = strlen(nm);
        if (nl < 4 || strcmp(nm + nl - 4, ".txt") != 0) {
            continue;
        }
        char *full = join_path(dir, nm);
        if (!full) {
            failed = 1;
            break;
        }
        struct stat st;
        int regular = (stat(full, &st) == 0 && S_ISREG(st.st_mode));
        free(full);
        if (!regular) {
            continue;
        }
        if (len == cap) {
            size_t ncap = cap ? cap * 2 : 8;
            char **nn = realloc(names, ncap * sizeof *nn);
            if (!nn) {
                failed = 1;
                break;
            }
            names = nn;
            cap = ncap;
        }
        names[len] = dupstr(nm);
        if (!names[len]) {
            failed = 1;
            break;
        }
        len++;
    }
    if (errno != 0) {
        failed = 1;
    }
    closedir(dp);
    if (failed) {
        for (size_t i = 0; i < len; i++) {
            free(names[i]);
        }
        free(names);
        fprintf(stderr, "calc: cannot read directory '%s'\n", dir);
        return -1;
    }

    if (len > 1) {
        qsort(names, len, sizeof *names, cmp_str);
    }

    int rc = 0;
    for (size_t i = 0; i < len; i++) {
        char *full = join_path(dir, names[i]);
        if (!full || process_file(full, outdir) != 0) {
            rc = -1;
            free(full);
            break;
        }
        free(full);
    }
    for (size_t i = 0; i < len; i++) {
        free(names[i]);
    }
    free(names);
    return rc;
}

static void usage(void) {
    fprintf(stderr,
            "usage: calc [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] [input.txt]\n");
}

static int parse_args(int argc, char **argv, const char **dir, const char **outdir,
                      const char **input) {
    *dir = NULL;
    *outdir = NULL;
    *input = NULL;
    if (argc < 2) {
        usage();
        return -1;
    }
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-d") == 0 || strcmp(a, "--dir") == 0) {
            if (*dir) {
                fprintf(stderr, "calc: -d given more than once\n");
                return -1;
            }
            if (i + 1 >= argc) {
                fprintf(stderr, "calc: %s needs a directory\n", a);
                return -1;
            }
            *dir = argv[++i];
        } else if (strcmp(a, "-o") == 0 || strcmp(a, "--output-dir") == 0) {
            if (*outdir) {
                fprintf(stderr, "calc: -o given more than once\n");
                return -1;
            }
            if (i + 1 >= argc) {
                fprintf(stderr, "calc: %s needs a directory\n", a);
                return -1;
            }
            *outdir = argv[++i];
        } else if (a[0] == '-') {
            fprintf(stderr, "calc: unknown option '%s'\n", a);
            return -1;
        } else if (*input) {
            fprintf(stderr, "calc: too many input files\n");
            return -1;
        } else {
            *input = a;
        }
    }
    if (*dir && *input) {
        fprintf(stderr, "calc: use either -d DIR or one input file, not both\n");
        return -1;
    }
    if (!*dir && !*input) {
        usage();
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *dir = NULL;
    const char *outdir_arg = NULL;
    const char *input = NULL;
    if (parse_args(argc, argv, &dir, &outdir_arg, &input) != 0) {
        return EXIT_FAILURE;
    }

    char *outdir = NULL;
    if (dir) {
        struct stat st;
        if (stat(dir, &st) != 0) {
            fprintf(stderr, "calc: cannot open directory '%s': %s\n", dir, strerror(errno));
            return EXIT_FAILURE;
        }
        if (!S_ISDIR(st.st_mode)) {
            fprintf(stderr, "calc: '%s' is not a directory\n", dir);
            return EXIT_FAILURE;
        }
        if (outdir_arg) {
            outdir = dupstr(outdir_arg);
        } else {
            char *stem = dir_stem(dir);
            outdir = stem ? default_dir_from_stem(stem) : NULL;
            free(stem);
        }
    } else {
        struct stat st;
        if (stat(input, &st) != 0) {
            fprintf(stderr, "calc: cannot open '%s': %s\n", input, strerror(errno));
            return EXIT_FAILURE;
        }
        if (!S_ISREG(st.st_mode)) {
            fprintf(stderr, "calc: '%s' is not a file\n", input);
            return EXIT_FAILURE;
        }
        if (outdir_arg) {
            outdir = dupstr(outdir_arg);
        } else {
            char *stem = file_stem(input);
            outdir = stem ? default_dir_from_stem(stem) : NULL;
            free(stem);
        }
    }
    if (!outdir) {
        fprintf(stderr, "calc: out of memory\n");
        return EXIT_FAILURE;
    }
    if (ensure_dir(outdir) != 0) {
        free(outdir);
        return EXIT_FAILURE;
    }

    int rc = dir ? process_dir(dir, outdir) : process_file(input, outdir);
    free(outdir);
    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
