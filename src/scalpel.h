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

#ifndef SCALPEL_H
#define SCALPEL_H
#define SCALPEL_VERSION    "2.1"

//#define GPU_THREADING
#ifndef SINGLE_THREADING
#define MULTICORE_THREADING
#endif
#define USE_FAST_STRING_SEARCH

#define _USE_LARGEFILE              1
#define _USE_FILEOFFSET64           1
#define _USE_LARGEFILE64            1
#define _LARGEFILE_SOURCE           1
#define _LARGEFILE64_SOURCE         1
#define _FILE_OFFSET_BITS           64

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <dirent.h>
#include <stdarg.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/timeb.h>
#include <sys/time.h>

//C++ STL headers
#include <exception>
#include <stdexcept>
#include <string>
#include <sstream>

//scalpel headers
#include "base_name.h"
#include "prioque.h"
#include "syncqueue.h"
#include "common.h"
#include "types.h"

#include "input_reader.h"


#ifdef __APPLE__
#define __UNIX
#include <sys/ttycom.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <libgen.h>
#include <tre/regex.h>
// off_t on Mac OS X is 64 bits
#define off64_t  off_t
#endif /* ifdef __APPLE__ */

#ifdef __linux
#define __UNIX
#include <linux/hdreg.h>
#include <libgen.h>
#include <error.h>
#include <tre/regex.h>
#endif /* ifdef __linux */

#if defined ( _WIN32)
#include <windows.h>
#include <sys/timeb.h>
#include "regex.h"
#define gettimeofday(A, B) QueryPerformanceCounter(A)
#define sleep    Sleep
#define  snprintf         _snprintf
#define lstat(A,B)      stat(A,B)
#include <libgen.h>
//char *basename (char *path);
extern char *optarg;
extern int optind;
int getopt (int argc, char *const argv[], const char *optstring);

#ifdef __MINGW32__
#define realpath(A,B)    _fullpath(B,A,PATH_MAX)
#endif

#ifdef __CYGWIN32__
#define realpath(A,B) \
  (getenv ("CYGWIN_USE_WIN32_PATHS") \
   ? (cygwin_conv_to_full_win32_path ((A), (B)), B) \
   : realpath ((A), (B)))
#endif
#else // ! defined(WIN32)
#include <sys/mount.h>
#define gettimeofday_t struct timeval
#endif // ! defined(WIN32)

#define SEARCHTYPE_FORWARD      0
#define SEARCHTYPE_REVERSE      1
#define SEARCHTYPE_FORWARD_NEXT 2

// LARGEST_REGEXP_OVERLAP specifies the largest regular expression overlap
// across the boundaries of SIZE_OF_BUFFER-sized chunks of the disk image.
//  This is also used internally as the maximum "size" of a regular expression
// and affects the mininum disk image size that can be processed.  Large
// values will have negative impacts on performance.
#define LARGEST_REGEXP_OVERLAP    1024

#define SCALPEL_SIZEOFBUFFER_PANIC_STRING \
"PANIC: SIZE_OF_BUFFER has been incorrectly configured.\n"

#define SCALPEL_BLOCK_SIZE            512
#define MAX_STRING_LENGTH            4096
#define MAX_NEEDLES                   254
#define NUM_SEARCH_SPEC_ELEMENTS        6
#define MAX_SUFFIX_LENGTH               8
#define MAX_FILE_TYPES                100
#define MAX_MATCHES_PER_BUFFER        (SIZE_OF_BUFFER / 10)	// BUG: MUST ERROR OUT PROPERLY ON OVERFLOW (check)

// Length of the queues used to tranfer data / results blocks to workers.
#define QUEUELEN 20

#define MAX_FILES_PER_SUBDIRECTORY    1000

#define SCALPEL_OK                             0
#define SCALPEL_ERROR_NO_SEARCH_SPEC           1
#define SCALPEL_ERROR_FILE_OPEN                2
#define SCALPEL_ERROR_FILE_READ                3
#define SCALPEL_ERROR_FILE_WRITE               4
#define SCALPEL_ERROR_FILE_CLOSE               5
#define SCALPEL_ERROR_TOO_MANY_TYPES           6
#define SCALPEL_ERROR_FATAL_READ               7
#define SCALPEL_ERROR_BAD_HEADER_REGEX         8
#define SCALPEL_ERROR_BAD_FOOTER_REGEX         9
#define SCALPEL_ERROR_FILE_TOO_SMALL          10
#define SCALPEL_ERROR_NONEMPTY_DIRECTORY      11
#define SCALPEL_ERROR_PTHREAD_FAILURE         12

#define SCALPEL_GENERAL_ABORT                999

