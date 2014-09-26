/*
Copyright (C) 2013, Basis Technology Corp.
Copyright (C) 2007-2011, Golden G. Richard III and Vico Marziale.
Copyright (C) 2005-2007, Golden G. Richard III.
*
Written by Golden G. Richard III and Vico Marziale.
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
*
http://www.apache.org/licenses/LICENSE-2.0
*
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
Thanks to Kris Kendall, Jesse Kornblum, et al for their work
on Foremost. Foremost 0.69 was used as the starting point for
Scalpel, in 2005.
*/

#include "scalpel.h"

// get # of seconds between two specified times
#if defined(_WIN32)
inline double elapsed(LARGE_INTEGER A, LARGE_INTEGER B) {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return fabs(((double)A.QuadPart - (double)B.QuadPart) /
        (double)freq.QuadPart);
}
#else
double elapsed(struct timeval A, struct timeval B) {
    return fabs(A.tv_sec - B.tv_sec) + fabs(A.tv_usec - B.tv_usec) / 1000000;
}
#endif

// determine if a string begins and ends with '/' characters
int isRegularExpression(char *s) {
    return (s && s[0] && s[0] == '/' && s[strlen(s) - 1] == '/');
}


void checkMemoryAllocation(struct scalpelState *state, 
                           void *ptr, int line,
                           const char *file, const char *structure) 
{
    if(ptr) {
        return;
    }
    else {
        fprintf(stderr, "** MEMORY ALLOCATION FAILURE **\n");
        fprintf(stderr,
            "ERROR: Memory exhausted at line %d in file %s. Scalpel was \n",
            line, file);
        fprintf(stderr,
            "allocating memory for %s when this condition occurred.\n",
            structure);

        fprintf(state->auditFile,
            "ERROR: Memory exhausted at line %d in file %s. Scalpel was \n",
            line, file);
        fprintf(state->auditFile,
            "allocating memory for %s when this condition occurred.\n",
            structure);

        handleError(state, SCALPEL_GENERAL_ABORT);	// fatal
        return;
    }
}

// write entry to both the screen and the audit file (if possible)
void scalpelLog(struct scalpelState *state, const char *format, ...) 
{
    va_list argp;

    va_start(argp, format);
    vfprintf(stderr, format, argp);
    va_end(argp);

    va_start(argp, format);
    if(state->auditFile) {
        vfprintf(state->auditFile, format, argp);
    }
    va_end(argp);
}

// determine if two characters match, with optional case 
// insensitivity.  If a is the Scalpel wildcard character,
// then a and b will always match.
int charactersMatch(char a, char b, int caseSensitive) 
{
    if(a == wildcard || a == b) {
        return 1;
    }
    if(caseSensitive || (a < 'A' || a > 'z' || b < 'A' || b > 'z')) {
        return 0;
    }

    /* This line is equivalent to (abs(a-b)) == 'a' - 'A' */
    return (abs(a - b) == 32);
}


// memwildcardcmp is a memcmp() clone, except that single
// character wildcards are supported.  The default wildcard character is '?',
// but this can be redefined in the configuration file.  A wildcard in s1 will 
// match any single character in s2.  
int memwildcardcmp(const void *s1, const void *s2, size_t n, int caseSensitive) 
{
    if(n != 0) {
        register const unsigned char *p1 = (const unsigned char *)s1,
            *p2 = (const unsigned char *)s2;
        do {
            if(!charactersMatch(*p1++, *p2++, caseSensitive)) {
                return (*--p1 - *--p2);
            }
        }
        while (--n != 0);
    }
    return 0;
}


