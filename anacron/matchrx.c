/*
    Anacron - run commands periodically
    Copyright (C) 1998  Itai Tzur <itzur@actcom.co.il>
    Copyright (C) 1999  Sean 'Shaleh' Perry <shaleh@debian.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 
    The GNU General Public License can also be found in the file
    `COPYING' that comes with the Anacron source distribution.
*/


#include <stdio.h>
#include <regex.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "matchrx.h"

int
match_rx(const char *rx, char *string, unsigned int n_sub,  /* char **substrings */...)
/* Return 1 if the regular expression "*rx" matches the string "*string",
 * 0 if not, -1 on error.
 * "Extended" regular expressions are used.
 * Additionally, there should be "n_sub" "substrings" arguments.  These,
 * if not NULL, and if the match succeeds are set to point to the 
 * corresponding substrings of the regexp.
 * The original string is changed, and the substrings must not overlap,
 * or even be directly adjacent.
 * This is not the most efficient, or elegant way of doing this.
 */
{
	int r, n;
	regex_t crx;
	va_list va;
	char **substring;
	regmatch_t *sub_offsets;
	sub_offsets = malloc(sizeof(regmatch_t) * (n_sub + 1));
	if (sub_offsets == NULL)
	    return -1;
	memset(sub_offsets, 0, sizeof(regmatch_t) * (n_sub + 1));

	if (regcomp(&crx, rx, REG_EXTENDED)) {
	    free(sub_offsets);
	    return -1;
	}
	r = regexec(&crx, string, n_sub + 1, sub_offsets, 0);
	if (r != 0 && r != REG_NOMATCH) {
	   free(sub_offsets);
	   return -1;
	}
	regfree(&crx);
	if (r == REG_NOMATCH) {
	    free(sub_offsets);
	    return 0;
	}

	va_start(va, n_sub);
	n = 1;
	while (n <= n_sub)
	{
		substring = va_arg(va, char**);
		if (substring != NULL)
		{
			if (sub_offsets[n].rm_so == -1) {
			    va_end(va);
			    free(sub_offsets);
			    return - 1;
			}
			*substring = string + sub_offsets[n].rm_so;
			*(string + sub_offsets[n].rm_eo) = 0;
		}
		n++;
	}
	va_end(va);
	free(sub_offsets);
	return 1;
}
