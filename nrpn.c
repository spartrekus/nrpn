
/*
    nrpn:
    pwd ; gcc -lm -lncurses nrpn.c -o nrpn 
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <ncurses.h>

#define CELLSTRMAX 1024
char nmemory[61][PATH_MAX];
char ncell[52][32][1024];
char ncell_clip[52][1024];
int ncell_colorscheme[52];
char clipboard[1024];
int pile = 1;
int pilemax = 1;
//int mode = 1;
int mode = 2;
int nrpn_show_results = 0;
int nrpn_show_memory = 0; 
int nrpn_text_bold = 0;


#ifndef __TINYEXPR_H__
#define __TINYEXPR_H__


#ifdef __cplusplus
extern "C" {
#endif



typedef struct te_expr {
    int type;
    union {double value; const double *bound; const void *function;};
    void *parameters[1];
} te_expr;


enum {
    TE_VARIABLE = 0,

    TE_FUNCTION0 = 8, TE_FUNCTION1, TE_FUNCTION2, TE_FUNCTION3,
    TE_FUNCTION4, TE_FUNCTION5, TE_FUNCTION6, TE_FUNCTION7,

    TE_CLOSURE0 = 16, TE_CLOSURE1, TE_CLOSURE2, TE_CLOSURE3,
    TE_CLOSURE4, TE_CLOSURE5, TE_CLOSURE6, TE_CLOSURE7,

    TE_FLAG_PURE = 32
};

typedef struct te_variable {
    const char *name;
    const void *address;
    int type;
    void *context;
} te_variable;



/* Parses the input expression, evaluates it, and frees it. */
/* Returns NaN on error. */
double te_interp(const char *expression, int *error);

/* Parses the input expression and binds variables. */
/* Returns NULL on error. */
te_expr *te_compile(const char *expression, const te_variable *variables, int var_count, int *error);

/* Evaluates the expression. */
double te_eval(const te_expr *n);

/* Prints debugging information on the syntax tree. */
void te_print(const te_expr *n);

/* Frees the expression. */
/* This is safe to call on NULL pointers. */
void te_free(te_expr *n);


#ifdef __cplusplus
}
#endif

#endif /*__TINYEXPR_H__*/

#include <stdio.h>
#include <math.h>




int rows, cols;


//#include "te.c"
/*
 * TINYEXPR - Tiny recursive descent parser and evaluation engine in C
 *
 * Copyright (c) 2015, 2016 Lewis Van Winkle
 *
 * http://CodePlea.com
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software
 * in a product, an acknowledgement in the product documentation would be
 * appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

/* COMPILE TIME OPTIONS */

/* Exponentiation associativity:
For a^b^c = (a^b)^c and -a^b = (-a)^b do nothing.
For a^b^c = a^(b^c) and -a^b = -(a^b) uncomment the next line.*/
/* #define TE_POW_FROM_RIGHT */

/* Logarithms
For log = base 10 log do nothing
For log = natural log uncomment the next line. */
/* #define TE_NAT_LOG */

//#include "tinyexpr.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#ifndef NAN
#define NAN (0.0/0.0)
#endif

#ifndef INFINITY
#define INFINITY (1.0/0.0)
#endif


typedef double (*te_fun2)(double, double);

enum {
    TOK_NULL = TE_CLOSURE7+1, TOK_ERROR, TOK_END, TOK_SEP,
    TOK_OPEN, TOK_CLOSE, TOK_NUMBER, TOK_VARIABLE, TOK_INFIX
};


enum {TE_CONSTANT = 1};


typedef struct state {
    const char *start;
    const char *next;
    int type;
    union {double value; const double *bound; const void *function;};
    void *context;

    const te_variable *lookup;
    int lookup_len;
} state;


#define TYPE_MASK(TYPE) ((TYPE)&0x0000001F)

#define IS_PURE(TYPE) (((TYPE) & TE_FLAG_PURE) != 0)
#define IS_FUNCTION(TYPE) (((TYPE) & TE_FUNCTION0) != 0)
#define IS_CLOSURE(TYPE) (((TYPE) & TE_CLOSURE0) != 0)
#define ARITY(TYPE) ( ((TYPE) & (TE_FUNCTION0 | TE_CLOSURE0)) ? ((TYPE) & 0x00000007) : 0 )
#define NEW_EXPR(type, ...) new_expr((type), (const te_expr*[]){__VA_ARGS__})

static te_expr *new_expr(const int type, const te_expr *parameters[]) {
    const int arity = ARITY(type);
    const int psize = sizeof(void*) * arity;
    const int size = (sizeof(te_expr) - sizeof(void*)) + psize + (IS_CLOSURE(type) ? sizeof(void*) : 0);
    te_expr *ret = malloc(size);
    memset(ret, 0, size);
    if (arity && parameters) {
        memcpy(ret->parameters, parameters, psize);
    }
    ret->type = type;
    ret->bound = 0;
    return ret;
}


void te_free_parameters(te_expr *n) {
    if (!n) return;
    switch (TYPE_MASK(n->type)) {
        case TE_FUNCTION7: case TE_CLOSURE7: te_free(n->parameters[6]);
        case TE_FUNCTION6: case TE_CLOSURE6: te_free(n->parameters[5]);
        case TE_FUNCTION5: case TE_CLOSURE5: te_free(n->parameters[4]);
        case TE_FUNCTION4: case TE_CLOSURE4: te_free(n->parameters[3]);
        case TE_FUNCTION3: case TE_CLOSURE3: te_free(n->parameters[2]);
        case TE_FUNCTION2: case TE_CLOSURE2: te_free(n->parameters[1]);
        case TE_FUNCTION1: case TE_CLOSURE1: te_free(n->parameters[0]);
    }
}


void te_free(te_expr *n) {
    if (!n) return;
    te_free_parameters(n);
    free(n);
}


static double pi() {return 3.14159265358979323846;}
static double e() {return 2.71828182845904523536;}
static double fac(double a) {/* simplest version of fac */
    if (a < 0.0)
        return NAN;
    if (a > UINT_MAX)
        return INFINITY;
    unsigned int ua = (unsigned int)(a);
    unsigned long int result = 1, i;
    for (i = 1; i <= ua; i++) {
        if (i > ULONG_MAX / result)
            return INFINITY;
        result *= i;
    }
    return (double)result;
}
static double ncr(double n, double r) {
    if (n < 0.0 || r < 0.0 || n < r) return NAN;
    if (n > UINT_MAX || r > UINT_MAX) return INFINITY;
    unsigned long int un = (unsigned int)(n), ur = (unsigned int)(r), i;
    unsigned long int result = 1;
    if (ur > un / 2) ur = un - ur;
    for (i = 1; i <= ur; i++) {
        if (result > ULONG_MAX / (un - ur + i))
            return INFINITY;
        result *= un - ur + i;
        result /= i;
    }
    return result;
}
static double npr(double n, double r) {return ncr(n, r) * fac(r);}

