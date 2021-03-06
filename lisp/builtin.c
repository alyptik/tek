#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include "builtin.h"
#include "error.h"
#include "eval.h"
#include "util.h"

/*
 * Verifies that `v` is a well-formed function and returns a new
 * function value built from it. `v->car` is the list of parameters
 * and `v->cdr` is the body of the function.
 */

static struct value *
make_function(struct value *env, struct value *v, int type)
{
	assert(type == VAL_FUNCTION || type == VAL_MACRO);

	if (!IS_LIST(v->car) || !IS_LIST(v->cdr))
		error(v->loc, "malformed function definition");

	for (struct value *p = v->car;
	     p->type == VAL_CELL;
	     p = p->cdr) {
		if (p->car->type == VAL_SYMBOL) continue;
		return error(p->car->loc,
		             "parameter name must be a symbol"
		             " (this is %s %s)",
		             IS_VOWEL(*TYPE_NAME(p->car->type))
		             ? "an" : "a",
		             TYPE_NAME(p->car->type));
	}

	struct value *r = new_value(v->cdr->loc);

	r->type  = type;
	r->param = v->car;
	r->body  = v->cdr;
	r->env   = env;

	return r;
}

/*
 * Builds a function. Builds both named and anonymous functions.
 */

struct value *
builtin_fn(struct value *env, struct value *v)
{
	if (v->cdr->type != VAL_CELL)
		return error(v->loc, "missing list of parameters");

	/*
	 * If `v->car->type != VAL_SYMBOL` then this is an anonymous
	 * function.
	 */

	if (v->car->type != VAL_SYMBOL)
		return make_function(env, v, VAL_FUNCTION);

	/*
	 * Otherwise it's obviously a named function which should be
	 * added to the environment as a variable.
	 */

	return add_variable(env,
	                    v->car,
	                    make_function(env, v->cdr, VAL_FUNCTION));
}

/*
 * Evaluates each expression in `list` and prints it to stdout.
 */

struct value *
builtin_print(struct value *env, struct value *list)
{
	struct value *r = eval_list(env, list);
	if (r->type == VAL_ERROR) return r;
	for (struct value *p = r; p->type != VAL_NIL; p = p->cdr) {
		struct value *e = print_value(stdout, p->car);
		if (e->type == VAL_ERROR) return e;
	}
	return Nil(list->loc);
}

struct value *
builtin_set(struct value *env, struct value *list)
{
	/* if (list_length(list) != 2 || list->car->type != TSYMBOL) */
	/* 	error("Malformed setq"); */
	struct value *sym = eval(env, list->car);
	struct value *bind = find(env, sym);

	if (!bind) {
		struct value *value = eval(env, list->cdr->car);
		add_variable(env, sym, value);
		return value;
	}

	struct value *value = eval(env, list->cdr->car);
	bind->cdr = value;

	return value;
}

#define ARITHMETIC(X)	  \
	int sum = 0, first = 1; \
	for (struct value *args = eval_list(env, list); \
	     args->type != VAL_NIL; \
	     args = args->cdr) { \
		if (args->type == VAL_ERROR) return args; \
		if (args->car->type == VAL_INT) { \
			if (first) { \
				sum = args->car->i; \
				first = 0; \
			} \
			else sum X##= args->car->i; \
			continue; \
		} \
		return error(args->car->loc, \
		             "builtin `"#X"' takes only " \
		             "numeric arguments (got `%s')", \
		             TYPE_NAME(args->car->type)); \
	} \
	struct value *r = new_value(list->loc); \
	r->type = VAL_INT; \
	r->i = sum; \
	return r; \

struct value *
builtin_add(struct value *env, struct value *list)
{
	ARITHMETIC(+);
}

struct value *
builtin_sub(struct value *env, struct value *list)
{
	ARITHMETIC(-);
}

struct value *
builtin_mul(struct value *env, struct value *list)
{
	ARITHMETIC(*);
}

