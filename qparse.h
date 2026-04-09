/*
 * Copyright (c) 2015 Richard Kelly Wiles (rkwiles@twc.com)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef QPARSE_H
#define QPARSE_H

#include <stddef.h>
#include <string.h>
#include "strqtok_r.h"

/*
 * strqtok_r — re-entrant, quote-aware string tokeniser.
 *
 * Behaves like strtok_r but treats paired single or double quotes as a
 * single token, removing the surrounding quote characters from the result.
 * Quote pairs must match — a token opened with ' must close with '.
 *
 *   s1       — string to tokenise on first call; NULL on subsequent calls.
 *   s2       — delimiter characters (same as strtok_r).
 *   saveptr  — caller-supplied char * used to maintain position between calls.
 *
 *   Returns a pointer to the next token, or NULL when no tokens remain.
 *   The source string is modified in-place (delimiters replaced with '\0').
 */
/*
 * qparse — tokenise a string into an array of pointers.
 *
 * Splits str on any character in chrs, honouring single and double quoted
 * substrings (quotes are stripped from the token).  Consecutive delimiters
 * are treated as one separator (same as strtok behaviour).
 *
 * Example:
 *   Given:  The,end,,,is,"very near".
 *   Result: argz[0]="The"  argz[1]="end"  argz[2]="is"
 *           argz[3]="very near."   argz[4]=NULL
 *   Return value: 4
 *
 *   str      — string to tokenise (modified in-place).
 *   chrs     — delimiter character set, e.g. "\t\n ,".
 *   argz     — output array of char* pointers (null-terminated on return).
 *   max_argz — capacity of argz; at most (max_argz - 1) tokens are stored.
 *
 *   Returns the number of tokens stored (always < max_argz).
 */
int qparse(char *str, const char *chrs, char **argz, int max_argz);

/*
 * strqtok — non-reentrant wrapper around strqtok_r.
 * WARNING: not thread-safe. Use strqtok_r() in multi-threaded code.
 */


#endif /* QPARSE_H */