static const te_variable functions[] = {
    /* must be in alphabetical order */
    {"abs", fabs,     TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"acos", acos,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"asin", asin,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"atan", atan,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"atan2", atan2,  TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"ceil", ceil,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"cos", cos,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"cosh", cosh,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"e", e,          TE_FUNCTION0 | TE_FLAG_PURE, 0},
    {"exp", exp,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"fac", fac,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"floor", floor,  TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"ln", log,       TE_FUNCTION1 | TE_FLAG_PURE, 0},
#ifdef TE_NAT_LOG
    {"log", log,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
#else
    {"log", log10,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
#endif
    {"log10", log10,  TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"ncr", ncr,      TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"npr", npr,      TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"pi", pi,        TE_FUNCTION0 | TE_FLAG_PURE, 0},
    {"pow", pow,      TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"sin", sin,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"sinh", sinh,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"sqrt", sqrt,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"tan", tan,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"tanh", tanh,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {0, 0, 0, 0}
};

static const te_variable *find_builtin(const char *name, int len) {
    int imin = 0;
    int imax = sizeof(functions) / sizeof(te_variable) - 2;

    /*Binary search.*/
    while (imax >= imin) {
        const int i = (imin + ((imax-imin)/2));
        int c = strncmp(name, functions[i].name, len);
        if (!c) c = '\0' - functions[i].name[len];
        if (c == 0) {
            return functions + i;
        } else if (c > 0) {
            imin = i + 1;
        } else {
            imax = i - 1;
        }
    }

    return 0;
}

static const te_variable *find_lookup(const state *s, const char *name, int len) {
    int iters;
    const te_variable *var;
    if (!s->lookup) return 0;

    for (var = s->lookup, iters = s->lookup_len; iters; ++var, --iters) {
        if (strncmp(name, var->name, len) == 0 && var->name[len] == '\0') {
            return var;
        }
    }
    return 0;
}



static double add(double a, double b) {return a + b;}
static double sub(double a, double b) {return a - b;}
static double mul(double a, double b) {return a * b;}
static double divide(double a, double b) {return a / b;}
static double negate(double a) {return -a;}
static double comma(double a, double b) {(void)a; return b;}


void next_token(state *s) {
    s->type = TOK_NULL;

    do {

        if (!*s->next){
            s->type = TOK_END;
            return;
        }

        /* Try reading a number. */
        if ((s->next[0] >= '0' && s->next[0] <= '9') || s->next[0] == '.') {
            s->value = strtod(s->next, (char**)&s->next);
            s->type = TOK_NUMBER;
        } else {
            /* Look for a variable or builtin function call. */
            if (s->next[0] >= 'a' && s->next[0] <= 'z') {
                const char *start;
                start = s->next;
                while ((s->next[0] >= 'a' && s->next[0] <= 'z') || (s->next[0] >= '0' && s->next[0] <= '9') || (s->next[0] == '_')) s->next++;

                const te_variable *var = find_lookup(s, start, s->next - start);
                if (!var) var = find_builtin(start, s->next - start);

                if (!var) {
                    s->type = TOK_ERROR;
                } else {
                    switch(TYPE_MASK(var->type))
                    {
                        case TE_VARIABLE:
                            s->type = TOK_VARIABLE;
                            s->bound = var->address;
                            break;

                        case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:
                        case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
                            s->context = var->context;

                        case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
                        case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
                            s->type = var->type;
                            s->function = var->address;
                            break;
                    }
                }

            } else {
                /* Look for an operator or special character. */
                switch (s->next++[0]) {
                    case '+': s->type = TOK_INFIX; s->function = add; break;
                    case '-': s->type = TOK_INFIX; s->function = sub; break;
                    case '*': s->type = TOK_INFIX; s->function = mul; break;
                    case '/': s->type = TOK_INFIX; s->function = divide; break;
                    case '^': s->type = TOK_INFIX; s->function = pow; break;
                    case '%': s->type = TOK_INFIX; s->function = fmod; break;
                    case '(': s->type = TOK_OPEN; break;
                    case ')': s->type = TOK_CLOSE; break;
                    case ',': s->type = TOK_SEP; break;
                    case ' ': case '\t': case '\n': case '\r': break;
                    default: s->type = TOK_ERROR; break;
                }
            }
        }
    } while (s->type == TOK_NULL);
}


static te_expr *list(state *s);
static te_expr *expr(state *s);
static te_expr *power(state *s);

static te_expr *base(state *s) {
    /* <base>      =    <constant> | <variable> | <function-0> {"(" ")"} | <function-1> <power> | <function-X> "(" <expr> {"," <expr>} ")" | "(" <list> ")" */
    te_expr *ret;
    int arity;

    switch (TYPE_MASK(s->type)) {
        case TOK_NUMBER:
            ret = new_expr(TE_CONSTANT, 0);
            ret->value = s->value;
            next_token(s);
            break;

        case TOK_VARIABLE:
            ret = new_expr(TE_VARIABLE, 0);
            ret->bound = s->bound;
            next_token(s);
            break;

        case TE_FUNCTION0:
        case TE_CLOSURE0:
            ret = new_expr(s->type, 0);
            ret->function = s->function;
            if (IS_CLOSURE(s->type)) ret->parameters[0] = s->context;
            next_token(s);
            if (s->type == TOK_OPEN) {
                next_token(s);
                if (s->type != TOK_CLOSE) {
                    s->type = TOK_ERROR;
                } else {
                    next_token(s);
                }
            }
            break;

        case TE_FUNCTION1:
        case TE_CLOSURE1:
            ret = new_expr(s->type, 0);
            ret->function = s->function;
            if (IS_CLOSURE(s->type)) ret->parameters[1] = s->context;
            next_token(s);
            ret->parameters[0] = power(s);
            break;

        case TE_FUNCTION2: case TE_FUNCTION3: case TE_FUNCTION4:
        case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
        case TE_CLOSURE2: case TE_CLOSURE3: case TE_CLOSURE4:
        case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
            arity = ARITY(s->type);

            ret = new_expr(s->type, 0);
            ret->function = s->function;
            if (IS_CLOSURE(s->type)) ret->parameters[arity] = s->context;
            next_token(s);

            if (s->type != TOK_OPEN) {
                s->type = TOK_ERROR;
            } else {
                int i;
                for(i = 0; i < arity; i++) {
                    next_token(s);
                    ret->parameters[i] = expr(s);
                    if(s->type != TOK_SEP) {
                        break;
                    }
                }
                if(s->type != TOK_CLOSE || i != arity - 1) {
                    s->type = TOK_ERROR;
                } else {
                    next_token(s);
                }
            }

            break;

        case TOK_OPEN:
            next_token(s);
            ret = list(s);
            if (s->type != TOK_CLOSE) {
                s->type = TOK_ERROR;
            } else {
                next_token(s);
            }
            break;

        default:
            ret = new_expr(0, 0);
            s->type = TOK_ERROR;
            ret->value = NAN;
            break;
    }

    return ret;
}





static te_expr *power(state *s) {
    /* <power>     =    {("-" | "+")} <base> */
    int sign = 1;
    while (s->type == TOK_INFIX && (s->function == add || s->function == sub)) {
        if (s->function == sub) sign = -sign;
        next_token(s);
    }

    te_expr *ret;

    if (sign == 1) {
        ret = base(s);
    } else {
        ret = NEW_EXPR(TE_FUNCTION1 | TE_FLAG_PURE, base(s));
        ret->function = negate;
    }

    return ret;
}

#ifdef TE_POW_FROM_RIGHT
static te_expr *factor(state *s) {
    /* <factor>    =    <power> {"^" <power>} */
    te_expr *ret = power(s);

    int neg = 0;
    te_expr *insertion = 0;

    if (ret->type == (TE_FUNCTION1 | TE_FLAG_PURE) && ret->function == negate) {
        te_expr *se = ret->parameters[0];
        free(ret);
        ret = se;
        neg = 1;
    }

    while (s->type == TOK_INFIX && (s->function == pow)) {
        te_fun2 t = s->function;
        next_token(s);

        if (insertion) {
            /* Make exponentiation go right-to-left. */
            te_expr *insert = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, insertion->parameters[1], power(s));
            insert->function = t;
            insertion->parameters[1] = insert;
            insertion = insert;
        } else {
            ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, power(s));
            ret->function = t;
            insertion = ret;
        }
    }

    if (neg) {
        ret = NEW_EXPR(TE_FUNCTION1 | TE_FLAG_PURE, ret);
        ret->function = negate;
    }

    return ret;
}
#else
static te_expr *factor(state *s) {
    /* <factor>    =    <power> {"^" <power>} */
    te_expr *ret = power(s);

    while (s->type == TOK_INFIX && (s->function == pow)) {
        te_fun2 t = s->function;
        next_token(s);
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, power(s));
        ret->function = t;
    }

    return ret;
}
#endif



