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
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>

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
	char quotechar, *c, *str, *val;

	filepos = ftell(f);
	fileline = LineNumber;
	if (EOF == get_string(envstr, MAX_ENVSTR, f, "\n"))
		return (ERR);

	Debug(DPARS, ("load_env, read <%s>\n", envstr));

	str = envstr;
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
				quotechar = '\0';
				*str++ = *c;
				val = str;
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
	*str = '\0';
	if (state == VALUE) {
		/* End of unquoted value: trim trailing whitespace */
		while (str > val && isspace((unsigned char)str[-1]))
			*(--str) = '\0';
	}
	return TRUE;
}

char *env_get(const char *name, char **envp) {
	size_t len = strlen(name);
	char *p, *q;

	while ((p = *envp++) != NULL) {
		if (!(q = strchr(p, '=')))
			continue;
		if ((size_t)(q - p) == len && !strncmp(p, name, len))
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

/* Expand env variables in 'source' arg and save to 'result'*/
void expand_envvar(const char *source, char *result, const int max_size) {
    const char *envvar_p;
    int envvar_name_size = 0;

    *result = '\0';

    while (find_envvar(source, &envvar_p, &envvar_name_size)) {
        char *envvar_name, *envvar_value;
        int prefix_size;
        
        /* Copy content before env var name */
        prefix_size = envvar_p - source;
        
        if (prefix_size > 0) {
            if ((strlen(result) + prefix_size + 1) > max_size) {
                goto too_big;
            }
            strncat(result, source, prefix_size);
        }
        
        /* skip envvar name */
        source = envvar_p + envvar_name_size;
        
        /* copy envvar name, ignoring $, { and } chars*/
        envvar_p++; 
        envvar_name_size--;
      
        if (*envvar_p == '{') {
            envvar_p++;
            envvar_name_size = envvar_name_size - 2;
        }

        envvar_name = malloc(sizeof(char) * (envvar_name_size + 1));
        strncpy(envvar_name, envvar_p, envvar_name_size);
        envvar_name[envvar_name_size] = '\0';

        /* Copy envvar value to result */
        envvar_value = getenv(envvar_name);
        free(envvar_name);
        
        if (envvar_value != NULL) {
            if ((strlen(result) + strlen(envvar_value) + 1) > max_size) {
                goto too_big;
            }
            strncat(result, envvar_value, strlen(envvar_value));
        }
    }

    /* Copy any character left in the source string */
    if (*source != '\0') {
        if ((strlen(result) + strlen(source) + 1) > max_size) {
            goto too_big;
        }
        strncat(result, source, max_size);
    }
    
    return;
    too_big:
        log_it("CRON", getpid(), "ERROR", "Environment variable expansion failed. Expanded value is bigger than allocated memory.", 0);
}

/* Return:
 * 0 - Not found
 * 1 - Found */
int find_envvar(const char *source, const char **start_pos, int *length) {
    const char *reader;
    int size = 1;
    int waiting_close = 0;
    int has_non_digit = 0;
    
    *length = 0;
    *start_pos = NULL;

    if (source == NULL || *source == '\0') {
        return 0;
    }

    *start_pos = strchr(source, '$');

    if (*start_pos == NULL) {
        return 0;
    }
    /* Skip $, since all envvars start with this char */
    reader = *start_pos + 1;
    
    while (*reader != '\0') {
        if (*reader == '_' || isalnum(*reader)) {
            if (size <= 2 && isdigit(*reader)) {
                goto not_found;
            }
            size++;
        } 
        else if (*reader == '{') {
            if (size != 1) {
                goto not_found;
            }
            size++;
            waiting_close = 1;
        } 
        else if (*reader == '}') {
            if ((waiting_close && size == 2) || size == 1) {
                goto not_found;
            }
            
            if (waiting_close) {
                size++;
            }
            
            waiting_close = 0;
            break;
        } 
        else
            break;

        reader++;
    }

    if (waiting_close) {
        goto not_found;
    }

    *length = size;
    return 1;

    not_found:
        *length = 0;
        *start_pos = NULL;
        return 0;
}