struct value *
builtin_div(struct value *env, struct value *list)
{
	ARITHMETIC(/);
}

struct value *
builtin_eq(struct value *env, struct value *list)
{
	int sum = 0, first = 1;

	for (struct value *args = eval_list(env, list);
	     args->type != VAL_NIL;
	     args = args->cdr) {
		if (args->type == VAL_ERROR) return args;

		if (args->car->type == VAL_INT) {
			if (first) sum = args->car->i, first = 0;
			else if (args->car->i != sum)
				return Nil(list->loc);
			continue;
		}

		return error(args->car->loc,
		             "builtin `=' takes only numeric"
		             " arguments (got `%s')",
		             TYPE_NAME(args->car->type));
	}

	return True(list->loc);
}

struct value *
builtin_less(struct value *env, struct value *v)
{
	int sum = 0, first = 1;

	for (struct value *args = eval_list(env, v);
	     args->type != VAL_NIL;
	     args = args->cdr) {
		if (args->type == VAL_ERROR) return args;

		if (args->car->type == VAL_INT) {
			if (first) sum = args->car->i, first = 0;
			else if (args->car->i >= sum)
				return Nil(v->loc);
			continue;
		}

		return error(args->car->loc,
		             "builtin `<' takes only numeric"
		             " arguments (got `%s')",
		             TYPE_NAME(args->car->type));
	}

	return True(v->loc);
}

struct value *
builtin_if(struct value *env, struct value *list)
{
	if (eval(env, list->car)->type == VAL_TRUE)
		return eval(env, list->cdr->car);

	/* Otherwise do the else branches. */
	return progn(env, list->cdr->cdr);
}

struct value *
builtin_quote(struct value *env, struct value *v)
{
	(void)env;
	return v->car;
}

struct value *
builtin_setq(struct value *env, struct value *list)
{
	return builtin_set(env, cons(quote(list->car), list->cdr));
}

struct value *
builtin_println(struct value *env, struct value *v)
{
	struct value *r = builtin_print(env, v);
	return putchar('\n'), r;
}

struct value *
builtin_cons(struct value *env, struct value *v)
{
	return cons(eval(env, v->car), eval(env, v->cdr->car));
}

struct value *
builtin_car(struct value *env, struct value *v)
{
	return eval(env, v->car)->car;
}

struct value *
builtin_cdr(struct value *env, struct value *v)
{
	return eval(env, v->car)->cdr;
}

struct value *
builtin_macro(struct value *env, struct value *v)
{
	return make_function(env, v, VAL_MACRO);
}

struct value *
builtin_progn(struct value *env, struct value *v)
{
	return progn(env, v);
}

struct value *
builtin_while(struct value *env, struct value *v)
{
	struct value *c, *r;

	while ((c = eval(env, v->car)) && c->type == VAL_TRUE)
		r = progn(env, v->cdr);

	return r;
}

void
load_builtins(struct value *env)
{
	add_builtin(env, "progn",   builtin_progn);
	add_builtin(env, "macro",   builtin_macro);
	add_builtin(env, "println", builtin_println);
	add_builtin(env, "print",   builtin_print);
	add_builtin(env, "while",   builtin_while);
	add_builtin(env, "quote",   builtin_quote);
	add_builtin(env, "cons",    builtin_cons);
	add_builtin(env, "setq",    builtin_setq);
	add_builtin(env, "set",     builtin_set);
	add_builtin(env, "car",     builtin_car);
	add_builtin(env, "cdr",     builtin_cdr);
	add_builtin(env, "fn",      builtin_fn);
	add_builtin(env, "if",      builtin_if);
	add_builtin(env, "+",       builtin_add);
	add_builtin(env, "-",       builtin_sub);
	add_builtin(env, "*",       builtin_mul);
	add_builtin(env, "/",       builtin_div);
	add_builtin(env, "=",       builtin_eq);
	add_builtin(env, "<",       builtin_less);
}