static te_expr *term(state *s) {
    /* <term>      =    <factor> {("*" | "/" | "%") <factor>} */
    te_expr *ret = factor(s);

    while (s->type == TOK_INFIX && (s->function == mul || s->function == divide || s->function == fmod)) {
        te_fun2 t = s->function;
        next_token(s);
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, factor(s));
        ret->function = t;
    }

    return ret;
}


static te_expr *expr(state *s) {
    /* <expr>      =    <term> {("+" | "-") <term>} */
    te_expr *ret = term(s);

    while (s->type == TOK_INFIX && (s->function == add || s->function == sub)) {
        te_fun2 t = s->function;
        next_token(s);
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, term(s));
        ret->function = t;
    }

    return ret;
}


static te_expr *list(state *s) {
    /* <list>      =    <expr> {"," <expr>} */
    te_expr *ret = expr(s);

    while (s->type == TOK_SEP) {
        next_token(s);
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, expr(s));
        ret->function = comma;
    }

    return ret;
}


#define TE_FUN(...) ((double(*)(__VA_ARGS__))n->function)
#define M(e) te_eval(n->parameters[e])


double te_eval(const te_expr *n) {
    if (!n) return NAN;

    switch(TYPE_MASK(n->type)) {
        case TE_CONSTANT: return n->value;
        case TE_VARIABLE: return *n->bound;

        case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
        case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
            switch(ARITY(n->type)) {
                case 0: return TE_FUN(void)();
                case 1: return TE_FUN(double)(M(0));
                case 2: return TE_FUN(double, double)(M(0), M(1));
                case 3: return TE_FUN(double, double, double)(M(0), M(1), M(2));
                case 4: return TE_FUN(double, double, double, double)(M(0), M(1), M(2), M(3));
                case 5: return TE_FUN(double, double, double, double, double)(M(0), M(1), M(2), M(3), M(4));
                case 6: return TE_FUN(double, double, double, double, double, double)(M(0), M(1), M(2), M(3), M(4), M(5));
                case 7: return TE_FUN(double, double, double, double, double, double, double)(M(0), M(1), M(2), M(3), M(4), M(5), M(6));
                default: return NAN;
            }

        case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:
        case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
            switch(ARITY(n->type)) {
                case 0: return TE_FUN(void*)(n->parameters[0]);
                case 1: return TE_FUN(void*, double)(n->parameters[1], M(0));
                case 2: return TE_FUN(void*, double, double)(n->parameters[2], M(0), M(1));
                case 3: return TE_FUN(void*, double, double, double)(n->parameters[3], M(0), M(1), M(2));
                case 4: return TE_FUN(void*, double, double, double, double)(n->parameters[4], M(0), M(1), M(2), M(3));
                case 5: return TE_FUN(void*, double, double, double, double, double)(n->parameters[5], M(0), M(1), M(2), M(3), M(4));
                case 6: return TE_FUN(void*, double, double, double, double, double, double)(n->parameters[6], M(0), M(1), M(2), M(3), M(4), M(5));
                case 7: return TE_FUN(void*, double, double, double, double, double, double, double)(n->parameters[7], M(0), M(1), M(2), M(3), M(4), M(5), M(6));
                default: return NAN;
            }

        default: return NAN;
    }

}

#undef TE_FUN
#undef M

static void optimize(te_expr *n) {
    /* Evaluates as much as possible. */
    if (n->type == TE_CONSTANT) return;
    if (n->type == TE_VARIABLE) return;

    /* Only optimize out functions flagged as pure. */
    if (IS_PURE(n->type)) {
        const int arity = ARITY(n->type);
        int known = 1;
        int i;
        for (i = 0; i < arity; ++i) {
            optimize(n->parameters[i]);
            if (((te_expr*)(n->parameters[i]))->type != TE_CONSTANT) {
                known = 0;
            }
        }
        if (known) {
            const double value = te_eval(n);
            te_free_parameters(n);
            n->type = TE_CONSTANT;
            n->value = value;
        }
    }
}


te_expr *te_compile(const char *expression, const te_variable *variables, int var_count, int *error) {
    state s;
    s.start = s.next = expression;
    s.lookup = variables;
    s.lookup_len = var_count;

    next_token(&s);
    te_expr *root = list(&s);

    if (s.type != TOK_END) {
        te_free(root);
        if (error) {
            *error = (s.next - s.start);
            if (*error == 0) *error = 1;
        }
        return 0;
    } else {
        optimize(root);
        if (error) *error = 0;
        return root;
    }
}





