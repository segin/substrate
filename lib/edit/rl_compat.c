/*
 * rl_compat.c - GNU Readline compatibility shim for libedit
 *
 * Provides the readline(3) and add_history(3) interfaces on top of
 * the native EditLine API so that applications written for libreadline
 * can use libedit with minimal changes.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <histedit.h>
#include "el.h"

/* ------------------------------------------------------------------ */
/* Global readline-compatible variables                               */
/* ------------------------------------------------------------------ */

char *rl_line_buffer;
int rl_point;
int rl_end;
const char *rl_readline_name = "editline";
char **(*rl_attempted_completion_function)(const char *, int, int);

/* ------------------------------------------------------------------ */
/* Internal singleton state                                           */
/* ------------------------------------------------------------------ */

static EditLine *rl_el;
static History  *rl_hist;
static const char *rl_cur_prompt;

static void rl_cleanup(void)
{
	if (rl_hist) { history_end(rl_hist); rl_hist = NULL; }
	if (rl_el)   { el_end(rl_el);       rl_el = NULL;   }
}

static void rl_init(void)
{
	HistEvent ev;

	if (rl_el)
		return;
	rl_hist = history_init();
	if (rl_hist)
		history(rl_hist, &ev, H_SETSIZE, 100);
	rl_el = el_init(rl_readline_name ? rl_readline_name : "editline",
			stdin, stdout, stderr);
	if (rl_el && rl_hist)
		el_set(rl_el, EL_HIST, history, rl_hist);
	if (rl_el)
		el_set(rl_el, EL_SIGNAL, 1);
	atexit(rl_cleanup);
}

/* ------------------------------------------------------------------ */
/* readline() / add_history()                                         */
/* ------------------------------------------------------------------ */

char *readline(const char *prompt)
{
	const char *line;
	int count;
	char *result;
	size_t len;

	rl_init();
	if (!rl_el)
		return NULL;

	rl_cur_prompt = prompt;
	el_set(rl_el, EL_PROMPT, prompt ? prompt : "");

	line = el_gets(rl_el, &count);
	if (!line || count <= 0)
		return NULL;

	/* Strip trailing newline */
	len = strlen(line);
	if (len > 0 && line[len - 1] == '\n')
		len--;
	result = malloc(len + 1);
	if (!result)
		return NULL;
	memcpy(result, line, len);
	result[len] = '\0';

	/* Update global variables */
	rl_line_buffer = result;
	rl_point = (int)len;
	rl_end = (int)len;

	return result;
}

void add_history(const char *line)
{
	HistEvent ev;

	rl_init();
	if (rl_hist && line)
		history(rl_hist, &ev, H_ENTER, line);
}

/* ------------------------------------------------------------------ */
/* Binding / prompt / display                                         */
/* ------------------------------------------------------------------ */

int rl_bind_key(int key, void (*func)(void))
{
	char seq[4];

	rl_init();
	if (!rl_el)
		return -1;

	(void)func; /* Can't directly map readline func to editline action */
	seq[0] = (char)key;
	seq[1] = '\0';
	/* Best-effort: bind to self-insert as a stub */
	el_set(rl_el, EL_BIND, seq, "ed-insert", NULL);
	return 0;
}

void rl_set_prompt(const char *prompt)
{
	rl_init();
	if (rl_el)
		el_set(rl_el, EL_PROMPT, prompt ? prompt : "");
}

void rl_on_new_line(void)
{
	/* Hint to redraw on next el_gets */
}

void rl_forced_update_display(void)
{
	rl_init();
	if (rl_el)
		el_set(rl_el, EL_REFRESH);
}