#define UNITS_BYTES                     0
#define UNITS_KILOB                     1
#define UNITS_MEGAB                     2
#define UNITS_GIGAB                     3
#define UNITS_TERAB                     4
#define UNITS_PETAB                     5
#define UNITS_EXAB                      6

// GLOBALS


// signal has been caught by signal handler
extern int signal_caught;

// current wildcard character
extern char wildcard;

// width of tty, for progress bar
extern int ttywidth;

extern int errno;

extern double totalsearch;	// # of seconds spent in pass # 1 header/footer searches
extern double totalqueues;	// # of seconds spent building work queues
extern double totalreads;	// # of seconds spent in all passes for input file reads
extern double totalwrites;	// # of seconds spent in pass # 2 for writing carved files

#define SCALPEL_NOEXTENSION_SUFFIX "NONE"
#define SCALPEL_NOEXTENSION '\xFF'

#define SCALPEL_DEFAULT_WILDCARD       '?'
#define SCALPEL_DEFAULT_CONFIG_FILE    "scalpel.conf"

#define SCALPEL_DEFAULT_OUTPUT_DIR     "scalpel-output"

#define SCALPEL_BANNER_STRING \
"Scalpel version %s\n"\
"Written by Golden G. Richard III and Lodovico Marziale.\n", SCALPEL_VERSION

#define SCALPEL_COPYRIGHT_STRING \
"Scalpel is (c) 2005-13 by Golden G. Richard III and Lodovico Marziale.\n"

// During the file carving operations (which occur after an initial
// scan of an image file to build the header/footer database), we want
// to read the image file only once more, sequentially, for all
// carves.  The following structure tracks the filename and first/last
// bytes in the image file for a single file to be carved.  When the
// read buffer includes the first byte of a file, the file is opened
// and the first write occurs.  When the read buffer includes the end
// byte, the last write operation occurs, the file is closed, and the
// struct can be reused.

// *****GGRIII: use of priority field to store these flags and the 
// data structures which track CarveInfo structs needs to be better
// documented

#define STARTCARVE      1	// carve operation for this CarveInfo struct
				// starts in current buffer
#define STOPCARVE       2	// carve operation stops in current buffer
#define STARTSTOPCARVE  3	// carve operation both starts and stops in
				// current buffer
#define CONTINUECARVE   4	// carve operation includes entire contents
				// of current buffer

typedef struct CarveInfo {
  char *filename;		// output filename for file to carve
  FILE *fp;			// file descriptor for file to carve
  unsigned long long start;	// offset of first byte in file
  unsigned long long stop;	// offset of last byte in file
  char chopped;			// is carved file's length constrained
  // by max file size for type? (i.e., could
  // the file actually be longer?
} CarveInfo;


// Each struct SearchSpecLine defines a particular file type,
// including header and footer information.  The following structure,
// SearchSpecOffsets, defines the absolute locations of all matching
// headers and footers for a particular file type.  Because the entire
// header/footer database is built during a single pass over an image
// or device file, the header and footer locations are sorted in
// ascending order.

typedef struct SearchSpecOffsets {
  unsigned long long *headers;	// positions of discovered headers
  size_t *headerlens;		// lengths of discovered headers
  unsigned long long headerstorage;	// space allocated for this many header offsets
  unsigned long long numheaders;	// # stored header positions
  unsigned long long *footers;	// positions of discovered footers
  size_t *footerlens;		// lengths of discovered footers
  unsigned long long footerstorage;	// space allocated for this many footer offsets
  unsigned long long numfooters;	// # stored footer positions
} SearchSpecOffsets;

// max files to open at once during carving--modify if you get
// a "too many files open" error message during the second carving phase.
#ifdef _WIN32
#define MAX_FILES_TO_OPEN            20
#else
#define MAX_FILES_TO_OPEN            512
#endif


typedef union SearchState {
  size_t bm_table[UCHAR_MAX + 1];
  regex_t re;
} SearchState;

typedef struct SearchSpecLine {
  char *suffix;
  int casesensitive;
  unsigned long long length;
  unsigned long long minlength;
  char *begin;          // translate()-d header
  char *begintext;      // textual version of header for humans
  int beginlength;
  int beginisRE;
  SearchState beginstate;
  char *end;            // translate()-d footer
  char *endtext;        // textual version of footer for humans
  int endlength;
  int endisRE;
  SearchState endstate;
  int searchtype;		// FORWARD, NEXT, REVERSE search type for footer
  struct SearchSpecOffsets offsets;
  unsigned long long numfilestocarve;	// # files to carve of this type
  unsigned long organizeDirNum;	// subdirectory # for organization 
  // of files of this type
} SearchSpecLine;