double te_interp(const char *expression, int *error) {
    te_expr *n = te_compile(expression, 0, 0, error);
    double ret;
    if (n) {
        ret = te_eval(n);
        te_free(n);
    } else {
        ret = NAN;
    }
    return ret;
}

static void pn (const te_expr *n, int depth) {
    int i, arity;
    printf("%*s", depth, "");

    switch(TYPE_MASK(n->type)) {
    case TE_CONSTANT: printf("%f\n", n->value); break;
    case TE_VARIABLE: printf("bound %p\n", n->bound); break;

    case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
    case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
    case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:
    case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
         arity = ARITY(n->type);
         printf("f%d", arity);
         for(i = 0; i < arity; i++) {
             printf(" %p", n->parameters[i]);
         }
         printf("\n");
         for(i = 0; i < arity; i++) {
             pn(n->parameters[i], depth + 1);
         }
         break;
    }
}


void te_print(const te_expr *n) {
    pn(n, 0);
}



#ifdef _WIN32
/// WIN
#include <windows.h>
int termsize()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int columns, rows;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 0;
    printf("OS: Windows \n" );
    printf("columns: %d\n", columns);
    printf("rows: %d\n", rows);
}
#else
/// LINUX
#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>
int termsize()
{
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    printf("OS: Linux \n" );
    printf ("lines %d\n", w.ws_row);
    printf ("columns %d\n", w.ws_col);
}
#endif




char *strinterpreter();


/////////////////////////////////
/////////////////////////////////
/////////////////////////////////
char *strinterpreter(char *str)
{  
      char ptr[ PATH_MAX ];  /// to have enough space
      char fooline[PATH_MAX];
      int i,j=0;  int toxi = 0; 
      int fonmem ; 
      int fonmemln ; 
      for(i=0; str[i]!='\0'; i++)
      {

        if ( str[i] == '$' ) 
        {
          if ( str[i+1] == '[' ) 
          if ( str[i+3] == ']' ) 
	  {  
            fonmem = str[i+2]-49+1;
            strncpy( fooline, strinterpreter( ncell[fonmem][1] ) , PATH_MAX );
            for(toxi=0; fooline[toxi]!='\0'; toxi++)
               ptr[j++]=fooline[toxi];
            i++;
            i++;
            i++;
            i++;
	  }
	}

        if ( str[i] == '$' ) 
        {
          if ( str[i+1] == '[' ) 
          if ( str[i+3] == ',' ) 
          if ( str[i+5] == ']' ) 
	  {  
            fonmem =   str[i+2]-49+1;
            fonmemln = str[i+4]-49+1;
            strncpy( fooline, strinterpreter( ncell[fonmem][fonmemln] ) , PATH_MAX );
            for(toxi=0; fooline[toxi]!='\0'; toxi++)
               ptr[j++]=fooline[toxi];
            i++; i++; i++; i++;
            i++; i++; 
	  }
	}


        if ( str[i] == '$' ) 
        {
          if (( str[i+1] >= 'A' ) && ( str[i+1] <= 'Z' ) )
	  {  
            fonmem = str[i+1]-65+1;
            for(toxi=0; nmemory[fonmem][toxi]!='\0'; toxi++)
               ptr[j++]=nmemory[fonmem][toxi];
            i++;
	  }
          if (( str[i+1] >= 'a' ) && ( str[i+1] <= 'z' ) )
	  {  
            fonmem = str[i+1]-97+1;
            for(toxi=0; nmemory[fonmem][toxi]!='\0'; toxi++)
               ptr[j++]=nmemory[fonmem][toxi];
            i++;
	  }
	}

        else
          ptr[j++]=str[i];

      } 
      ptr[j]='\0';
      size_t siz = 1 + sizeof ptr ; 
      char *r = malloc( 1 +  sizeof ptr );
      return r ? memcpy(r, ptr, siz ) : NULL;
}



/////////////////////////////////
/////////////////////////////////
/////////////////////////////////
char *strconvtxt(char *str)
{  
      char ptr[ 5* strlen(str)+1];
      int i,j=0;
      for(i=0; str[i]!='\0'; i++)
      {

        if ( str[i] == '|' ) 
	{
          ptr[j++]=' ';
          ptr[j++]='+';
          ptr[j++]=' ';
	}

        //else if ( str[i] == ',' ) 
	//{
        //  ptr[j++]='.';
	//}

        else if ( str[i] == ';' ) 
	{
          ptr[j++]=' ';
          ptr[j++]='+';
          ptr[j++]=' ';
	}

        else
	{
          ptr[j++]=str[i];
	}
      } 
      ptr[j]='\0';
      size_t siz = 1 + sizeof ptr ; 
      char *r = malloc( 1 +  sizeof ptr );
      return r ? memcpy(r, ptr, siz ) : NULL;
}










////////////////////////////////////////////////////////////////////
char *strrlf(char *str) 
{     // copyleft, C function made by Spartrekus 
      char ptr[strlen(str)+1];
      int i,j=0;
      for(i=0; str[i]!='\0'; i++)
      {
        if (str[i] != '\n' && str[i] != '\n') 
        ptr[j++]=str[i];
      } 
      ptr[j]='\0';
      size_t siz = sizeof ptr ; 
      char *r = malloc( sizeof ptr );
      return r ? memcpy(r, ptr, siz ) : NULL;
}

////////////////////////////////////////////////////////////////////
char *strcut( char *str , int myposstart, int myposend )
{     // copyleft, C function made by Spartrekus 
      char ptr[strlen(str)+1];
      int i,j=0;
      for(i=0; str[i]!='\0'; i++)
      {
        if ( ( str[i] != '\0' ) && ( str[i] != '\0') )
         if ( ( i >=  myposstart-1 ) && (  i <= myposend-1 ) )
           ptr[j++]=str[i];
      } 
      ptr[j]='\0';
      size_t siz = sizeof ptr ; 
      char *r = malloc( sizeof ptr );
      return r ? memcpy(r, ptr, siz ) : NULL;
}


void piles_copy( ) 
{
   int i ; 
   for( i = 1 ; i <= 50 ; i++ )
   {
     strncpy( ncell_clip[i], ncell[i][1], CELLSTRMAX );
   }
}

void piles_paste( ) 
{
   int i ; 
   for( i = 1 ; i <= 50 ; i++ )
     strncpy( ncell[i][1], ncell_clip[i] , CELLSTRMAX );
}


void ins_pile( int pile1 ) 
{
  int fooi = 1; 
  int topile = pile1 +1;
  for( fooi = 11 ; fooi >= topile ; fooi--)   
     strncpy( ncell[fooi][1], ncell[fooi-1][1],  CELLSTRMAX );
  strncpy( ncell[ topile ][1], "" ,  CELLSTRMAX );
}


