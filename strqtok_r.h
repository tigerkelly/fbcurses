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

#ifndef STRQTOK_R_H
#define STRQTOK_R_H

#include <stddef.h>
#include <string.h>

/*
 * strqtok_r - extract successive tokens from a string, allowing quotes (reentrant)
 *
 * strqtok_r works like strtok_r(), except that individual tokens may be
 * surrounded by single or double quotes.
 *
 * strqtok_r considers the string s1 to consist of zero or more text tokens
 * separated by spans of one or more chars from the separator string s2.
 * The first call (with s1 specified) returns a pointer to the first char of
 * the first token, and writes a null char into s1 immediately following the
 * returned token. If the token was surrounded by single or double quotes,
 * they are stripped.
 *
 * The caller must supply a 'saveptr' variable of type (char *). On the first
 * call pass the source string in s1 and any value in *saveptr (it is ignored).
 * On subsequent calls pass NULL for s1; the function resumes from *saveptr.
 * The separator string s2 may be different from call to call.
 *
 * If a quoted token is missing its closing quote the function treats the
 * end-of-string as the closing quote (consuming the rest of the string) and
 * sets *saveptr to NULL so that the next call returns NULL, signalling that
 * no further tokens are available.
 *
 * Returns a pointer to the extracted token, or NULL if no more tokens remain.
 *
 * SPECIAL NOTE: Do not pass a string literal (e.g. char *s = "hello world";)
 * to this function — the compiler places literals in read-only memory and the
 * in-place null-termination will cause a SIGSEGV. Always pass a mutable buffer
 * (e.g. a char array or heap-allocated string).
 */
char *strqtok_r(char *s1, const char *s2, char **saveptr);

/*
 * strqtok - non-reentrant wrapper around strqtok_r for single-threaded use.
 *
 * WARNING: This function is NOT thread-safe. In multi-threaded programs use
 * strqtok_r() directly with a per-thread (or per-parse) saveptr variable.
 */
char *strqtok(char *s1, const char *s2);

#endif /* STRQTOK_R_H */