//prototype for external carving function
extern int scalpel_carveSingleInput(ScalpelInputReader * const reader,
		const char * const confFilePath,
		const char * const outDir,
		const unsigned char generateFooterDb,
		const unsigned char handleEmbedded,
		const unsigned char organizeSubdirs,
		const unsigned char previewMode,
		const unsigned char carveWithMissingFooters,
		const unsigned char noSearchOverlap
		) throw (std::runtime_error);

typedef struct scalpelState {
  ScalpelInputReader * inReader;
  char *conffile;
  char *outputdirectory;
  int specLines;
  struct SearchSpecLine *SearchSpec;
  unsigned long long fileswritten;
  int modeVerbose;
  int modeNoSuffix;
  FILE *auditFile;
  char *invocation;
  unsigned long long skip;
  char *coveragefile;
  unsigned int coverageblocksize;
  FILE *coverageblockmap;
  unsigned char *coveragebitmap;
  unsigned long long coveragenumblocks;
  int useInputFileList;
  char *inputFileList;
  int carveWithMissingFooters;
  int noSearchOverlap;
  int handleEmbedded;
  int generateHeaderFooterDatabase;
  int updateCoverageBlockmap;
  int useCoverageBlockmap;
  int organizeSubdirectories;
  unsigned long long organizeMaxFilesPerSub;
  int blockAlignedOnly;
  unsigned int alignedblocksize;
  int previewMode;
} scalpelState;


// one extent for a fragmented file.  'start' and 'stop'
// are real disk image addresses that define the fragment's
// location.
typedef struct Fragment {
  unsigned long long start;
  unsigned long long stop;
} Fragment;

// Interface for using scalpel as a library
extern int libscalpel_initialize(scalpelState ** state, char * confFilePath, 
                                 char * outDir, const scalpelState& options);
extern int libscalpel_carve_input(scalpelState * state, ScalpelInputReader * const reader);
extern int libscalpel_finalize(scalpelState ** state);

// prototypes for visible scalpel.c functions
void freeState(struct scalpelState *state);
void initializeState(char ** argv, struct scalpelState *state);
void convertFileNames(struct scalpelState *state);
int readSearchSpecFile(struct scalpelState *state);


// prototypes for visible dig.c functions
int init_threading_model (struct scalpelState *state);
void destroy_threading_model(struct scalpelState *state);
int digImageFile (struct scalpelState *state);
int carveImageFile (struct scalpelState *state);
void init_store ();  // return int for error??
void destroyStore();

// prototypes for visible helpers.c functions

// LMIII fix me
#ifndef _WIN32
double elapsed (struct timeval A, struct timeval B);
//double elapsed(gettimeofday_t a, gettimeofday_t b);
#endif
int isRegularExpression (char *s);
void checkMemoryAllocation (struct scalpelState *state, void *ptr, int line,
			    const char *file, const char *structure);
int skipInFile (struct scalpelState *state, ScalpelInputReader * inReader);
void scalpelLog (struct scalpelState *state, const char *format, ...);
void handleError (struct scalpelState *s, int error);
int memwildcardcmp (const void *s1, const void *s2,
		    size_t n, int caseSensitive);
void setProgramName (char *s);
void init_bm_table (char *needle, size_t table[UCHAR_MAX + 1],
		    size_t len, int casesensitive);
int findLongestNeedle (struct SearchSpecLine *SearchSpec);
regmatch_t *re_needleinhaystack (regex_t * needle,
				 char *haystack, size_t haystack_len);
char *bm_needleinhaystack (char *needle, size_t needle_len,
			   char *haystack, size_t haystack_len,
			   size_t table[UCHAR_MAX + 1], int casesensitive);
int translate (char *str);
char *skipWhiteSpace (char *str);
void setttywidth ();

// prototypes for visible files.c functions
long long measureOpenFile (FILE * f, struct scalpelState *state);
int openAuditFile (struct scalpelState *state);
int closeAuditFile (FILE * f);

//// prototypes for visible dig.cu functions
int gpuSearchBuffer (char *readbuffer, int size_of_buffer, char *gpuresults,
		     int longestneedle, char wildcard);
void copytodevicepattern (char hostpatterntable[MAX_PATTERNS][MAX_PATTERN_LENGTH]);
void copytodevicelookup_headers(char hostlookuptable[LOOKUP_ROWS][LOOKUP_COLUMNS]);
void copytodevicelookup_footers(char hostlookuptable[LOOKUP_ROWS][LOOKUP_COLUMNS]);
void ourCudaMallocHost (void **ptr, int len);
int gpu_init (int longestneedle);
int gpu_cleanup();

// WIN32 string.h wierdness
#ifdef _WIN32
extern const char *strsignal (int sig);
//extern char *strsignal (int sig);
#else
extern char *strsignal (int sig);
#endif /*  ifdef _WIN32 */

#endif /* ifndef SCALPEL_H */