void nrpn_help( ) 
{
   int i = 0; erase();
   mvprintw( i++, 0, "|NRPN, By Spartrekus (GNU)|" );
   mvprintw( i++, 0, "F1: This Help" );
   mvprintw( i++, 0, "F2: Save Stacks to Buffer" );
   mvprintw( i++, 0, "F3: Load Stacks from Buffer" );
   mvprintw( i++, 0, "F5: Copy Current Stack to Clipboard" );
   mvprintw( i++, 0, "F6: Paste Current Stack from Clipboard" );
   mvprintw( i++, 0, "F7: Insert Stack" );
   mvprintw( i++, 0, "F8: Delete Stack" );
   mvprintw( i++, 0, "F9: Settings" );
   mvprintw( i++, 0, "Tab: Edit/Calc with +,-,..." );
   mvprintw( i++, 0, "Up/Down: Change stack ");
   mvprintw( i++, 0, "b: Turn Reverse Current Stack  (in visual mode)");
   mvprintw( i++, 0, "hjkl: Moving    (in visual mode)");
   mvprintw( i++, 0, "y,p: Copy/Paste (in visual mode)");
   mvprintw( i++, 0, " " );
   mvprintw( i++, 0, "(PLEASE READ *IMPORTANT*: This Software is in development with no warranty, it may have bugs, no support, possible errors, ...)" );
   getch();
}

void suppr_pile( int pile1 ) 
{
  int fooi = 1; 
  strncpy( ncell[pile1][1], "" ,  CELLSTRMAX );
  for( fooi = 1 ; fooi <= 10 ; fooi++)   
      strncpy( ncell[ pile1 + fooi-1  ][1], ncell[ pile1 + fooi ][1] ,  CELLSTRMAX );
}






void calc_pile( int pile2, int pile1 , int modecalc ) 
{
  int fooi = 1; 
  char foocharo[CELLSTRMAX]; strncpy( foocharo, "", CELLSTRMAX );
  fooi = snprintf( foocharo , CELLSTRMAX , "%s %c %s", ncell[pile2][1] , modecalc , ncell[ pile1 ][1] );
  if ( strcmp( ncell[ pile1 ][1], "" ) != 0 ) 
    strncpy( ncell[pile2][1],  foocharo ,  CELLSTRMAX );
  // ranger piles
  strncpy( ncell[pile1][1], "" ,  CELLSTRMAX );
  for( fooi = 1 ; fooi <= 10 ; fooi++)   
      strncpy( ncell[ pile1 + fooi-1  ][1], ncell[ pile1 + fooi ][1] ,  CELLSTRMAX );
}




////////////////////////
////////////////////////
char *strninput( char *myinitstring )
{

   int strninput_gameover = 0; 
   char strmsg[PATH_MAX];
   char charotmp[PATH_MAX];
   strncpy( strmsg, myinitstring , PATH_MAX );

   int ch ;  int foo ; 
   int fooj; 
   while ( strninput_gameover == 0 )
   {
                  getmaxyx( stdscr, rows, cols);
                  attroff( A_REVERSE );
                  for ( fooj = 0 ; fooj <= cols-1;  fooj++)
                  {
                    mvaddch( rows-1, fooj , ' ' );
                  }
                  mvprintw( rows-1, 0, ":> %s" , strrlf( strmsg ) );
                  attron( A_REVERSE );
                  printw( " " );
                  attroff( A_REVERSE );
                  attroff( A_REVERSE );
                  attroff( A_BOLD );
                  refresh() ; 

                  curs_set( 0 );
                  ch = getch();

		  if ( ( ch == KEY_BACKSPACE ) || ( ch == 4  ) )
		  {
			 strncpy( strmsg, strcut( strmsg, 1 , strlen( strmsg ) -1 )  ,  PATH_MAX );
		  }

	          else if (
			(( ch >= 'a' ) && ( ch <= 'z' ) ) 
		        || (( ch >= 'A' ) && ( ch <= 'Z' ) ) 
		        || (( ch >= '1' ) && ( ch <= '9' ) ) 
		        || (( ch == '0' ) ) 
		        || (( ch == '~' ) ) 
		        || (( ch == '!' ) ) 
		        || (( ch == '&' ) ) 
		        || (( ch == '=' ) ) 
		        || (( ch == ':' ) ) 
		        || (( ch == ';' ) ) 
		        || (( ch == '<' ) ) 
		        || (( ch == '>' ) ) 
		        || (( ch == ' ' ) ) 
		        || (( ch == '|' ) ) 
		        || (( ch == '#' ) ) 
		        || (( ch == '?' ) ) 
		        || (( ch == '+' ) ) 
		        || (( ch == '/' ) ) 
		        || (( ch == '\\' ) ) 
		        || (( ch == '.' ) ) 
		        || (( ch == '$' ) ) 
		        || (( ch == '%' ) ) 
		        || (( ch == '-' ) ) 
		        || (( ch == ',' ) ) 
		        || (( ch == '{' ) ) 
		        || (( ch == '}' ) ) 
		        || (( ch == '(' ) ) 
		        || (( ch == ')' ) ) 
		        || (( ch == ']' ) ) 
		        || (( ch == '[' ) ) 
		        || (( ch == '*' ) ) 
		        || (( ch == '"' ) ) 
		        || (( ch == '@' ) ) 
		        || (( ch == '-' ) ) 
		        || (( ch == '_' ) ) 
		        || (( ch == '^' ) ) 
		        || (( ch == '\'' ) ) 
	             ) 
		  {
                           foo = snprintf( charotmp, PATH_MAX , "%s%c",  strmsg, ch );
			   strncpy( strmsg,  charotmp ,  PATH_MAX );
		  }
		  else if ( ch == 10 ) 
		  {
                        strninput_gameover = 1;
		  }
     }
     char ptr[PATH_MAX];
     strncpy( ptr, strmsg, PATH_MAX );
     size_t siz = sizeof ptr ; 
     char *r = malloc( sizeof ptr );
     return r ? memcpy(r, ptr, siz ) : NULL;
}



void proc_sto_memory()
{
     int ch ; int foo; char charo[PATH_MAX]; int foopile ;  
                          attron( A_REVERSE ); mvprintw( rows-1, 0, "STO (Press Key)?" ); attroff( A_REVERSE );
                          ch = getch();
                          if ( ( ch >= 'A' ) && ( ch <= 'Z' ) )
                          {
                             foopile = ch -65+1;
                             attron( A_REVERSE ); mvprintw( rows-2, 0, "[STO] SET %c Variable:", ch ); attroff( A_REVERSE );
                             foo = snprintf( charo, PATH_MAX , "%s", strninput(  nmemory[foopile]   ));
                             strncpy( nmemory[ foopile ], charo , PATH_MAX );
                          }
                          if ( ( ch >= 'a' ) && ( ch <= 'z' ) )
                          {
                             foopile = ch -97+1;
                             attron( A_REVERSE ); mvprintw( rows-2, 0, "[STO] SET %c Variable:", ch ); attroff( A_REVERSE );
                             foo = snprintf( charo, PATH_MAX , "%s", strninput( nmemory[foopile] ));
                             strncpy( nmemory[ foopile ], charo , PATH_MAX );
                          }
}