// initialize Boyer-Moore "jump table" for search. Dependence
// on search type (e.g., FORWARD, REVERSE, etc.) from Foremost 
// has been removed, because Scalpel always performs searches across
// a buffer in a forward direction.
void
init_bm_table(char *needle, size_t table[UCHAR_MAX + 1],
	      size_t len, int casesensitive) 
{
    size_t i = 0, j = 0, currentindex = 0;

    for(i = 0; i <= UCHAR_MAX; i++) {
        table[i] = len;
    }

    for(i = 0; i < len; i++) {
        currentindex = len - i - 1;	//Count from the back of string
        //No skip entry can advance us past the last wildcard in the string
        if(needle[i] == wildcard) {
            for(j = 0; j <= UCHAR_MAX; j++) {
                table[j] = currentindex;
            }
        }
        table[(unsigned char)needle[i]] = currentindex;
        if(!casesensitive && needle[i] > 0) {
            table[tolower(needle[i])] = currentindex;
            table[toupper(needle[i])] = currentindex;
        }
    }
}

#ifdef USE_SIMPLE_STRING_SEARCH

// Perform a simple string search, supporting wildcards,
// case-insensitive searches, and specifiable start locations in the buffer.
// The parameter 'table' is ignored.
char *bm_needleinhaystack_skipnchars(char *needle, size_t needle_len,
				     char *haystack, size_t haystack_len,
				     size_t table[UCHAR_MAX + 1],
				     int casesensitive, int start_pos) {

    register int i, j, go;

    if(needle_len == 0) {
        return haystack;
    }

    for(i = start_pos; i < haystack_len; i++) {
        go = 1;
        j = 0;
        while (go) {
            go = (i + j) < haystack_len && j < needle_len &&
                charactersMatch(needle[j], haystack[i + j], casesensitive);
            j++;
        }

        if(j > needle_len) {
            return haystack + i;
        }
    }

    return NULL;
}

#endif



#ifdef USE_FAST_STRING_SEARCH

// Perform a modified Boyer-Moore string search, supporting wildcards,
// case-insensitive searches, and specifiable start locations in the buffer.
// Dependence on search type (e.g., FORWARD, REVERSe, etc.) from Foremost has 
// been removed, because Scalpel always performs forward searching.
char *bm_needleinhaystack_skipnchars(char *needle, size_t needle_len,
				     char *haystack, size_t haystack_len,
				     size_t table[UCHAR_MAX + 1],
				     int casesensitive, int start_pos) {

    register size_t shift = 0;
    register size_t pos = start_pos;
    char *here;

    if(needle_len == 0) {
        return haystack;
    }

    while (pos < haystack_len) {
        while (pos < haystack_len
            && (shift = table[(unsigned char)haystack[pos]]) > 0) {
                pos += shift;
        }
        if(0 == shift) {
            if(0 ==
                memwildcardcmp(needle, here =
                (char *)&haystack[pos - needle_len + 1],
                needle_len, casesensitive)) {
                    return (here);
            }
            else {
                pos++;
            }
        }
    }
    return NULL;
}

#endif


char *bm_needleinhaystack(char *needle, size_t needle_len,
			  char *haystack, size_t haystack_len,
			  size_t table[UCHAR_MAX + 1], int casesensitive) {

    return bm_needleinhaystack_skipnchars(needle,
        needle_len,
        haystack,
        haystack_len,
        table, casesensitive, needle_len - 1);
}



// find longest header OR footer.  Headers or footers which are
// regular expressions are assigned LARGEST_REGEXP_OVERLAP lengths, to
// allow for regular expressions spanning SIZE_OF_BUFFER-sized chunks
// of the disk image.
int findLongestNeedle(struct SearchSpecLine *SearchSpec) 
{
    int longest = 0;
    int i = 0;
    int lenb, lene;
    for(i = 0; SearchSpec[i].suffix != NULL; i++) {
        lenb =
            SearchSpec[i].beginisRE ? LARGEST_REGEXP_OVERLAP : SearchSpec[i].
            beginlength;
        lene =
            SearchSpec[i].endisRE ? LARGEST_REGEXP_OVERLAP : SearchSpec[i].endlength;
        if(lenb > longest) {
            longest = lenb;
        }
        if(lene > longest) {
            longest = lene;
        }
    }
    return longest;
}


