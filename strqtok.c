
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

/* Originally written in 05/28/1988, modified to bring it up to coding and format standards. */

#include <string.h>

#define DQUOTE	'"'
#define SQUOTE	'\''

/*
 * strqtok - extract successive tokens from a string, allowing quotes
 *
 * strqtok works just like strtok (), except that individual tokens may
 * be surrounded by single or double quotes.
 *
 * strqtok considers the string s1 to consist of zero or more text tokens
 * separated by spans of one or more chars from the separator string s2.
 * The first call (with s1 specified) returns a pointer to the first
 * char of the first token, and will have written a null char into s1
 * immediately following the returned token. If the token was surrounded
 * by single or double quotes, they are stripped. The function keeps
 * track of its position in the string between separate calls, so that on
 * subsequent calls (which must be made with s1 equal to NULL) will work
 * thru the string s1 until no tokens remain. The separator string s2 may
 * be differnt from call to call. When no token remains in s1, a NULL
 * pointer is returned.
 *
 * Returns a pointer to the extracted token, or NULL if no more tokens
 * remain in the string.
 */
/* SPECIAL NOTE: Do not use a const char * like
 * char *str = "string";
 * when passing data to qparse, this will cause a core dump at the *s1++ = '\0'; line.
 * The string array passed in must be allocated by your program and not the compiler.
 * When you use the above statement in your program the compiler allocates the memory
 * in a protected area, so you get a SIGSEGV error.
 */

char *strqtok (char *s1, const char *s2) {
	static	char	*sp = NULL;	/* keep track of position */
	char	*begin;
	char	q;

	if( s1 == NULL )
		s1 = sp;

	/* skip leading separator chars */

	while( *s1 && strchr( s2, *s1 ) != NULL )
		s1++;

	/* if the token begins with nul, end of string reached so no */
	/* more tokens, return NULL.  */

	if( *s1 == '\0' )
		return (NULL);

	/* if the token begins with single or double quotes, search that */

	if( *s1 == SQUOTE || *s1 == DQUOTE ) {
		q = *s1++;
		begin = s1;
		while( *s1 && *s1 != q )
			s1++;
	} else {			/* else look for next separator char */
		begin = s1;
		while( *s1 && strchr( s2, *s1 ) == NULL )
			s1++;
	}

	/* terminate token. */

	if( *s1 )
	 	*s1++ = '\0';

	/* save position for next call */

	sp = s1;

	/* return pointer to beginning of token */

	return (begin);
}