void proc_nrpn_spreadsheet()
{
   int j, i;  int tableselx = 1; int tablesely = 1;
   char charo[PATH_MAX]; int foo ;  int foopile; int ch = 0 ; char spcharo[PATH_MAX]; int rruni ; int rrunj; 
   foo = 1;  tableselx = 1;  tablesely = 1; int spreadsheet_gameover = 0;  char cellval[PATH_MAX]; int toss;
                          while( spreadsheet_gameover == 0 )
                          {
                           erase();
                           mvprintw( 0,0, "|NRPN (GNU)|Spartrekus|");
                           for( rruni = 1 ; rruni <= rows-1 ; rruni++ )
                           for( rrunj = 1 ; rrunj <= 10 ; rrunj++ )
                           {
                            attroff(A_REVERSE);
                            if ( rrunj == tableselx ) if ( rruni == tablesely ) attron(A_REVERSE);

                             if ( ncell[rruni][rrunj][0] == '\'' )
                             {
                               if ( strcmp( cellval, "" ) == 0 )     strncpy( cellval , "_", PATH_MAX );
                               strncpy( cellval , ncell[rruni][rrunj] , PATH_MAX );
                               mvprintw(rruni, 10*rrunj -8 , "%s", strcut( cellval, 2, strlen( cellval) ));
                             }
                             else 
                             {
                               toss = snprintf( cellval , PATH_MAX , "%f", te_interp( strinterpreter( ncell[rruni][rrunj]), 0 ));
                               if ( strcmp( cellval , "nan" ) == 0 )  strncpy( cellval , "?", PATH_MAX );
                               else if ( strcmp( cellval, "" ) == 0 )     strncpy( cellval , "_", PATH_MAX );
                               if ( strcmp(ncell[rruni][rrunj] , "" ) == 0 ) strncpy( cellval , "_", PATH_MAX );
                               mvprintw(rruni, 10*rrunj -8 , "%s",   strcut( cellval , 1 , 8 ) );
                             }
                           }
                           mvprintw(rows-1, 0, "|CELL #R%d,C%d = %s|", tablesely, tableselx, 
                           strcut( ncell[tablesely][tableselx] , 1 , cols - 4 ) );

                           ch = getch();
                           if      ( ch == 27 )   spreadsheet_gameover = 1;
                           else if ( ch == 'i' )  spreadsheet_gameover = 1; 
                           else if ( ch == 'm' )  proc_sto_memory();
                           else if ( ch == '#' )  spreadsheet_gameover = 1; 
                           else if ( ch == KEY_DOWN )  tablesely++;
                           else if ( ch == KEY_UP )  tablesely--;
                           else if ( ch == KEY_LEFT )  tableselx--;
                           else if ( ch == KEY_RIGHT )  tableselx++;
                           else if ( ch == 'j' )  tablesely++;
                           else if ( ch == 'k' )  tablesely--;
                           else if ( ch == 'h' )  tableselx--;
                           else if ( ch == 'l' )  tableselx++;
                           else if ( ch == 'y' )  strncpy( clipboard, ncell[tablesely][tableselx], CELLSTRMAX );
                           else if ( ch == 'p' )  strncpy( ncell[tablesely][tableselx], clipboard , CELLSTRMAX );
                           else if ( ch == 'x' )  
                           {
                              strncpy( clipboard, ncell[tablesely][tableselx], CELLSTRMAX );
                              strncpy( ncell[tablesely][tableselx], "" , CELLSTRMAX );
                           }
                           else if ( ch == '\'' )  
                           {
                             attron( A_REVERSE ); mvprintw( rows-2, 0, "[SET #R%dC%d CELL (STRING)]", tablesely, tableselx ); attroff( A_REVERSE );
                             foo = snprintf( spcharo, PATH_MAX , "'%s", strninput( ncell[tablesely][tableselx] ));
                             strncpy( ncell[tablesely][tableselx], strrlf( spcharo ) , CELLSTRMAX );
                           }
                           else if ( ch == 10 )  
                           {
                             attron( A_REVERSE ); mvprintw( rows-2, 0, "[SET #R%dC%d CELL]", tablesely, tableselx ); attroff( A_REVERSE );
                             foo = snprintf( spcharo, PATH_MAX , "%s", strninput( ncell[tablesely][tableselx] ));
                             strncpy( ncell[tablesely][tableselx], strrlf( spcharo ) , CELLSTRMAX );
                           }
                          }
}




void proc_show_memory( int stposy, int stposx)
{
       int i;  int y = stposy ; int x = stposx;
       mvprintw( y , x, "** MEMORY **");
       for( i = 1 ; i <= 26 ; i++ )
       mvprintw( y+i , x, "%c (#%d): %s", i+64 , i, nmemory[i] );
}