// do a regular expression search using the Tre regular expression
// library.  The needle is a previously compiled regular expression
// (via Tre regcomp()).  The caller must free the memory associated 
// with the returned regmatch_t structure.
regmatch_t *re_needleinhaystack(regex_t * needle, char *haystack,
				size_t haystack_len) 
{
    regmatch_t *match = (regmatch_t *) malloc(sizeof(regmatch_t));


    // LMIII temp fix till working with g++
    if(!regnexec(needle, haystack, (size_t) haystack_len, (size_t) 1, match, 0)) {
        // match
        return match;
    }
    else {
        // no match
        return NULL;
    }
}


// decode strings with embedded escape sequences and return the total length of the
// translated string.

int translate(char *str) 
{
    char next;
    char *rd = str, *wr = str, *bad;
    char temp[1 + 3 + 1];
    char ch;

    if(!*rd) {			//If it's a null string just return
        return 0;
    }

    while (*rd) {
        // Is it an escaped character?
        if(*rd == '\\') {
            rd++;
            switch (*rd) {
            case '\\':
                rd++;
                *(wr++) = '\\';
                break;
            case 'a':
                rd++;
                *(wr++) = '\a';
                break;
            case 's':
                rd++;
                *(wr++) = ' ';
                break;
            case 'n':
                rd++;
                *(wr++) = '\n';
                break;
            case 'r':
                rd++;
                *(wr++) = '\r';
                break;
            case 't':
                rd++;
                *(wr++) = '\t';
                break;
            case 'v':
                rd++;
                *(wr++) = '\v';
                break;
                // Hexadecimal/Octal values are treated in one place using strtoul() 
            case 'x':
            case '0':
            case '1':
            case '2':
            case '3':
                next = *(rd + 1);
                if(next < 48 || (57 < next && next < 65) ||
                    (70 < next && next < 97) || next > 102)
                    break;		//break if not a digit or a-f, A-F 
                next = *(rd + 2);
                if(next < 48 || (57 < next && next < 65) ||
                    (70 < next && next < 97) || next > 102)
                    break;		//break if not a digit or a-f, A-F 

                temp[0] = '0';
                bad = temp;
                strncpy(temp + 1, rd, 3);
                temp[4] = '\0';
                ch = strtoul(temp, &bad, 0);
                if(*bad == '\0') {
                    *(wr++) = ch;
                    rd += 3;
                }			// else INVALID CHARACTER IN INPUT ('\\' followed by *rd) 
                break;
            default:		// INVALID CHARACTER IN INPUT (*rd)
                *(wr++) = '\\';
                break;
            }
        }
        // just copy un-escaped characters
        else {
            *(wr++) = *(rd++);
        }
    }
    *wr = '\0';			// null termination
    return wr - str;
}

// skip leading whitespace
char *skipWhiteSpace(char *str) 
{
    while (isspace(str[0])) {
        str++;
    }
    return str;
}


