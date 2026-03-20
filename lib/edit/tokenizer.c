/*
 * tokenizer.c - EditLine tokenizer
 *
 * Tokenizes input lines into argc/argv arrays with support for
 * single-quoting, double-quoting, backslash-escaping, and
 * continuation detection for incomplete quotes.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "el.h"

#define TOK_GROW 16

enum tok_state {
	TOK_PLAIN,
	TOK_SQUOTE,
	TOK_DQUOTE,
	TOK_BACKSLASH
};

struct tokenizer {
	char *ifs;
	char **argv;
	int argc;
	int argv_cap;
	char *wbuf;
	size_t wlen;
	size_t wcap;
	enum tok_state state;
	enum tok_state saved_state; /* for backslash in quotes */
};

Tokenizer *tok_init(const char *ifs)
{
	Tokenizer *tok;

	tok = calloc(1, sizeof(Tokenizer));
	if (!tok)
		return NULL;
	tok->ifs = strdup(ifs ? ifs : " \t\n");
	if (!tok->ifs) {
		free(tok);
		return NULL;
	}
	tok->argv_cap = TOK_GROW;
	tok->argv = calloc((size_t)tok->argv_cap, sizeof(char *));
	if (!tok->argv) {
		free(tok->ifs);
		free(tok);
		return NULL;
	}
	tok->wcap = 64;
	tok->wbuf = malloc(tok->wcap);
	if (!tok->wbuf) {
		free(tok->argv);
		free(tok->ifs);
		free(tok);
		return NULL;
	}
	tok->wlen = 0;
	tok->state = TOK_PLAIN;
	return tok;
}

void tok_reset(Tokenizer *tok)
{
	int i;

	if (!tok)
		return;
	for (i = 0; i < tok->argc; i++)
		free(tok->argv[i]);
	tok->argc = 0;
	tok->wlen = 0;
	tok->state = TOK_PLAIN;
}

void tok_end(Tokenizer *tok)
{
	if (!tok)
		return;
	tok_reset(tok);
	free(tok->argv);
	free(tok->wbuf);
	free(tok->ifs);
	free(tok);
}

static int tok_wbuf_add(Tokenizer *tok, char ch)
{
	if (tok->wlen + 1 >= tok->wcap) {
		size_t nc = tok->wcap * 2;
		char *nb = realloc(tok->wbuf, nc);
		if (!nb)
			return -1;
		tok->wbuf = nb;
		tok->wcap = nc;
	}
	tok->wbuf[tok->wlen++] = ch;
	return 0;
}

static int tok_push_word(Tokenizer *tok)
{
	char *word;

	if (tok->wlen == 0)
		return 0;
	if (tok->argc >= tok->argv_cap - 1) {
		int nc = tok->argv_cap + TOK_GROW;
		char **na = realloc(tok->argv, (size_t)nc * sizeof(char *));
		if (!na)
			return -1;
		tok->argv = na;
		tok->argv_cap = nc;
	}
	word = malloc(tok->wlen + 1);
	if (!word)
		return -1;
	memcpy(word, tok->wbuf, tok->wlen);
	word[tok->wlen] = '\0';
	tok->argv[tok->argc++] = word;
	tok->wlen = 0;
	return 0;
}

static int is_ifs(const char *ifs, char ch)
{
	return strchr(ifs, ch) != NULL;
}

int tok_str(Tokenizer *tok, const LineInfo *li, int *argc,
	    const char ***argv)
{
	const char *p;
	const char *end;

	if (!tok || !li || !argc || !argv)
		return -1;

	tok_reset(tok);

	p = li->buffer;
	end = li->lastchar;

	while (p < end) {
		unsigned char ch = (unsigned char)*p++;

		switch (tok->state) {
		case TOK_PLAIN:
			if (ch == '\\') {
				tok->saved_state = TOK_PLAIN;
				tok->state = TOK_BACKSLASH;
			} else if (ch == '\'') {
				tok->state = TOK_SQUOTE;
				/* Empty quotes produce an empty word */
				tok_wbuf_add(tok, '\0');
				tok->wlen--;
			} else if (ch == '"') {
				tok->state = TOK_DQUOTE;
				tok_wbuf_add(tok, '\0');
				tok->wlen--;
			} else if (is_ifs(tok->ifs, (char)ch)) {
				if (tok_push_word(tok) < 0)
					return -1;
			} else {
				tok_wbuf_add(tok, (char)ch);
			}
			break;

		case TOK_SQUOTE:
			if (ch == '\'') {
				tok->state = TOK_PLAIN;
			} else {
				tok_wbuf_add(tok, (char)ch);
			}
			break;

		case TOK_DQUOTE:
			if (ch == '\\') {
				tok->saved_state = TOK_DQUOTE;
				tok->state = TOK_BACKSLASH;
			} else if (ch == '"') {
				tok->state = TOK_PLAIN;
			} else {
				tok_wbuf_add(tok, (char)ch);
			}
			break;

		case TOK_BACKSLASH:
			tok_wbuf_add(tok, (char)ch);
			tok->state = tok->saved_state;
			break;
		}
	}

	/* Handle end-of-input */
	if (tok->state == TOK_SQUOTE || tok->state == TOK_DQUOTE ||
	    tok->state == TOK_BACKSLASH) {
		/* Incomplete quote/escape - continuation needed */
		if (tok->wlen > 0)
			tok_push_word(tok);
		tok->argv[tok->argc] = NULL;
		*argc = tok->argc;
		*argv = (const char **)tok->argv;
		return 1; /* continuation */
	}

	/* Push final word if any */
	if (tok_push_word(tok) < 0)
		return -1;

	tok->argv[tok->argc] = NULL;
	*argc = tok->argc;
	*argv = (const char **)tok->argv;
	return 0;
}