///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
int main( int argc, char *argv[])
{
   termsize();

   strncpy( clipboard, "" , CELLSTRMAX );
   char cwd[PATH_MAX];
   int j, i ;  
   char charo[PATH_MAX]; 
   int foo ;  int foopile;


   for( i = 1 ; i <= 60 ; i++ )
   {
     foo = snprintf( charo, PATH_MAX , "%d", i );
     strncpy( nmemory[i], charo , PATH_MAX );
   }

   // another column 
   for( i = 1 ; i <= 50 ; i++ )
   for( j = 2 ; j <= 35 ; j++ )
   {
     //foo = snprintf( charo, PATH_MAX , "%d", i );
     //strncpy( ncell[i][j], charo , CELLSTRMAX );
     strncpy( ncell[i][j], "" , CELLSTRMAX );
   }

   // clean
   for( i = 1 ; i <= 50 ; i++ )
   {
     strncpy( ncell[i][1], "", CELLSTRMAX );
     strncpy( ncell_clip[i], "", CELLSTRMAX );
     ncell_colorscheme[i] = 1;
   }


   initscr();	
   curs_set( 0 );
   noecho();            
   keypad( stdscr, TRUE );  // for F...
   start_color();

   init_pair(0, COLOR_WHITE, COLOR_BLACK);
   init_pair(1, COLOR_RED, COLOR_BLACK);
   init_pair(2, COLOR_GREEN, COLOR_BLACK);
   init_pair(3, COLOR_BLUE, COLOR_BLACK);
   init_pair(4, COLOR_YELLOW, COLOR_BLACK);
   init_pair(5, COLOR_CYAN, COLOR_BLACK);
   color_set( 2, NULL );

   int gameover; 
   gameover = 0;
   int ch = 0; 
   char foostring[PATH_MAX]; 
   char strmsg[PATH_MAX];
   char strresult[PATH_MAX];
   strncpy( strmsg, "", PATH_MAX );
   strncpy( strresult, "", PATH_MAX );

   while ( gameover == 0)
   {
           getmaxyx( stdscr, rows, cols);
           attroff( A_REVERSE ) ; 
           color_set( 4, NULL ); 
           attroff( A_REVERSE);

           if ( nrpn_text_bold == 1 )
             attron( A_BOLD);
           else 
             attroff( A_BOLD);
           erase();
           mvprintw( 0,0, "|NRPN (GNU)|Spartrekus|");


           for( i = 1 ; i <= 10 ; i++ )
           {
             if ( ncell_colorscheme[i] == 1 )
                 attroff( A_REVERSE ); 
             else if ( ncell_colorscheme[i] == 2 )
                 attron( A_REVERSE ); 

             mvprintw( rows-4 - i , 2 , "" );

             if ( pile == i ) 
                printw( ">" );
             else
                printw( " " );

              if ( nrpn_show_results == 0 ) 
                   printw( "%d: %s", i, ncell[i][1] );
              else if ( nrpn_show_results == 1 ) 
              {
                   foo = snprintf( charo, PATH_MAX , "%f", te_interp( strinterpreter( strconvtxt( ncell[ i ][1] )) , 0));
       	           strncpy( strresult, charo, PATH_MAX );
                   if ( strcmp( strresult, "" ) == 0 ) 
                      printw( "%d: %s", i, ncell[i][1]);
                   else if ( strcmp( strresult, "nan" ) == 0 ) 
                      printw( "%d: %s", i, ncell[i][1]);
                   else 
                      printw( "%d: %s = %s", i, ncell[i][1], strresult );
              }
           }
           attroff( A_REVERSE ); 

           // status memory
           if ( nrpn_show_memory == 1 ) proc_show_memory(0,cols/2); 

           /// status
           if ( mode == 1 )  mvprintw(  1, 0, "$: %s" , strconvtxt( strrlf( strmsg ) ) );


           // Interpreter line 
           foo = snprintf( charo, PATH_MAX , " Interpreter: %s", strinterpreter( strconvtxt( ncell[pile][1])));

	   strncpy( strresult, charo, PATH_MAX );
           mvprintw( rows-2, 2, "%s", strresult );


           // Result line 
           foo = snprintf( charo, PATH_MAX , " Result: %f", te_interp( strinterpreter( strconvtxt( ncell[pile][1]) ) , 0));

	   strncpy( strresult, charo, PATH_MAX );
           mvprintw( rows-1, 2, "%s", strresult );


           // Key
           mvprintw( rows-1,cols-2, "%d", ch );
	   refresh(); // <- not necessary
           ch = getch();


		  if ( ch == 27  ) 
		  {
                        gameover = 1; 
		  }

    else if ( ch == KEY_UP )
    {
        pile++;
        strncpy( strmsg, ncell[ pile ][1] ,  PATH_MAX );
    }

    else if ( ch == KEY_DOWN )
    {
        pile--;
        strncpy( strmsg, ncell[ pile ][1] ,  PATH_MAX );
    }

		  else if ( ( ch == KEY_BACKSPACE ) || ( ch == 4  ) )
		  {
			 strncpy( strmsg, strcut( strmsg, 1 , strlen( strmsg ) -1 )  ,  PATH_MAX );
			 strncpy( strresult, ""  ,  PATH_MAX );
		  }

                  else if ( ch == 5 )
		  {
                           foo = snprintf( charo, PATH_MAX , "%s%c",  strmsg, ' ' );
			   strncpy( strmsg,  charo ,  PATH_MAX );
			   strncpy( strresult, ""  ,  PATH_MAX );
		  }



             else if ( ch ==  KEY_LEFT )
    {
    }

    else if ( ch ==  KEY_RIGHT )
    {
    }


		  else if ( ch == 4  ) 
		  {
			   strncpy( strmsg, ""  ,  PATH_MAX );
			   strncpy( strresult, ""  ,  PATH_MAX );
		  }

		  else if ( ch == 2  ) 
		  {
			   strncpy( strmsg, ""  ,  PATH_MAX );
			   strncpy( strresult, ""  ,  PATH_MAX );
		  }


		  else if ( ch == 27  ) 
		  {
			   strncpy( strmsg, ""  ,  PATH_MAX );
			   strncpy( strresult, ""  ,  PATH_MAX );
		  }



	          else if ( ( mode == 1 ) &&  
                        (
			(( ch >= 'a' ) && ( ch <= 'z' ) ) 
		        || (( ch >= 'A' ) && ( ch <= 'Z' ) ) 
		        || (( ch >= '1' ) && ( ch <= '9' ) ) 
		        || (( ch == '0' ) ) 
		        || (( ch == '~' ) ) 
		        || (( ch == '!' ) ) 
		        || (( ch == '&' ) ) 
		        || (( ch == '=' ) ) 
		        || (( ch == ':' ) ) 
		        || (( ch == ';' ) ) 
		        || (( ch == '<' ) ) 
		        || (( ch == '>' ) ) 
		        || (( ch == ' ' ) ) 
		        || (( ch == '|' ) ) 
		        || (( ch == '#' ) ) 
		        || (( ch == '?' ) ) 
		        || (( ch == '+' ) ) 
		        || (( ch == '/' ) ) 
		        || (( ch == '\\' ) ) 
		        || (( ch == '.' ) ) 
		        || (( ch == '$' ) ) 
		        || (( ch == '%' ) ) 
		        || (( ch == '-' ) ) 
		        || (( ch == '{' ) ) 
		        || (( ch == '}' ) ) 
		        || (( ch == ',' ) ) 
		        || (( ch == ';' ) ) 
		        || (( ch == '.' ) ) 
		        || (( ch == '(' ) ) 
		        || (( ch == ')' ) ) 
		        || (( ch == ']' ) ) 
		        || (( ch == '[' ) ) 
		        || (( ch == '*' ) ) 
		        || (( ch == '"' ) ) 
		        || (( ch == '@' ) ) 
		        || (( ch == '-' ) ) 
		        || (( ch == '_' ) ) 
		        || (( ch == '^' ) ) 
		        || (( ch == '\'' ) ) 
                   )
	          ) 
		  {
                           foo = snprintf( charo, PATH_MAX , "%s%c",  strmsg, ch );
			   strncpy( strmsg,  charo ,  PATH_MAX );
			   strncpy( strresult, ""  ,  PATH_MAX );
		  }


		 else if ( (mode == 2 ) && ( ch == 10 ) )
                 {
		      strncpy( foostring,  ncell[ pile ][1]  ,  PATH_MAX );
		      strncpy( strmsg ,  strninput( foostring ) ,  PATH_MAX );
                      strncpy( ncell[ pile ][1] , strmsg, CELLSTRMAX );
		      strncpy( strmsg, "" ,  PATH_MAX );
                      pile++;
                      if ( pile >= pilemax ) pilemax++;
                 }

		  else if ( ch == 9 ) 
		  {
                     if ( mode == 1 ) mode = 2;
                     else mode = 1;
                  }

		  
	          else if (( mode == 1 ) && ( ch == 10 ))
		  {
                      strncpy( ncell[ pile ][1] , strmsg, CELLSTRMAX );
		      strncpy( strmsg, "" ,  PATH_MAX );
                      pile++;
                      if ( pile >= pilemax ) pilemax++;
                  }


		  else if ( ch == KEY_F(7) ) 
                       ins_pile( pile );
		  else if ( ch == KEY_F(8) ) 
                       suppr_pile( pile );

		  else if ( ch == KEY_F(1) ) 
                     nrpn_help();

		  else if ( ch == KEY_F(2) ) 
                    piles_copy();
		  else if ( ch == KEY_F(3) ) 
                    piles_paste();

		  else if ( ch == KEY_F(6) ) 
                       strncpy( ncell[ pile ][1], clipboard , CELLSTRMAX );
		  else if ( ch == KEY_F(5) ) 
                       strncpy( clipboard, ncell[ pile ][1] , CELLSTRMAX );

		  else if ( ch == 16 )  //ctrl+p
		  {
                       strncpy( ncell[ pile ][1], clipboard , CELLSTRMAX );
                  }
		  else if ( ch == 25 ) //ctrl+y
		  {
                       strncpy( clipboard, ncell[ pile ][1] , CELLSTRMAX );
                  }

	         else if (( mode == 2 ) && ( ch == '+' ))
                     calc_pile( pile, pile+1, '+' ); 
	         else if (( mode == 2 ) && ( ch == '-' ))
                     calc_pile( pile, pile+1, '-' ); 
	         else if (( mode == 2 ) && ( ch == '*' ))
                     calc_pile( pile, pile+1, '*' ); 
	         else if (( mode == 2 ) && ( ch == '/' ))
                     calc_pile( pile, pile+1, '/' ); 


		 else if ( ch == '=' ) 
                 {
                     if ( nrpn_show_results == 1 )
                       nrpn_show_results = 0; 
                     else 
                      nrpn_show_results = 1;
                 }

		 else if ( ch == KEY_F(9) ) 
                 {
                       erase(); 
                       mvprintw( 0,0, "** Settings **" );
                       mvprintw( 1,0, "--------------");
                       mvprintw( 2,0, "1: Color (bold)"); 
                       mvprintw( 3,0, "2: Show Results" ); 
                       mvprintw( 4,0, "3: Show Memories (Variables)" ); 
                       mvprintw( 5,0, "4: Quick View Memories (Variables)" ); 
                       mvprintw( 6,0, "5: Quick Spreadsheet Table" ); 

                       ch = getch();

                       if ( ch ==  '1' )
                       {
                         if ( nrpn_text_bold == 1 )
                           nrpn_text_bold = 0; 
                         else 
                           nrpn_text_bold = 1;
                       }

                       else if ( ch ==  '2' )
                       {
                         if ( nrpn_show_results == 1 )
                           nrpn_show_results = 0; 
                         else 
                            nrpn_show_results = 1;
                       }

                       else if ( ch ==  '4' )
                       {
                          erase();
                          for( i = 1 ; i <= 60 ; i++ )
                              mvprintw( i , 0, "%d/%d: %s", i, i+64, nmemory[i] );
                          getch();
                       }

                       else if ( ch ==  '5' )
                       {  
                           proc_nrpn_spreadsheet();
                       }

                       else if ( ch == '3' )
                       {
                         if ( nrpn_show_memory == 1 )
                            nrpn_show_memory = 0; 
                         else 
                            nrpn_show_memory = 1;
                       }
                 }



    else if ( ( mode == 2 ) && ( ch == 'b' ) )
    {
       if ( ncell_colorscheme[pile] == 1 )
       ncell_colorscheme[pile] = 2 ; 
       else
       ncell_colorscheme[pile] = 1 ; 
    }

    else if ( ( mode == 2 ) && ( ch == 'y' ) )
          strncpy( clipboard, ncell[ pile ][1] , CELLSTRMAX );

    else if ( ( mode == 2 ) && ( ch == 'x' ) )
    {
          strncpy( clipboard, ncell[ pile ][1] , CELLSTRMAX );
          strncpy( ncell[ pile ][1], "" , CELLSTRMAX );
    }

    else if ( ( mode == 2 ) && ( ch == 'p' ) )
            strncpy( ncell[ pile ][1], clipboard , CELLSTRMAX );

    else if ( ( mode == 2 ) && ( ch == 'k' ) )
    {
        pile++;
        strncpy( strmsg, ncell[ pile ][1] ,  PATH_MAX );
    }

    else if ( ( mode == 2 ) && ( ch == 'j' ) )
    {
        pile--;
        strncpy( strmsg, ncell[ pile ][1] ,  PATH_MAX );
    }

    else if ( ( mode == 2 ) && ( ch == '?' ) )
           nrpn_help();


                       else if ( ( mode == 2 ) && ( ch ==  'm' ) )
                       {
                         proc_sto_memory();
                       }

                       else if ( ( mode == 2 ) && ( ch ==  '#' ) )
                          proc_nrpn_spreadsheet();


                      /*
                       else if ( ( mode == 2 ) && ( ch ==  '#' ) )
                       {
                          erase();
                          proc_show_memory(0,0);
                          ch = getch();
                          if ( ( ch >= 'A' ) && ( ch <= 'Z' ) )
                          {
                             foopile = ch -65+1;
                             foo = snprintf( charo, PATH_MAX , "%s", strninput(  nmemory[foopile]   ));
                             strncpy( nmemory[ foopile ], charo , PATH_MAX );
                             erase();
                             proc_show_memory(0,0); getch();
                          }
                          if ( ( ch >= 'a' ) && ( ch <= 'z' ) )
                          {
                             foopile = ch -97+1;
                             foo = snprintf( charo, PATH_MAX , "%s", strninput( nmemory[foopile] ));
                             strncpy( nmemory[ foopile ], charo , PATH_MAX );
                             erase();
                             proc_show_memory(0,0); getch();
                          }
                         }
                         */


   }


   curs_set( 1 );
   endwin();		
   return 0;
}