// describe Scalpel error conditions.  Some errors are fatal, while
// others are advisory.
void handleError(struct scalpelState *state, int error) 
{

    const char * inputId = NULL;
    std::string msg;

    if (state->inReader) {
        inputId = scalpelInputGetId(state->inReader);
    }
    else {
        inputId = " (input reader not yet set)";
    }

    switch (error) {

    case SCALPEL_ERROR_PTHREAD_FAILURE:
        // fatal
        msg = "Scalpel was unable to create threads and will abort.\n";
        scalpelLog(state, msg.c_str());
        closeAuditFile(state->auditFile);
        throw std::runtime_error(msg);

        break;

    case SCALPEL_ERROR_FILE_OPEN:
        // non-fatal
        if (inputId == 0 || *(inputId) == '\0') {
            scalpelLog(state, "Scalpel was unable to open the input file: <blank>....\n"
                "Skipping...\n");
        }
        else {
            scalpelLog(state, "Scalpel was unable to open the input file: %s...%s\n"
                "Skipping...\n", inputId, strerror(errno));
        }
        break;
    case SCALPEL_ERROR_FILE_TOO_SMALL:
        // non-fatal
        scalpelLog(state,
            "The input file %s is smaller than the longest header/footer and cannot be processed.\n"
            "Skipping...\n", inputId);
        break;

    case SCALPEL_ERROR_FILE_READ:
        // non-fatal
        scalpelLog(state, "Scalpel was unable to read the input file: %s\n"
            "Skipping...\n", inputId);
        break;

    case SCALPEL_ERROR_FATAL_READ:
        // fatal
        msg = "Scalpel was unable to read a needed file and will abort.\n";
        scalpelLog(state, msg.c_str());
        closeAuditFile(state->auditFile);
        throw std::runtime_error(msg);
        break;

    case SCALPEL_ERROR_NONEMPTY_DIRECTORY:
        // fatal
        msg = "Scalpel will write only to empty output directories to avoid\n"
            "mixing the output from multiple carving operations.\n"
            "Please specify a different output directory or delete the specified\noutput directory.\n";
        fprintf(stderr, "%s", msg.c_str());
        closeAuditFile(state->auditFile);
        throw std::runtime_error(msg);
        break;

    case SCALPEL_ERROR_FILE_WRITE:
        // fatal--unable to write files, which may mean that disk space is exhausted.
        msg = "Scalpel was unable to write output files and will abort.\n"
            "This error generally indicates that disk space is exhausted.\n";
        fprintf(stderr, "%s", msg.c_str());
        closeAuditFile(state->auditFile);
        throw std::runtime_error(msg);
        break;

    case SCALPEL_ERROR_NO_SEARCH_SPEC:
        // fatal, configuration file didn't specify anything to carve
        msg = "ERROR: The configuration file didn't specify any file types to carve.\n"
            "(If you're using the default configuration file, you'll have to "
            "uncomment some of the file types.)\n";
        scalpelLog(state, msg.c_str() );
        closeAuditFile(state->auditFile);
        throw std::runtime_error(msg);
        break;

    case SCALPEL_GENERAL_ABORT:
        // fatal
        msg = "Scalpel will abort.\n";
        scalpelLog(state, msg.c_str());
        closeAuditFile(state->auditFile);
        throw std::runtime_error(msg);
        break;

    default:
        // fatal
        closeAuditFile(state->auditFile);
        throw std::runtime_error("Unexpected error");
    }
}

void setttywidth(void) {
#if defined (_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if(!GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        ttywidth = 80;
    }
    else {
        ttywidth = csbi.dwSize.X;
    }
#else
    //#if defined(__BSD)
    //  ttywidth = 80;
    //#else
    struct winsize winsize;
    if(ioctl(fileno(stdout), TIOCGWINSZ, &winsize) != -1) {
        ttywidth = winsize.ws_col;
    }
    else {
        // 80 is a reasonable default
        ttywidth = 80;
    }
    //#endif
#endif
}


int skipInFile(struct scalpelState *state, ScalpelInputReader * inReader) 
{

    int retries = 0;
    const char * const inputId = scalpelInputGetId(state->inReader);

    while (TRUE) {
        if((scalpelInputSeeko(inReader, state->skip, SCALPEL_SEEK_SET))) {

            fprintf(stderr,
                "ERROR: Couldn't skip %"PRIu64 " bytes at the start of input file %s\n",
                state->skip, inputId);


            if(retries++ > 3) {
                fprintf(stderr, "Sorry, maximum retries exceeded...\n");
                return FALSE;
            }
            else {
                fprintf(stderr, "Waiting to try again... \n");
                sleep(3);
            }
        }
        else {

            fprintf(stderr, "\nSkipped the first %"PRIu64 " bytes of %s...\n",
                state->skip, inputId);


            return TRUE;
        }
    }
    return TRUE;
}
