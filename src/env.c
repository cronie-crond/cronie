/* Copyright 1988,1990,1993,1994 by Paul Vixie
 * All rights reserved
 */

/*
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1997,2000 by Internet Software Consortium, Inc.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "globals.h"
#include "funcs.h"

#if defined(BSD)
extern char **environ;
#endif

char **env_init(void) {
	char **p = (char **) malloc(sizeof (char *));

	if (p != NULL)
		p[0] = NULL;
	return (p);
}

void env_free(char **envp) {
	char **p;

	for (p = envp; *p != NULL; p++)
		free(*p);
	free(envp);
}

char **env_copy(char **envp) {
	int save_errno;
	size_t count, i;
	char **p;

	for (count = 0; envp[count] != NULL; count++) ;

	p = (char **) malloc((count + 1) * sizeof (char *));	/* 1 for the NULL */
	if (p != NULL) {
		for (i = 0; i < count; i++)
			if ((p[i] = strdup(envp[i])) == NULL) {
				save_errno = errno;
				while (i-- > 0)
					free(p[i]);
				free(p);
				errno = save_errno;
				return (NULL);
			}
		p[count] = NULL;
	}
	return (p);
}

char **env_set(char **envp, const char *envstr) {
	size_t count, found;
	char **p, *envtmp;

	/*
	 * count the number of elements, including the null pointer;
	 * also set 'found' to -1 or index of entry if already in here.
	 */
	found = (size_t)-1;
	for (count = 0; envp[count] != NULL; count++) {
		if (!strcmp_until(envp[count], envstr, '='))
			found = count;
	}
	count++;	/* for the NULL */

	if (found != (size_t)-1) {
		/*
		 * it exists already, so just free the existing setting,
		 * save our new one there, and return the existing array.
		 */
		if ((envtmp = strdup(envstr)) == NULL)
			return (NULL);
		free(envp[found]);
		envp[found] = envtmp;
		return (envp);
	}

	/*
	 * it doesn't exist yet, so resize the array, move null pointer over
	 * one, save our string over the old null pointer, and return resized
	 * array.
	 */
	if ((envtmp = strdup(envstr)) == NULL)
		return (NULL);
	p = (char **) realloc((void *) envp,
		(count + 1) * sizeof (char *));
	if (p == NULL) {
		free(envtmp);
		return (NULL);
	}
	p[count] = p[count - 1];
	p[count - 1] = envtmp;
	return (p);
}

int env_set_from_environ(char ***envpp) {
	static const char *names[] = {
		"LANG",
		"LC_CTYPE",
		"LC_NUMERIC",
		"LC_TIME",
		"LC_COLLATE",
		"LC_MONETARY",
		"LC_MESSAGES",
		"LC_PAPER",
		"LC_NAME",
		"LC_ADDRESS",
		"LC_TELEPHONE",
		"LC_MEASUREMENT",
		"LC_IDENTIFICATION",
		"LC_ALL",
		"LANGUAGE",
		"RANDOM_DELAY",
		NULL
	};
	const char **name;
	char **procenv;

	for (procenv = environ; *procenv != NULL; ++procenv) {
		for (name = names; *name != NULL; ++name) {
			size_t namelen;

			namelen = strlen(*name);
			if (strncmp(*name, *procenv, namelen) == 0 
			    && (*procenv)[namelen] == '=') {
				char **tmpenv;

				tmpenv = env_set(*envpp, *procenv);
				if (tmpenv == NULL)
					return FALSE;
				*envpp = tmpenv;
			}
		}
	}
	return TRUE;
}

/* The following states are used by load_env(), traversed in order: */
enum env_state {
	NAMEI,	/* First char of NAME, may be quote */
	NAME,	/* Subsequent chars of NAME */
	EQ1,	/* After end of name, looking for '=' sign */
	EQ2,	/* After '=', skipping whitespace */
	VALUEI,	/* First char of VALUE, may be quote */
	VALUE,	/* Subsequent chars of VALUE */
	FINI,	/* All done, skipping trailing whitespace */
	ERROR,	/* Error */
};

/* return	ERR = end of file
 *		FALSE = not an env setting (file was repositioned)
 *		TRUE = was an env setting
 */
int load_env(char *envstr, FILE * f) {
	long filepos;
	int fileline;
	enum env_state state;
	char name[MAX_ENVSTR], val[MAX_ENVSTR];
	char quotechar, *c, *str;

	filepos = ftell(f);
	fileline = LineNumber;
	skip_comments(f);
	if (EOF == get_string(envstr, MAX_ENVSTR, f, "\n"))
		return (ERR);

	Debug(DPARS, ("load_env, read <%s>\n", envstr));

	memset(name, 0, sizeof name);
	memset(val, 0, sizeof val);

	str = name;
	state = NAMEI;
	quotechar = '\0';
	c = envstr;
	while (state != ERROR && *c) {
		switch (state) {
		case NAMEI:
		case VALUEI:
			if (*c == '\'' || *c == '"')
				quotechar = *c++;
			state++;
			/* FALLTHROUGH */
		case NAME:
		case VALUE:
			if (quotechar) {
				if (*c == quotechar) {
					state++;
					c++;
					break;
				}
				if (state == NAME && *c == '=') {
					state = ERROR;
					break;
				}
			}
			else {
				if (state == NAME) {
					if (isspace((unsigned char) *c)) {
						c++;
						state++;
						break;
					}
					if (*c == '=') {
						state++;
						break;
					}
				}
			}
			*str++ = *c++;
			break;

		case EQ1:
			if (*c == '=') {
				state++;
				str = val;
				quotechar = '\0';
			}
			else {
				if (!isspace((unsigned char) *c))
					state = ERROR;
			}
			c++;
			break;

		case EQ2:
		case FINI:
			if (isspace((unsigned char) *c))
				c++;
			else
				state++;
			break;

		default:
			abort();
		}
	}
	if (state != FINI && state != EQ2 && !(state == VALUE && !quotechar)) {
		Debug(DPARS, ("load_env, not an env var, state = %d\n", state));
			if (fseek(f, filepos, 0)) {
                return ERR;
           }
		Set_LineNum(fileline);
		return (FALSE);
	}
	if (state == VALUE) {
		/* End of unquoted value: trim trailing whitespace */
		c = val + strlen(val);
		while (c > val && isspace((unsigned char) c[-1]))
			*(--c) = '\0';
	}

	/* 2 fields from parser; looks like an env setting */

	/*
	 * This can't overflow because get_string() limited the size of the
	 * name and val fields.  Still, it doesn't hurt to be careful...
	 */
	if (!glue_strings(envstr, MAX_ENVSTR, name, val, '='))
		return (FALSE);
	Debug(DPARS, ("load_env, <%s> <%s> -> <%s>\n", name, val, envstr));
		return (TRUE);
}

char *env_get(const char *name, char **envp) {
	size_t len = strlen(name);
	char *p, *q;

	while ((p = *envp++) != NULL) {
		if (!(q = strchr(p, '=')))
			continue;
		if ((q - p) == len && !strncmp(p, name, len))
			return (q + 1);
	}
	return (NULL);
}

char **env_update_home(char **envp, const char *dir) {
	char envstr[MAX_ENVSTR];

	if (dir == NULL || *dir == '\0' || env_get("HOME", envp)) {
		return envp;
	}

	if (glue_strings(envstr, sizeof envstr, "HOME", dir, '=')) {
		envp = env_set(envp, envstr);
	}			
	else
		log_it("CRON", getpid(), "ERROR", "can't set HOME", 0);

	return envp;
}
