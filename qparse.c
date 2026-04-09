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

#include "strqtok_r.h"
#include "qparse.h"
/*
 * qparse — tokenise a string into an array of pointers.
 *
 * This function tokenises a string based on the characters passed in.
 * If a string has the following pattern:
 *     The,end,,,is,"very near".
 * The above string will return a token count of 4 and the following in the args array.
 *   args[0] = "The";
 *   args[1] = "end";
 *   args[2] = "is";
 *   args[3] = "very near.";            // removes the quotes from the string.
 *   args[4] = NULL;
 *
 * The quote marks can be either single or double pairs but can not mix them on a token.
 *
 *   str      = string to be tokenised.  This string is modified.
 *   chrs     = characters to parse string by.
 *   argz     = A pointer to an array of char * pointers. This array will contain
 *              the tokens found up to (max_argz - 1).
 *   max_argz = max number of pointers in the pointer array argz.
 *
 *   returns number of tokens parsed which would be less than max_argz.
 *           The return pointer array is null terminated.
 */
int qparse(char *str, const char *chrs, char **argz, int max_argz)
{
    if (!str || !chrs || !argz || max_argz <= 0) {
        return 0;
    }

    char *saveptr;
    char *token;
    int   count = 0;

    /* We reserve the last slot for the NULL terminator */
    int limit = max_argz - 1;

    /* Use strqtok_r for thread safety */
    token = strqtok_r(str, chrs, &saveptr);
    while (token != NULL && count < limit) {
        argz[count++] = token;
        token = strqtok_r(NULL, chrs, &saveptr);
    }

    /* Always NULL terminate the array */
    argz[count] = NULL;

    return count;                                       /* return count */
}
