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


/////////// GLOBALS ////////////

static char *readbuffer;	// Read buffer--process image files in 
                            // SIZE_OF_BUFFER-size chunks.

// Info needed for each of above SIZE_OF_BUFFER chunks.
typedef struct readbuf_info {
    long long bytesread;		// number of bytes in this buf
    long long beginreadpos;	    // position in the image
    char *readbuf;		        // pointer SIZE_OF_BUFFER array
} readbuf_info;


// queues to facilitiate async reads, concurrent cpu, gpu work
static syncqueue_t *full_readbuf;	// que of full buffers read from image
static syncqueue_t *empty_readbuf;	// que of empty buffers
static readbuf_info *readbuf_store; //read  buff stored here to free

//pthread_cond_t reader_done = PTHREAD_COND_INITIALIZER;

#ifdef GPU_THREADING
// GPU only threading globals
syncqueue_t *results_readbuf;	// que of encoded results from gpu
//pthread_cond_t gpu_handler_done = PTHREAD_COND_INITIALIZER;
sig_atomic_t reads_finished;
sig_atomic_t gpu_finished = FALSE;	// for thread synchronization
char localpattern[MAX_PATTERNS][MAX_PATTERN_LENGTH];	// search patterns
char locallookup_headers[LOOKUP_ROWS][LOOKUP_COLUMNS];	// header lookup table
char locallookup_footers[LOOKUP_ROWS][LOOKUP_COLUMNS];	// footer lookup table

#endif



#ifdef MULTICORE_THREADING
// Multi-core only threading globals

// Parameters to threads under the MULTICORE_THREADING model, to allow
// efficient header/footer searches.
typedef struct ThreadFindAllParams {
    int id;
    char *str;
    size_t length;
    char *startpos;
    long offset;
    char **foundat;
    size_t *foundatlens;
    int strisRE;
    union {
        size_t *table;
        regex_t *regex;
    };
    int casesensitive;
    int nosearchoverlap;
    struct scalpelState *state;
} ThreadFindAllParams;

// TODO:  These structures could be released after the dig phase; they aren't needed in the carving phase since it's not
// threaded.  Look into this in the future.

static pthread_t *searchthreads;	// thread pool for header/footer searches
static ThreadFindAllParams *threadargs;	// argument structures for threads
static char ***foundat;		// locations of headers/footers discovered by threads    
static size_t **foundatlens;	// lengths of discovered headers/footers
//static sem_t *workavailable;	// semaphores that block threads until 
// header/footer searches are required
static pthread_mutex_t *workavailable;
//static sem_t *workcomplete;	// semaphores that allow main thread to wait
// for all search threads to complete current job
static pthread_mutex_t *workcomplete;

#endif

// prototypes for private dig.c functions
static unsigned long long 
    adjustForEmbedding(struct SearchSpecLine *currentneedle,
                       unsigned long long headerindex,
                       unsigned long long *prevstopindex);
static int writeHeaderFooterDatabase(struct scalpelState *state);
static int setupCoverageMaps(struct scalpelState *state, 
                             unsigned long long filesize);
static int auditUpdateCoverageBlockmap(struct scalpelState *state,
                                       struct CarveInfo *carve);
static int updateCoverageBlockmap(struct scalpelState *state,
                                  unsigned long long block);
static void generateFragments(struct scalpelState *state, Queue * fragments,
                              struct CarveInfo *carve);
static unsigned long long 
    positionUseCoverageBlockmap(struct scalpelState *state,
                                unsigned long long
                                position);
static void destroyCoverageMaps(struct scalpelState *state);
static int fseeko_use_coverage_map(struct scalpelState *state, 
                                   ScalpelInputReader * const inReader,
                                   off64_t offset);
static off64_t ftello_use_coverage_map(struct scalpelState *state, 
                                       ScalpelInputReader * const inReader);
static size_t fread_use_coverage_map(struct scalpelState *state, void *ptr,
                                     size_t size, size_t nmemb, 
                                     ScalpelInputReader * const inReader);
#ifdef UNUSED
static void printhex(char *s, int len);
#endif

static void clean_up(struct scalpelState *state, int signum);
static int displayPosition(int *units, unsigned long long pos,
                           unsigned long long size, const char *fn);
static int setupAuditFile(struct scalpelState *state);
static int digBuffer(struct scalpelState *state,
                     unsigned long long lengthofbuf,
                     unsigned long long offset);
#ifdef MULTICORE_THREADING
static void *threadedFindAll(void *args);
#endif


// force header/footer matching to deal with embedded headers/footers
static unsigned long long
adjustForEmbedding(struct SearchSpecLine *currentneedle,
                   unsigned long long headerindex,
                   unsigned long long *prevstopindex) {

    unsigned long long h = headerindex + 1;
    unsigned long long f = 0;
    int header = 0;
    int footer = 0;
    long long headerstack = 0;
    unsigned long long start = currentneedle->offsets.headers[headerindex] + 1;
    unsigned long long candidatefooter;
    int morecandidates = 1;
    int moretests = 1;

    // skip footers which precede header
    while (*prevstopindex < currentneedle->offsets.numfooters &&
           currentneedle->offsets.footers[*prevstopindex] < start) {
            (*prevstopindex)++;
    }

    f = *prevstopindex;
    candidatefooter = *prevstopindex;

    // skip footers until header/footer count balances
    while (candidatefooter < currentneedle->offsets.numfooters && 
           morecandidates) {
        // see if this footer is viable
        morecandidates = 0;	 // assumption is yes, viable
        moretests = 1;
        while (moretests && 
               start < currentneedle->offsets.footers[candidatefooter]) {
            header = 0;
            footer = 0;
            if(h < currentneedle->offsets.numheaders
                && currentneedle->offsets.headers[h] >= start
                && currentneedle->offsets.headers[h] <
                currentneedle->offsets.footers[candidatefooter]) {
                    header = 1;
            }
            if(f < currentneedle->offsets.numfooters
                && currentneedle->offsets.footers[f] >= start
                && currentneedle->offsets.footers[f] <
                currentneedle->offsets.footers[candidatefooter]) {
                    footer = 1;
            }

            if(header && (!footer ||
                currentneedle->offsets.headers[h] <
                currentneedle->offsets.footers[f])) {
                    h++;
                    headerstack++;
                    start = (h < currentneedle->offsets.numheaders)
                        ? currentneedle->offsets.headers[h] : start + 1;
            }
            else if(footer) {
                f++;
                headerstack--;
                if(headerstack < 0) {
                    headerstack = 0;
                }
                start = (f < currentneedle->offsets.numfooters)
                    ? currentneedle->offsets.footers[f] : start + 1;
            }
            else {
                moretests = 0;
            }
        }

        if(headerstack > 0) {
            // header/footer imbalance, need to try next footer
            candidatefooter++;
            // this footer counts against footer deficit!
            headerstack--;
            morecandidates = 1;
        }
    }

    return (headerstack > 0) ? 999999999999999999ULL : candidatefooter;
}


#ifdef UNUSED
// output hex notation for chars in 's'
static void printhex(char *s, int len) {

    int i;
    for(i = 0; i < len; i++) {
        printf("\\x%.2x", (unsigned char)s[i]);
    }
}
#endif

static void clean_up(struct scalpelState *state, int signum) {
    std::stringstream ss;
    ss << "Cleaning up...\nCaught signal: " << signum;
    ss << std::endl << "Program is terminating early" << std::endl;
    //<< strsignal(signum)); TODO ADAM fix strsignal / libiberty
    std::string msg = ss.str();
    scalpelLog(state, msg.c_str());
    closeAuditFile(state->auditFile);
    throw std::runtime_error(msg);
}

// display progress bar
static int
displayPosition(int *units,
                unsigned long long pos, unsigned long long size, 
                const char *fn) {
    double percentDone = (((double)pos) / (double)(size) * 100);
    double position = (double)pos;
    int count;
    int barlength, i, len;
    double elapsed;
    long remaining;

    char buf[MAX_STRING_LENGTH];
    char line[MAX_STRING_LENGTH];

#ifdef _WIN32
    static LARGE_INTEGER start;
    LARGE_INTEGER now;
    static LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
#else
    static struct timeval start;
    struct timeval now, td;
#endif

    // get current time and remember start time when first chunk of 
    // an image file is read

    if(pos <= SIZE_OF_BUFFER) {
        gettimeofday(&start, (struct timezone *)0);
    }
    gettimeofday(&now, (struct timezone *)0);

    // First, reduce the position to the right units 
    for(count = 0; count < *units; count++) {
        position = position / 1024;
    }

    // Now check if we've hit the next type of units 
    while (position > 1023) {
        position = position / 1024;
        (*units)++;
    }

    switch (*units) {

    case UNITS_BYTES:
        sprintf(buf, "bytes");
        break;
    case UNITS_KILOB:
        sprintf(buf, "KB");
        break;
    case UNITS_MEGAB:
        sprintf(buf, "MB");
        break;
    case UNITS_GIGAB:
        sprintf(buf, "GB");
        break;
    case UNITS_TERAB:
        sprintf(buf, "TB");
        break;
    case UNITS_PETAB:
        sprintf(buf, "PB");
        break;
    case UNITS_EXAB:
        sprintf(buf, "EB");
        break;

    default:
        fprintf(stdout, "Unable to compute progress.\n");
        return SCALPEL_OK;
    }

    len = 0;
    len +=
        snprintf(line + len, sizeof(line) - len, "\r%s: %5.1f%% ", fn, percentDone);
    barlength = ttywidth - strlen(fn) - strlen(buf) - 32;
    if(barlength > 0) {
        i = barlength * (int)percentDone / 100;
        len += snprintf(line + len, sizeof(line) - len,
            "|%.*s%*s|", i,
            "****************************************************************************************************************************************************************",
            barlength - i, "");
    }

    len += snprintf(line + len, sizeof(line) - len, " %6.1f %s", position, buf);

#ifdef _WIN32
    elapsed =
        ((double)now.QuadPart - (double)start.QuadPart) / ((double)freq.QuadPart);
    //printf("elapsed: %f\n",elapsed);
#else
    timersub(&now, &start, &td);
    elapsed = td.tv_sec + (td.tv_usec / 1000000.0);
#endif
    remaining = (long)((100 - percentDone) / percentDone * elapsed);
    //printf("Ratio remaining: %f\n",(100-percentDone)/percentDone);
    //printf("Elapsed time: %f\n",elapsed);
    if(remaining >= 100 * (60 * 60)) {	//60*60 is seconds per hour
        len += snprintf(line + len, sizeof(line) - len, " --:--ETA");
    }
    else {
        i = remaining / (60 * 60);
        if(i)
            len += snprintf(line + len, sizeof(line) - len, " %2d:", i);
        else
            len += snprintf(line + len, sizeof(line) - len, "    ");
        i = remaining % (60 * 60);
        len +=
            snprintf(line + len, sizeof(line) - len, "%02d:%02d ETA", i / 60, i % 60);
    }

    fprintf(stdout, "%s", line);
    fflush(stdout);

    return SCALPEL_OK;
}

// create initial entries in audit for each image file processed
static int setupAuditFile(struct scalpelState *state) {

    char inputStreamId[MAX_STRING_LENGTH];

    if(realpath(scalpelInputGetId(state->inReader), inputStreamId)) {
        scalpelLog(state, "\nOpening target \"%s\"\n\n", inputStreamId);
    }
    else {
        //handleError(state, SCALPEL_ERROR_FILE_OPEN);
        return SCALPEL_ERROR_FILE_OPEN;
    }


    if(state->skip) {
        fprintf(state->auditFile, "\nSkipped the first %" PRIu64 "bytes of %s...\n",
            state->skip, scalpelInputGetId(state->inReader));
        if(state->modeVerbose) {
            fprintf(stdout, "\nSkipped the first %" PRIu64 "bytes of %s...\n",
                state->skip, scalpelInputGetId(state->inReader));
        }
    }


    fprintf(state->auditFile, "The following files were carved:\n");
    fprintf(state->auditFile,
        "File\t\t  Start\t\t\tChop\t\tLength\t\tExtracted From\n");

    return SCALPEL_OK;
}

static int
digBuffer(struct scalpelState *state, unsigned long long lengthofbuf,
          unsigned long long offset) {

    unsigned long long startLocation = 0;
    int needlenum, i = 0;
    struct SearchSpecLine *currentneedle = 0;
    //  gettimeofday_t srchnow, srchthen;

    // for each file type, find all headers and some (or all) footers

    // signal check
    if(signal_caught == SIGTERM || signal_caught == SIGINT) {
        clean_up(state, signal_caught);
    }


    ///////////////////////////////////////////////////
    ///////////////////////////////////////////////////
    ////////////////GPU-based SEARCH///////////////////
    ///////////////////////////////////////////////////
    ///////////////////////////////////////////////////
#ifdef GPU_THREADING		// code for searches on GPUs


    // BUG (GGRIII): GPU MODEL DOES NOT SUPPORT REGULAR EXPRESSIONS
    // (LMIII): NOR WILL IT EVER.  
    // GGRIII: let's not be hasty.  :)

    // In GPU mode, the needle search has already been done. readbuffer now holds
    // the encoded results, which must be decoded into scalpel structures.         

    // Each needle found is encoded in 2 bytes, one for the <type and one for position in buffer?>

    int longestneedle = findLongestNeedle(state->SearchSpec);

    // Compute the size of the results buffer.      
    int result_chunks = (lengthofbuf / (THREADS_PER_BLOCK - longestneedle)) + 1;
    int result_length = result_chunks * RESULTS_SIZE_PER_BLOCK;

    // Decode the results from the gpu.
    int d = 0;
    for(d = 0; d < result_length; d += 2) {
        i = (d / RESULTS_SIZE_PER_BLOCK) * (THREADS_PER_BLOCK - longestneedle) +
            (unsigned char)(readbuffer[d + 1]);
        if(readbuffer[d] > 0) {

            needlenum = readbuffer[d] - 1;
            currentneedle = &(state->SearchSpec[needlenum]);
            startLocation = offset + i;

            // found a header--record location in header offsets database
            if(state->modeVerbose) {

                fprintf(stdout, "A %s header was found at : %" PRIu64 "\n",
                    currentneedle->suffix,
                    positionUseCoverageBlockmap(state, startLocation));

            }

            currentneedle->offsets.numheaders++;
            if(currentneedle->offsets.headerstorage <=
                currentneedle->offsets.numheaders) {
                    // need more memory for header offset storage--add an
                    // additional 100 elements              
                    currentneedle->offsets.headers = (unsigned long long *)
                        realloc(currentneedle->offsets.headers,
                        sizeof(unsigned long long) *
                        (currentneedle->offsets.numheaders + 100));


                    checkMemoryAllocation(state, currentneedle->offsets.headers,
                        __LINE__, __FILE__, "header array");
                    // inserted
                    currentneedle->offsets.headerlens = (size_t *)
                        realloc(currentneedle->offsets.headerlens, sizeof(size_t) *
                        (currentneedle->offsets.numheaders + 100));
                    checkMemoryAllocation(state, currentneedle->offsets.headerlens,
                        __LINE__, __FILE__, "header array");
                    currentneedle->offsets.headerstorage =
                        currentneedle->offsets.numheaders + 100;

                    if(state->modeVerbose) {
                        fprintf(stdout,
                            "Memory reallocation performed, total header storage = %" PRIu64 "\n",
                            currentneedle->offsets.headerstorage);
                    }
            }
            currentneedle->offsets.headers[currentneedle->offsets.numheaders -
                1] = startLocation;

        }
        else if(readbuffer[d] < 0) {	// footer

            needlenum = -1 * readbuffer[d] - 1;
            currentneedle = &(state->SearchSpec[needlenum]);
            startLocation = offset + i;
            // found a footer--record location in footer offsets database
            if(state->modeVerbose) {

                fprintf(stdout, "A %s footer was found at : %" PRIu64 "\n",
                    currentneedle->suffix,
                    positionUseCoverageBlockmap(state, startLocation));

            }

            currentneedle->offsets.numfooters++;
            if(currentneedle->offsets.footerstorage <=
                currentneedle->offsets.numfooters) {
                    // need more memory for footer offset storage--add an
                    // additional 100 elements
                    currentneedle->offsets.footers = (unsigned long long *)
                        realloc(currentneedle->offsets.footers,
                        sizeof(unsigned long long) *
                        (currentneedle->offsets.numfooters + 100));

                    checkMemoryAllocation(state, currentneedle->offsets.footers,
                        __LINE__, __FILE__, "footer array");
                    currentneedle->offsets.footerlens =
                        (size_t *) realloc(currentneedle->offsets.footerlens,
                        sizeof(size_t) *
                        (currentneedle->offsets.numfooters + 100));
                    checkMemoryAllocation(state, currentneedle->offsets.footerlens,
                        __LINE__, __FILE__, "footer array");
                    currentneedle->offsets.footerstorage =
                        currentneedle->offsets.numfooters + 100;

                    if(state->modeVerbose) {

                        fprintf(stdout,
                            "Memory reallocation performed, total footer storage = %" PRIu64 "\n",
                            currentneedle->offsets.footerstorage);
                    }
            }
            currentneedle->offsets.footers[currentneedle->offsets.numfooters -
                1] = startLocation;

        }
    }

#endif
    ///////////////////////////////////////////////////
    ///////////////////////////////////////////////////
    ////////////////END GPU-BASED SEARCH///////////////
    ///////////////////////////////////////////////////
    ///////////////////////////////////////////////////



    ///////////////////////////////////////////////////
    ///////////////////////////////////////////////////
    ///////////////MULTI-THREADED SEARCH //////////////
    ///////////////////////////////////////////////////
    ///////////////////////////////////////////////////
#ifdef MULTICORE_THREADING

    // as of v1.9, this is now the lowest common denominator mode

    // ---------------- threaded header search ------------------ //
    // ---------------- threaded header search ------------------ //

    //  gettimeofday(&srchthen, 0);
    if(state->modeVerbose) {
        printf("Waking up threads for header searches.\n");
    }
    for(needlenum = 0; needlenum < state->specLines; needlenum++) {
        currentneedle = &(state->SearchSpec[needlenum]);
        // # of matches in last element of foundat array
        foundat[needlenum][MAX_MATCHES_PER_BUFFER] = 0;
        threadargs[needlenum].id = needlenum;
        threadargs[needlenum].str = currentneedle->begin;
        threadargs[needlenum].length = currentneedle->beginlength;
        threadargs[needlenum].startpos = readbuffer;
        threadargs[needlenum].offset = (long)readbuffer + lengthofbuf;
        threadargs[needlenum].foundat = foundat[needlenum];
        threadargs[needlenum].foundatlens = foundatlens[needlenum];
        threadargs[needlenum].strisRE = currentneedle->beginisRE;
        if(currentneedle->beginisRE) {
            threadargs[needlenum].regex = &(currentneedle->beginstate.re);
        }
        else {
            threadargs[needlenum].table = currentneedle->beginstate.bm_table;
        }
        threadargs[needlenum].casesensitive = currentneedle->casesensitive;
        threadargs[needlenum].nosearchoverlap = state->noSearchOverlap;
        threadargs[needlenum].state = state;

        // unblock thread
        //    sem_post(&workavailable[needlenum]);

        pthread_mutex_unlock(&workavailable[needlenum]);

    }

    // ---------- thread group synchronization point ----------- //
    // ---------- thread group synchronization point ----------- //

    if(state->modeVerbose) {
        printf("Waiting for thread group synchronization.\n");
    }

    // wait for all threads to complete header search before proceeding
    for(needlenum = 0; needlenum < state->specLines; needlenum++) {
        //    sem_wait(&workcomplete[needlenum]);

        pthread_mutex_lock(&workcomplete[needlenum]);

    }

    if(state->modeVerbose) {
        printf("Thread group synchronization complete.\n");
    }

    // digest header locations discovered by the thread group

    for(needlenum = 0; needlenum < state->specLines; needlenum++) {
        currentneedle = &(state->SearchSpec[needlenum]);

        // number of matches stored in last element of vector
        for(i = 0; i < (long)foundat[needlenum][MAX_MATCHES_PER_BUFFER]; i++) {
            startLocation = offset + (foundat[needlenum][i] - readbuffer);
            // found a header--record location in header offsets database
            if(state->modeVerbose) {

                fprintf(stdout, "A %s header was found at : %" PRIu64 "\n",
                    currentneedle->suffix,
                    positionUseCoverageBlockmap(state, startLocation));
            }

            currentneedle->offsets.numheaders++;
            if(currentneedle->offsets.headerstorage <=
                currentneedle->offsets.numheaders) {
                    // need more memory for header offset storage--add an
                    // additional 100 elements
                    currentneedle->offsets.headers = (unsigned long long *)
                        realloc(currentneedle->offsets.headers,
                        sizeof(unsigned long long) *
                        (currentneedle->offsets.numheaders + 100));
                    checkMemoryAllocation(state, currentneedle->offsets.headers,
                        __LINE__, __FILE__, "header array");
                    currentneedle->offsets.headerlens =
                        (size_t *) realloc(currentneedle->offsets.headerlens,
                        sizeof(size_t) *
                        (currentneedle->offsets.numheaders + 100)); //TODO @@@ realloc causes crash when rerun a few times
                    checkMemoryAllocation(state, currentneedle->offsets.headerlens,
                        __LINE__, __FILE__, "header array");

                    currentneedle->offsets.headerstorage =
                        currentneedle->offsets.numheaders + 100;

                    if(state->modeVerbose) {

                        fprintf(stdout,
                            "Memory reallocation performed, total header storage = %" PRIu64 "\n",
                            currentneedle->offsets.headerstorage);

                    }
            }
            currentneedle->offsets.headers[currentneedle->offsets.numheaders -
                1] = startLocation;
            currentneedle->offsets.headerlens[currentneedle->offsets.
                numheaders - 1] =
                foundatlens[needlenum][i];
        }
    }


    // ---------------- threaded footer search ------------------ //
    // ---------------- threaded footer search ------------------ //

    // now footer search, if:
    //
    // there's a footer for the file type AND
    //
    // (at least one header for that type has been previously seen and 
    // at least one header is viable--that is, it was found in the current
    // buffer, or it's less than the max carve distance behind the current
    // file offset
    //
    // OR
    // 
    // a header/footer database is being created.  In this case, ALL headers and
    // footers must be discovered)

    if(state->modeVerbose) {
        printf("Waking up threads for footer searches.\n");
    }

    for(needlenum = 0; needlenum < state->specLines; needlenum++) {
        currentneedle = &(state->SearchSpec[needlenum]);
        if(
            // regular case--want to search for only "viable" (in the sense that they are
            // useful for carving unfragmented files) footers, to save time
            (currentneedle->offsets.numheaders > 0 &&
            currentneedle->endlength &&
            (currentneedle->offsets.
            headers[currentneedle->offsets.numheaders - 1] > offset
            || (offset -
            currentneedle->offsets.headers[currentneedle->offsets.
            numheaders - 1] <
            currentneedle->length))) ||
            // generating header/footer database, need to find all footers
            // BUG:  ALSO need to do this for discovery of fragmented files--document this
            (currentneedle->endlength && state->generateHeaderFooterDatabase)) {
                // # of matches in last element of foundat array
                foundat[needlenum][MAX_MATCHES_PER_BUFFER] = 0;
                threadargs[needlenum].id = needlenum;
                threadargs[needlenum].str = currentneedle->end;
                threadargs[needlenum].length = currentneedle->endlength;
                threadargs[needlenum].startpos = readbuffer;
                threadargs[needlenum].offset = (long)readbuffer + lengthofbuf;
                threadargs[needlenum].foundat = foundat[needlenum];
                threadargs[needlenum].foundatlens = foundatlens[needlenum];
                threadargs[needlenum].strisRE = currentneedle->endisRE;
                if(currentneedle->endisRE) {
                    threadargs[needlenum].regex = &(currentneedle->endstate.re);
                }
                else {
                    threadargs[needlenum].table = currentneedle->endstate.bm_table;
                }
                threadargs[needlenum].casesensitive = currentneedle->casesensitive;
                threadargs[needlenum].nosearchoverlap = state->noSearchOverlap;
                threadargs[needlenum].state = state;

                // unblock thread
                //      sem_post(&workavailable[needlenum]);
                pthread_mutex_unlock(&workavailable[needlenum]);

        }
        else {
            threadargs[needlenum].length = 0;
        }
    }

    if(state->modeVerbose) {
        printf("Waiting for thread group synchronization.\n");
    }

    // ---------- thread group synchronization point ----------- //
    // ---------- thread group synchronization point ----------- //

    // wait for all threads to complete footer search before proceeding
    for(needlenum = 0; needlenum < state->specLines; needlenum++) {
        if(threadargs[needlenum].length > 0) {
            //      sem_wait(&workcomplete[needlenum]);
            pthread_mutex_lock(&workcomplete[needlenum]);
        }
    }

    if(state->modeVerbose) {
        printf("Thread group synchronization complete.\n");
    }

    // digest footer locations discovered by the thread group

    for(needlenum = 0; needlenum < state->specLines; needlenum++) {
        currentneedle = &(state->SearchSpec[needlenum]);
        // number of matches stored in last element of vector
        for(i = 0; i < (long)foundat[needlenum][MAX_MATCHES_PER_BUFFER]; i++) {
            startLocation = offset + (foundat[needlenum][i] - readbuffer);
            if(state->modeVerbose) {

                fprintf(stdout, "A %s footer was found at : %" PRIu64 "\n",
                    currentneedle->suffix,
                    positionUseCoverageBlockmap(state, startLocation));
            }

            currentneedle->offsets.numfooters++;
            if(currentneedle->offsets.footerstorage <=
                currentneedle->offsets.numfooters) {
                    // need more memory for footer offset storage--add an
                    // additional 100 elements
                    currentneedle->offsets.footers = (unsigned long long *)
                        realloc(currentneedle->offsets.footers,
                        sizeof(unsigned long long) *
                        (currentneedle->offsets.numfooters + 100));
                    checkMemoryAllocation(state, currentneedle->offsets.footers,
                        __LINE__, __FILE__, "footer array");
                    currentneedle->offsets.footerlens =
                        (size_t *) realloc(currentneedle->offsets.footerlens,
                        sizeof(size_t) *
                        (currentneedle->offsets.numfooters + 100));
                    checkMemoryAllocation(state, currentneedle->offsets.footerlens,
                        __LINE__, __FILE__, "footer array");
                    currentneedle->offsets.footerstorage =
                        currentneedle->offsets.numfooters + 100;

                    if(state->modeVerbose) {

                        fprintf(stdout,
                            "Memory reallocation performed, total footer storage = %" PRIu64 "\n",
                            currentneedle->offsets.footerstorage);
                    }
            }
            currentneedle->offsets.footers[currentneedle->offsets.numfooters -
                1] = startLocation;
            currentneedle->offsets.footerlens[currentneedle->offsets.
                numfooters - 1] =
                foundatlens[needlenum][i];
        }
    }

#endif // multi-core CPU code
    ///////////////////////////////////////////////////
    ///////////////////////////////////////////////////
    ///////////END MULTI-THREADED SEARCH///////////////
    ///////////////////////////////////////////////////
    ///////////////////////////////////////////////////

    return SCALPEL_OK;
}


////////////////////////////////////////////////////////////////////////////////
/////////////////////// LMIII //////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/* In order to facilitate asynchronous reads, we will set up a reader
   pthread, whose only job is to read the image in SIZE_OF_BUFFER
   chunks using the coverage map functions.

   This thread will buffer QUEUELEN reads.
*/


#ifdef GPU_THREADING
void *gpu_handler(void *sss) {

    struct scalpelState *state = (struct scalpelState *)sss;

    char *fullbuf;

    // Initialize constant table of headers/footers on GPU.  First array
    // element is header # 1, second is footer # 1, third is header # 2, etc.
    // zero-length header marks end of array.

    // Each needle is stored as follows:
    // byte 0: size of needle
    // bytes 1 - sizeof needle: the needle itself
    // byte PATTERN_WCSHIFT: number of leading wildcards
    // byte PATTERN_CASESEN: bool this needle is case sensitive

    // Example: wpc header
    // (one leading wildcard, case sensitive)
    // {\x04?WPC\x00\x00 ... \x00\x01\x01}

    // htm header:
    // (no leading wildcard, not case sensitive)
    // \x05<html\x00\x00 ... \x00\x00\x00}

    // We also create 2 lookup tables for efficiency, one for headers and one for
    // footers. Each is a LOOKUP_ROWS x LOOKUP_COLUMNS array. Each row of the 
    // headers lookup table holds indexes into the h/f table described above of 
    // headers which begin with the array index of this row, eg lookup_headers[7]
    // holds a list of the indexes of the headers whose first byte is \x07.


    // first, build local copies ...
    bzero(localpattern, sizeof(localpattern));
    bzero(locallookup_headers, sizeof(locallookup_headers));
    bzero(locallookup_footers, sizeof(locallookup_footers));
    int i = 0;
    for(i = 0; i < LOOKUP_ROWS; i++) {
        locallookup_headers[i][0] = LOOKUP_ENDOFROW;
        locallookup_footers[i][0] = LOOKUP_ENDOFROW;
    }

    struct SearchSpecLine *currentneedle = 0;
    int j = 0;
    int k = 0;
    for(i = 0; i < state->specLines; i++) {
        currentneedle = &(state->SearchSpec[i]);

        // header/footer initialization for one Scalpel rule

        localpattern[j][0] = currentneedle->beginlength;
        memcpy(&(localpattern[j][1]), currentneedle->begin,
            currentneedle->beginlength);
        localpattern[j][PATTERN_CASESEN] = currentneedle->casesensitive;

        // Here's the rub. Some patterns start with the wildcard. In order for
        // the lookup tables to work out efficiently, we need to be able to skip
        // over leading wildcards, so we keep track of the shift from the
        // beginning of the pattern to the first non-wildcard character.
        int wc_shift = 0;
        while ((localpattern[j][wc_shift + 1] == wildcard)
            && (wc_shift < localpattern[j][0])) {
                wc_shift++;
        }

        // Now we can set up the lookup table to use the first non-wildcard
        // byte of the pattern.
        k = 0;
        while (locallookup_headers[(unsigned char)localpattern[j][wc_shift + 1]][k]
        != LOOKUP_ENDOFROW) {
            k++;
        }
        locallookup_headers[(unsigned char)localpattern[j][wc_shift + 1]][k] = j;
        locallookup_headers[(unsigned char)localpattern[j][wc_shift + 1]][k + 1] =
            LOOKUP_ENDOFROW;

        // And let's not forget to store the shift, we'll need in in the GPU when
        // we search.
        localpattern[j][PATTERN_WCSHIFT] = wc_shift;

        // We never expect a footer to begin with a wildcard -- it makes no sense.
        // BUG: Assume we'll find them anyway.
        localpattern[j + 1][0] = currentneedle->endlength;
        memcpy(&(localpattern[j + 1][1]), currentneedle->end,
            currentneedle->endlength);
        localpattern[j + 1][PATTERN_CASESEN] = currentneedle->casesensitive;
        if(currentneedle->endlength > 0) {
            k = 0;
            while (locallookup_footers[(unsigned char)localpattern[j + 1][1]][k] !=
                LOOKUP_ENDOFROW) {
                    k++;
            }
            locallookup_footers[(unsigned char)localpattern[j + 1][1]][k] = j + 1;
            locallookup_footers[(unsigned char)localpattern[j + 1][1]][k + 1] =
                LOOKUP_ENDOFROW;
        }
        j += 2;
    }
    localpattern[j][0] = 0;	// mark end of array
    localpattern[j + 1][0] = 0;


    // ...then copy to GPU constant memory.  This assignment is persistent across
    // the entire Scalpel execution.
    copytodevicepattern(localpattern);
    copytodevicelookup_headers(locallookup_headers);
    copytodevicelookup_footers(locallookup_footers);

    printf("Copying tables to GPU constant memory complete.\n");

    int longestneedle = findLongestNeedle(state->SearchSpec);
    gpu_init(longestneedle);
    printf("Initializing GPU buffers complete.\n");

    // Now we get buffers from the full_readbuf queue, send them to the GPU for
    // seach, wait till GPU finishes, and put the results buffers into the
    // results_readbuf queue.
    while (!reads_finished) { 
        readbuf_info *rinfo = (readbuf_info *)get(full_readbuf);
        fullbuf = rinfo->readbuf;
        gpuSearchBuffer(fullbuf, (unsigned int)rinfo->bytesread, fullbuf,
            longestneedle, wildcard);
        put(results_readbuf, (void *)rinfo);
    }

    // reads are finished, but there may still be stuff in the queue
    while (!full_readbuf->empty) {
        readbuf_info *rinfo = (readbuf_info *)get(full_readbuf);
        fullbuf = rinfo->readbuf;
        gpuSearchBuffer(fullbuf, (unsigned int)rinfo->bytesread, fullbuf,
            longestneedle, wildcard);
        put(results_readbuf, (void *)rinfo);
    }

    // Done with image.
    gpu_finished = TRUE;
    gpu_cleanup();
    pthread_exit(0);
}
#endif

// Streaming reader gets empty buffers from the empty_readbuf queue, reads 
// SIZE_OF_BUFFER chunks of the input image into the buffers and puts them into
// the full_readbuf queue for processing.
void *streaming_reader(void *sss) {

    struct scalpelState *state = (struct scalpelState *)sss;
    readbuf_info *rinfo = NULL;
    long long filesize = 0, bytesread = 0, filebegin = 0,
        fileposition = 0, beginreadpos = 0;
    long err = SCALPEL_OK;
    int displayUnits = UNITS_BYTES;
    int longestneedle = findLongestNeedle(state->SearchSpec);

    filebegin = scalpelInputTello(state->inReader);
    if((filesize = scalpelInputGetSize(state->inReader)) == -1) {
        fprintf(stderr,
            "ERROR: Couldn't measure size of input: %s\n",
            scalpelInputGetId(state->inReader));
        err=SCALPEL_ERROR_FILE_READ;
        goto exit_reader_thread;
    }

    // Get empty buffer from empty_readbuf queue
    rinfo = (readbuf_info *)get(empty_readbuf);

    // Read chunk of image into empty buffer.
    while ((bytesread =
        fread_use_coverage_map(state, rinfo->readbuf, 1, SIZE_OF_BUFFER,
        state->inReader)) > longestneedle - 1) {

            if(state->modeVerbose) {
                fprintf(stdout, "Read %" PRIu64 " bytes from image file.\n", bytesread);
            }

            if((err = scalpelInputGetError(state->inReader))) {
                err = SCALPEL_ERROR_FILE_READ;      
                goto exit_reader_thread;
            }

            // progress report needs a fileposition that doesn't depend on coverage map
            fileposition = scalpelInputTello(state->inReader);
            displayPosition(&displayUnits, fileposition - filebegin,
                filesize, scalpelInputGetId(state->inReader));

            // if carving is dependent on coverage map, need adjusted fileposition
            fileposition = ftello_use_coverage_map(state, state->inReader);
            beginreadpos = fileposition - bytesread;

            //signal check
            if(signal_caught == SIGTERM || signal_caught == SIGINT) {
                clean_up(state, signal_caught);
            }

            // Put now-full buffer into full_readbuf queue.
            // Note that if the -s option was used we need to adjust the relative begin
            // position 
            rinfo->bytesread = bytesread;
            rinfo->beginreadpos = beginreadpos - state->skip;
            put(full_readbuf, (void *)rinfo);

            // At this point, the host, GPU, whatever can start searching the buffer. 

            // Get another empty buffer.
            rinfo = (readbuf_info *)get(empty_readbuf);

            // move file position back a bit so headers and footers that fall
            // across SIZE_OF_BUFFER boundaries in the image file aren't
            // missed
            fseeko_use_coverage_map(state, state->inReader, -1 * (longestneedle - 1));
    }

exit_reader_thread:
    if (err != SCALPEL_OK) {
        handleError(state, err);
    }

    // Done reading image.
    // get an empty buffer
    rinfo = (readbuf_info *)get(empty_readbuf);
    // mark as end of reads
    rinfo->bytesread = 0;
    rinfo->beginreadpos = 0;
    // put in queue
    put(full_readbuf, (void *)rinfo);

    if (scalpelInputIsOpen(state->inReader)) {
        scalpelInputClose(state->inReader);
    }
    pthread_exit(0);
    return NULL;
}


// Scalpel's approach dictates that this function digAllFiles an image
// file, building the header/footer offset database.  The task of
// extracting files from the image has been moved to carveImageFile(),
// which operates in a second pass over the image.  Digging for
// header/footer values proceeds in SIZE_OF_BUFFER sized chunks of the
// image file.  This buffer is now global and named "readbuffer".
int digImageFile(struct scalpelState *state) {

    int status, err;
    int longestneedle = findLongestNeedle(state->SearchSpec);
    //long long filebegin;
    long long filesize;


    if ((err = setupAuditFile(state)) != SCALPEL_OK) {
        return err;
    }

    if(state->SearchSpec[0].suffix == NULL) {
        return SCALPEL_ERROR_NO_SEARCH_SPEC;
    }

    // open current image file
    int steamOpenErr = 0;
    if((steamOpenErr = scalpelInputOpen(state->inReader)) != 0) {
        return SCALPEL_ERROR_FILE_OPEN;
    }

    // skip initial portion of input file, if that cmd line option
    // was set
    if(state->skip > 0) {
        if(!skipInFile(state, state->inReader)) {
            return SCALPEL_ERROR_FILE_READ;
        }

        // ***GGRIII: want to update coverage bitmap when skip is specified????
    }

    //filebegin = scalpelInputTello(state->inReader); ADAM: not needed
    if((filesize = scalpelInputGetSize(state->inReader)) == -1) {
        fprintf(stderr,
            "ERROR: Couldn't measure size of image file %s\n",
            scalpelInputGetId(state->inReader));
        return SCALPEL_ERROR_FILE_READ;
    }

    // can't process an image file smaller than the longest needle
    if(filesize <= longestneedle * 2) {
        return SCALPEL_ERROR_FILE_TOO_SMALL;
    }


    if(state->modeVerbose) {
        fprintf(stdout, "Total file size is %" PRIu64 " bytes\n", filesize);
    }


    // allocate and initialize coverage bitmap and blockmap, if appropriate
    if((err = setupCoverageMaps(state, filesize)) != SCALPEL_OK) {
        return err;
    }

    // process SIZE_OF_BUFFER-sized chunks of the current image
    // file and look for both headers and footers, recording their
    // offsets for use in the 2nd scalpel phase, when file data will 
    // be extracted.

    fprintf(stdout, "Image file pass 1/2.\n");

    // Create and start the streaming reader thread for this image file.
    pthread_t reader;
    if(pthread_create(&reader, NULL, streaming_reader, (void *)state) != 0) {
        return SCALPEL_ERROR_PTHREAD_FAILURE;
    }

    // wait on reader mutex "reads_begun"


#ifdef GPU_THREADING

    // Create and start the gpu_handler for searches on this image.
    gpu_finished = FALSE;
    pthread_t gpu;
    if(pthread_create(&gpu, NULL, gpu_handler, (void *)state) != 0) {
        return SCALPEL_ERROR_PTHREAD_FAILURE;
    }

    // The reader is now reading in the image, and the GPU is searching the
    // buffers. We get the results buffers and call digBuffer to decode them.
    while (!gpu_finished) {  // || !results_readbuf->empty) {
        readbuf_info *rinfo = (readbuf_info *)get(results_readbuf);
        readbuffer = rinfo->readbuf;
        if((status =
            digBuffer(state, rinfo->bytesread, rinfo->beginreadpos
            )) != SCALPEL_OK) {
                return status;
        }
        put(empty_readbuf, (void *)rinfo);
    }

    // the GPU is finished, but there may still be results buffers to decode
    while (!results_readbuf->empty) {
        readbuf_info *rinfo = (readbuf_info *)get(results_readbuf);
        readbuffer = rinfo->readbuf;
        if((status =
            digBuffer(state, rinfo->bytesread, rinfo->beginreadpos
            )) != SCALPEL_OK) {
                return status;
        }
        put(empty_readbuf, (void *)rinfo);
    }


#endif

#ifdef MULTICORE_THREADING

    // The reader is now reading in chunks of the image. We call digbuffer on
    // these chunks for multi-threaded search. 

    while (1) {

        readbuf_info *rinfo = (readbuf_info *)get(full_readbuf);
        if ((rinfo->bytesread == 0) && (rinfo->beginreadpos == 0)) {
            // end of reads condition - we're done
            break;
        }
        readbuffer = rinfo->readbuf;
        if ((status = digBuffer(state, rinfo->bytesread, 
            rinfo->beginreadpos)) != SCALPEL_OK) {
                return status;
        }
        put(empty_readbuf, (void *)rinfo);
    }

#endif

    return SCALPEL_OK;
}


// carveImageFile() uses the header/footer offsets database
// created by digImageFile() to build a list of files to carve.  These
// files are then carved during a single, sequential pass over the
// image file.  The global 'readbuffer' is used as a buffer in this
// function.

int carveImageFile(struct scalpelState *state) {

    int openErr;
    struct SearchSpecLine *currentneedle;
    struct CarveInfo *carveinfo;
    char fn[MAX_STRING_LENGTH];	// temp buffer for output filename
    char orgdir[MAX_STRING_LENGTH];	// buffer for name of organizing subdirectory
    long long start, stop;	// temp begin/end bytes for file to carve
    unsigned long long prevstopindex;	// tracks index of first 'reasonable' 
    // footer
    int needlenum;
    long long filesize = 0, bytesread = 0, fileposition = 0, filebegin = 0;
    long err = 0;
    int displayUnits = UNITS_BYTES;
    int success = 0;
    long long i, j;
    int halt;
    char chopped;			// file chopped because it exceeds
    // max carve size for type?
    int CURRENTFILESOPEN = 0;	// number of files open (during carve)
    unsigned long long firstcandidatefooter=0;

    // index of header and footer within image file, in SIZE_OF_BUFFER
    // blocks
    unsigned long long headerblockindex, footerblockindex;

    struct Queue *carvelists;	// one entry for each SIZE_OF_BUFFER bytes of
    // input file
    //  struct timeval queuenow, queuethen;

    // open image file and get size so carvelists can be allocated
    if((openErr = scalpelInputOpen(state->inReader)) != 0 ) {
        fprintf(stderr, "ERROR: Couldn't open input file: %s -- %s\n",
            (*(scalpelInputGetId(state->inReader)) == '\0') ? "<blank>"
            : scalpelInputGetId(state->inReader),
            strerror(errno));
        return SCALPEL_ERROR_FILE_OPEN;
    }

    // If skip was activated, then there's no way headers/footers were
    // found there, so skip during the carve operations, too

    if(state->skip > 0) {
        if(!skipInFile(state, state->inReader)) {
            return SCALPEL_ERROR_FILE_READ;
        }
    }

    filebegin = scalpelInputTello(state->inReader);
    if((filesize = scalpelInputGetSize(state->inReader)) == -1) {
        fprintf(stderr,
            "ERROR: Couldn't measure size of image file %s\n",
            scalpelInputGetId(state->inReader));
        return SCALPEL_ERROR_FILE_READ;
    }


    //  gettimeofday(&queuethen, 0);

    // allocate memory for carvelists--we alloc a queue for each
    // SIZE_OF_BUFFER bytes in advance because it's simpler and an empty
    // queue doesn't consume much memory, anyway.

    carvelists =
        (Queue *) malloc(sizeof(Queue) * (2 + (filesize / SIZE_OF_BUFFER)));
    checkMemoryAllocation(state, carvelists, __LINE__, __FILE__, "carvelists");

    // queue associated with each buffer of data holds pointers to
    // CarveInfo structures.

    fprintf(stdout, "Allocating work queues...\n");

    for(i = 0; i < 2 + (filesize / SIZE_OF_BUFFER); i++) {
        init_queue(&carvelists[i], sizeof(struct CarveInfo *), TRUE, 0, TRUE);
    }
    fprintf(stdout, "Work queues allocation complete. Building work queues...\n");

    // build carvelists before 2nd pass over image file

    for(needlenum = 0; needlenum < state->specLines; needlenum++) {

        currentneedle = &(state->SearchSpec[needlenum]);

        // handle each discovered header independently

        prevstopindex = 0;
        for(i = 0; i < (long long)currentneedle->offsets.numheaders; i++) {
            start = currentneedle->offsets.headers[i];

            ////////////// DEBUG ////////////////////////
            //fprintf(stdout, "start: %lu\n", start);

            // block aligned test for "-q"

            if(state->blockAlignedOnly && start % state->alignedblocksize != 0) {
                continue;
            }

            stop = 0;
            chopped = 0;

            // case 1: no footer defined for this file type
            if(!currentneedle->endlength) {

                // this is the unfortunate case--if file type doesn't have a footer,
                // all we can done is carve a block between header position and
                // maximum carve size.
                stop = start + currentneedle->length - 1;
                // these are always considered chopped, because we don't really
                // know the actual size
                chopped = 1;
            }
            else if(currentneedle->searchtype == SEARCHTYPE_FORWARD ||
                currentneedle->searchtype == SEARCHTYPE_FORWARD_NEXT) {
                    // footer defined: use FORWARD or FORWARD_NEXT semantics.
                    // Stop at first occurrence of footer, but for FORWARD,
                    // include the header in the carved file; for FORWARD_NEXT,
                    // don't include footer in carved file.  For FORWARD_NEXT, if
                    // no footer is found, then the maximum carve size for this
                    // file type will be used and carving will proceed.  For
                    // FORWARD, if no footer is found then no carving will be
                    // performed unless -b was specified on the command line.

                    halt = 0;

                    if (state->handleEmbedded && 
                        (currentneedle->searchtype == SEARCHTYPE_FORWARD ||
                        currentneedle->searchtype == SEARCHTYPE_FORWARD_NEXT)) {
                            firstcandidatefooter=adjustForEmbedding(currentneedle, i, &prevstopindex);
                    }
                    else {
                        firstcandidatefooter=prevstopindex;
                    }

                    for (j=firstcandidatefooter;
                        j < (long long)currentneedle->offsets.numfooters && ! halt; j++) {

                            if((long long)currentneedle->offsets.footers[j] <= start) {
                                if (! state->handleEmbedded) {
                                    prevstopindex=j;
                                }

                            }
                            else {
                                halt = 1;
                                stop = currentneedle->offsets.footers[j];

                                if(currentneedle->searchtype == SEARCHTYPE_FORWARD) {
                                    // include footer in carved file
                                    stop += currentneedle->endlength - 1;
                                    // 	BUG? this or above?		    stop += currentneedle->offsets.footerlens[j] - 1;
                                }
                                else {
                                    // FORWARD_NEXT--don't include footer in carved file
                                    stop--;
                                }
                                // sanity check on size of potential file to carve--different
                                // actions depending on FORWARD or FORWARD_NEXT semantics
                                if(stop - start + 1 > (long long)currentneedle->length) {
                                    if(currentneedle->searchtype == SEARCHTYPE_FORWARD) {
                                        // if the user specified -b, then foremost 0.69
                                        // compatibility is desired: carve this file even 
                                        // though the footer wasn't found and indicate
                                        // the file was chopped, in the log.  Otherwise, 
                                        // carve nothing and move on.
                                        if(state->carveWithMissingFooters) {
                                            stop = start + currentneedle->length - 1;
                                            chopped = 1;
                                        }
                                        else {
                                            stop = 0;
                                        }
                                    }
                                    else {
                                        // footer found for FORWARD_NEXT, but distance exceeds
                                        // max carve size for this file type, so use max carve
                                        // size as stop
                                        stop = start + currentneedle->length - 1;
                                        chopped = 1;
                                    }
                                }
                            }
                    }
                    if(!halt &&
                        (currentneedle->searchtype == SEARCHTYPE_FORWARD_NEXT ||
                        (currentneedle->searchtype == SEARCHTYPE_FORWARD &&
                        state->carveWithMissingFooters))) {
                            // no footer found for SEARCHTYPE_FORWARD_NEXT, or no footer
                            // found for SEARCHTYPE_FORWARD and user specified -b, so just use
                            // max carve size for this file type as stop
                            stop = start + currentneedle->length - 1;
                    }
            }
            else {
                // footer defined: use REVERSE semantics: want matching footer
                // as far away from header as possible, within maximum carving
                // size for this file type.  Don't bother to look at footers
                // that can't possibly match a header and remember this info
                // in prevstopindex, as the next headers will be even deeper
                // into the image file.  Footer is included in carved file for
                // this type of carve.
                halt = 0;
                for(j = prevstopindex; j < (long long)currentneedle->offsets.numfooters &&
                    !halt; j++) {
                        if((long long)currentneedle->offsets.footers[j] <= start) {
                            prevstopindex = j;
                        }
                        else if(currentneedle->offsets.footers[j] - start <=
                            currentneedle->length) {
                                stop = currentneedle->offsets.footers[j]
                                + currentneedle->endlength - 1;
                        }
                        else {
                            halt = 1;
                        }
                }
            }

            // if stop <> 0, then we have enough information to set up a
            // file carving operation.  It must pass the minimum carve size
            // test, if currentneedle->minLength != 0.
            //     if(stop) {
            if (stop && (stop - start + 1) >= (long long)currentneedle->minlength) {

                // don't carve past end of image file...
                stop = stop > filesize ? filesize - 1 : stop;

                // find indices (in SIZE_OF_BUFFER units) of header and
                // footer, so the carveinfo can be placed into the right
                // queues.  The priority of each element in a queue allows the
                // appropriate thing to be done (e.g., STARTSTOPCARVE,
                // STARTCARVE, STOPCARVE, CONTINUECARVE).

                headerblockindex = start / SIZE_OF_BUFFER;
                footerblockindex = stop / SIZE_OF_BUFFER;

                // set up a struct CarveInfo for inclusion into the
                // appropriate carvelists

                // generate unique filename for file to carve

                if(state->organizeSubdirectories) {
                    snprintf(orgdir, MAX_STRING_LENGTH, "%s/%s-%d-%1lu",
                        state->outputdirectory,
                        currentneedle->suffix,
                        needlenum, currentneedle->organizeDirNum);
                    if(!state->previewMode) {
#ifdef _WIN32
                        mkdir(orgdir);
#else
                        mkdir(orgdir, 0777);
#endif
                    }
                }
                else {
                    snprintf(orgdir, MAX_STRING_LENGTH, "%s", state->outputdirectory);
                }

                if(state->modeNoSuffix || currentneedle->suffix[0] ==
                    SCALPEL_NOEXTENSION) {
#ifdef _WIN32
                        snprintf(fn, MAX_STRING_LENGTH, "%s/%08I64u",
                            orgdir, state->fileswritten);
#else
                        snprintf(fn, MAX_STRING_LENGTH, "%s/%08llu",
                            orgdir, state->fileswritten);
#endif

                }
                else {
#ifdef _WIN32
                    snprintf(fn, MAX_STRING_LENGTH, "%s/%08I64u.%s",
                        orgdir, state->fileswritten, currentneedle->suffix);
#else
                    snprintf(fn, MAX_STRING_LENGTH, "%s/%08llu.%s",
                        orgdir, state->fileswritten, currentneedle->suffix);
#endif
                }
                state->fileswritten++;
                currentneedle->numfilestocarve++;
                if(currentneedle->numfilestocarve % state->organizeMaxFilesPerSub == 0) {
                    currentneedle->organizeDirNum++;
                }

                carveinfo = (struct CarveInfo *)malloc(sizeof(struct CarveInfo));
                checkMemoryAllocation(state, carveinfo, __LINE__, __FILE__,
                    "carveinfo");

                // remember filename
                carveinfo->filename = (char *)malloc(strlen(fn) + 1);
                checkMemoryAllocation(state, carveinfo->filename, __LINE__,
                    __FILE__, "carveinfo");
                strcpy(carveinfo->filename, fn);
                carveinfo->start = start;
                carveinfo->stop = stop;
                carveinfo->chopped = chopped;

                // fp will be allocated when the first byte of the file is
                // in the current buffer and cleaned up when we encounter the
                // last byte of the file.
                carveinfo->fp = 0;

                if(headerblockindex == footerblockindex) {
                    // header and footer will both appear in the same buffer
                    fprintf(stdout, "Adding %s to queue\n", carveinfo->filename);
                    add_to_queue(&carvelists[headerblockindex],
                        &carveinfo, STARTSTOPCARVE);
                }
                else {
                    // header/footer will appear in different buffers, add carveinfo to 
                    // stop and start lists...
                    fprintf(stdout, "Adding %s to queue\n", carveinfo->filename);
                    add_to_queue(&carvelists[headerblockindex], &carveinfo, STARTCARVE);
                    add_to_queue(&carvelists[footerblockindex], &carveinfo, STOPCARVE);
                    // .. and to all lists in between (these will result in a full
                    // SIZE_OF_BUFFER bytes being carved into the file).  
                    for(j = (long long)headerblockindex + 1; j < (long long)footerblockindex; j++) {
                        add_to_queue(&carvelists[j], &carveinfo, CONTINUECARVE);
                    }
                }
            }
        }
    }

    fprintf(stdout, "Work queues built.  Workload:\n");
    for(needlenum = 0; needlenum < state->specLines; needlenum++) {
        currentneedle = &(state->SearchSpec[needlenum]);
        fprintf(stdout, "%s with header \"", currentneedle->suffix);
        fprintf(stdout, "%s", currentneedle->begintext);
        fprintf(stdout, "\" and footer \"");
        if(currentneedle->end == 0) {
            fprintf(stdout, "NONE");
        }
        else {
            fprintf(stdout, "%s", currentneedle->endtext);
        }

        fprintf(stdout, "\" --> %" PRIu64 " files\n", currentneedle->numfilestocarve);


    }

    if(state->previewMode) {
        fprintf(stdout, "** PREVIEW MODE: GENERATING AUDIT LOG ONLY **\n");
        fprintf(stdout, "** NO CARVED FILES WILL BE WRITTEN **\n");
    }

    fprintf(stdout, "Carving files from image.\n");
    fprintf(stdout, "Image file pass 2/2.\n");

    // now read image file in SIZE_OF_BUFFER-sized windows, writing
    // carved files to output directory

    success = 1;
    while (success) {

        unsigned long long biglseek = 0L;
        // goal: skip reading buffers for which there is no work to do by using one big
        // seek
        fileposition = ftello_use_coverage_map(state, state->inReader);

        while (queue_length(&carvelists[fileposition / SIZE_OF_BUFFER]) == 0
            && success) {
                biglseek += SIZE_OF_BUFFER;
                fileposition += SIZE_OF_BUFFER;
                success = fileposition <= filesize;

        }

        if(success && biglseek) {
            fseeko_use_coverage_map(state, state->inReader, biglseek);
        }

        if(!success) {
            // not an error--just means we've exhausted the image file--show
            // progress report then quit carving
            displayPosition(&displayUnits, filesize, filesize, scalpelInputGetId(state->inReader) );

            continue;
        }

        if(!state->previewMode) {
            bytesread =
                fread_use_coverage_map(state, readbuffer, 1, SIZE_OF_BUFFER, state->inReader);
            // Check for read errors
            if((err = scalpelInputGetError(state->inReader))) {
                return SCALPEL_ERROR_FILE_READ;
            }
            else if(bytesread == 0) {
                // no error, but image file exhausted
                success = 0;
                continue;
            }
        }
        else {
            // in preview mode, seeks are used in the 2nd pass instead of
            // reads.  This isn't optimal, but it's fast enough and avoids
            // complicating the file carving code further.

            fileposition = ftello_use_coverage_map(state, state->inReader);
            fseeko_use_coverage_map(state, state->inReader, SIZE_OF_BUFFER);
            bytesread = ftello_use_coverage_map(state, state->inReader) - fileposition;

            // Check for errors
            if((err = scalpelInputGetError(state->inReader))) {
                return SCALPEL_ERROR_FILE_READ;
            }
            else if(bytesread == 0) {
                // no error, but image file exhausted
                success = 0;
                continue;
            }
        }

        success = 1;

        // progress report needs real file position
        fileposition = scalpelInputTello(state->inReader);
        displayPosition(&displayUnits, fileposition - filebegin,
            filesize, scalpelInputGetId(state->inReader));

        // if using coverage map for carving, need adjusted file position
        fileposition = ftello_use_coverage_map(state, state->inReader);

        // signal check
        if(signal_caught == SIGTERM || signal_caught == SIGINT) {
            clean_up(state, signal_caught);
        }

        // deal with work for this SIZE_OF_BUFFER-sized block by
        // examining the associated queue
        rewind_queue(&carvelists[(fileposition - bytesread) / SIZE_OF_BUFFER]);

        while (!end_of_queue
            (&carvelists[(fileposition - bytesread) / SIZE_OF_BUFFER])) {
                struct CarveInfo *carve;
                int operation;
                unsigned long long bytestowrite = 0, byteswritten = 0, offset = 0;

                peek_at_current(&carvelists
                    [(fileposition - bytesread) / SIZE_OF_BUFFER], &carve);
                operation =
                    current_priority(&carvelists
                    [(fileposition - bytesread) / SIZE_OF_BUFFER]);

                // open file, if beginning of carve operation or file had to be closed
                // previously due to resource limitations
                if(operation == STARTSTOPCARVE ||
                    operation == STARTCARVE || carve->fp == 0) {

                        if(!state->previewMode && state->modeVerbose) {
                            fprintf(stdout, "OPENING %s\n", carve->filename);
                        }

                        carve->fp = (FILE *) 1;
                        if(!state->previewMode) {
                            carve->fp = fopen(carve->filename, "ab");
                        }

                        if(!carve->fp) {
                            fprintf(stderr, "Error opening file: %s -- %s\n",
                                carve->filename, strerror(errno));
                            fprintf(state->auditFile, "Error opening file: %s -- %s\n",
                                carve->filename, strerror(errno));
                            return SCALPEL_ERROR_FILE_WRITE;
                        }
                        else {
                            CURRENTFILESOPEN++;
                        }
                }

                // write some portion of current readbuffer
                switch (operation) {
                case CONTINUECARVE:
                    offset = 0;
                    bytestowrite = SIZE_OF_BUFFER;
                    break;
                case STARTSTOPCARVE:
                    offset = carve->start - (fileposition - bytesread);
                    bytestowrite = carve->stop - carve->start + 1;
                    break;
                case STARTCARVE:
                    offset = carve->start - (fileposition - bytesread);
                    bytestowrite = (carve->stop - carve->start + 1) >
                        (SIZE_OF_BUFFER - offset) ? (SIZE_OF_BUFFER - offset) :
                        (carve->stop - carve->start + 1);
                    break;
                case STOPCARVE:
                    offset = 0;
                    bytestowrite = carve->stop - (fileposition - bytesread) + 1;
                    break;
                }

                if(!state->previewMode) {
                    //	struct timeval writenow, writethen;
                    //	gettimeofday(&writethen, 0);
                    if((byteswritten = fwrite(readbuffer + offset,
                        sizeof(char),
                        bytestowrite, carve->fp)) != bytestowrite) {

                            fprintf(stderr, "Error writing to file: %s -- %s\n",
                                carve->filename, strerror(ferror(carve->fp)));
                            fprintf(state->auditFile,
                                "Error writing to file: %s -- %s\n",
                                carve->filename, strerror(ferror(carve->fp)));
                            return SCALPEL_ERROR_FILE_WRITE;
                    }
                }

                // close file, if necessary.  Always do it on STARTSTOPCARVE and
                // STOPCARVE, but also do it if we have a large number of files
                // open, otherwise we'll run out of available file handles.  Updating the
                // coverage blockmap and auditing is done here, when a file being carved
                // is closed for the last time.
                if(operation == STARTSTOPCARVE ||
                    operation == STOPCARVE || CURRENTFILESOPEN > MAX_FILES_TO_OPEN) {
                        err = 0;
                        if(!state->previewMode) {
                            if(state->modeVerbose) {
                                fprintf(stdout, "CLOSING %s\n", carve->filename);
                            }
                            err = fclose(carve->fp);
                        }

                        if(err) {
                            fprintf(stderr, "Error closing file: %s -- %s\n\n",
                                carve->filename, strerror(ferror(carve->fp)));
                            fprintf(state->auditFile,
                                "Error closing file: %s -- %s\n\n",
                                carve->filename, strerror(ferror(carve->fp)));
                            return SCALPEL_ERROR_FILE_WRITE;
                        }
                        else {
                            CURRENTFILESOPEN--;
                            carve->fp = 0;

                            // release filename buffer if it won't be needed again.  Don't release it
                            // if the file was closed only because a large number of files are currently
                            // open!
                            if(operation == STARTSTOPCARVE || operation == STOPCARVE) {
                                auditUpdateCoverageBlockmap(state, carve);
                                free(carve->filename);
                                carve->filename = NULL;
                            }
                            // free(carve);
                        }
                }
                next_element(&carvelists[(fileposition - bytesread) / SIZE_OF_BUFFER]);
        }
    }

    //  closeFile(infile);
    scalpelInputClose(state->inReader);

    // write header/footer database, if necessary, before 
    // cleanup for current image file.  

    if(state->generateHeaderFooterDatabase) {
        if((err = writeHeaderFooterDatabase(state)) != SCALPEL_OK) {
            return err;
        }
    }

    // tear down coverage maps, if necessary
    destroyCoverageMaps(state);

    printf("Processing of image file complete. Cleaning up...\n");

    // tear down header/footer databases

    for(needlenum = 0; needlenum < state->specLines; needlenum++) {
        currentneedle = &(state->SearchSpec[needlenum]);
        if(currentneedle->offsets.headers) {
            free(currentneedle->offsets.headers);
            currentneedle->offsets.headers = NULL;
        }
        if(currentneedle->offsets.footers) {
            free(currentneedle->offsets.footers);
            currentneedle->offsets.footers = NULL;
        }
        currentneedle->offsets.headers = 0;
        currentneedle->offsets.footers = 0;
        currentneedle->offsets.numheaders = 0;
        currentneedle->offsets.numfooters = 0;
        currentneedle->offsets.headerstorage = 0;
        currentneedle->offsets.footerstorage = 0;
        currentneedle->numfilestocarve = 0;
    }

    // tear down work queues--no memory deallocation for each queue
    // entry required, because memory associated with fp and the
    // filename was freed after the carved file was closed.

    // destroy queues    
    for(i = 0; i < 2 + (filesize / SIZE_OF_BUFFER); i++) {
        rewind_queue(&carvelists[i]);
        
        while (!end_of_queue(&carvelists[i]))
        {
            // We need to free the CarveInfo strutures allocated
            // above. Since these structures may have been added
            // to multiple elements in the queue we want to make 
            // sure we only free them once. That's accomplished 
            // by checking the operation (priority). 
            // The structures will either have been added to a 
            // single STARTSTOPCARVE element or to a STARTCARVE,
            // STOPCARVE and 0 or more CONTINUECARVE elements.
            // We ensure the structures are only freed once by
            // calling free when we come across either a STARTCARVE
            // or STARTSTOPCARVE element.
            switch (current_priority(&carvelists[i]))
            {
                case STARTCARVE:
                case STARTSTOPCARVE:
                {
                    struct CarveInfo *carve;
            
                    peek_at_current(&carvelists[i], &carve);
                    free(carve);
                }
                default:
                    next_element(&carvelists[i]);
            }
        }
        
        destroy_queue(&carvelists[i]);
    }
    // destroy array of queues
    free(carvelists);
    carvelists = NULL;

    printf("Done.");
    return SCALPEL_OK;
}


// The coverage blockmap marks which blocks (of a user-specified size)
// have been "covered" by a carved file.  If the coverage blockmap
// is to be modified, check to see if it exists.  If it does, then
// open it and set the file handle in the Scalpel state.  If it
// doesn't, create a zeroed copy and set the file handle in the
// Scalpel state.  If the coverage blockmap is guiding carving, then
// create the coverage bitmap and initialize it using the coverage
// blockmap file.  The difference between the coverage blockmap (on
// disk) and the coverage bitmap (in memory) is that the blockmap
// counts carved files that cover a block.  The coverage bitmap only
// indicates if ANY carved file covers a block.  'filesize' is the
// size of the image file being examined.

static int
    setupCoverageMaps(struct scalpelState *state, unsigned long long filesize) {

    char fn[MAX_STRING_LENGTH];	// filename for coverage blockmap
    unsigned long long i, k;
    int empty;
    unsigned int blocksize, entry;


    state->coveragebitmap = 0;
    state->coverageblockmap = 0;

    if(!state->useCoverageBlockmap && !state->updateCoverageBlockmap) {
        return SCALPEL_OK;
    }

    fprintf(stdout, "Setting up coverage blockmap.\n");
    // generate pathname for coverage blockmap
    snprintf(fn, MAX_STRING_LENGTH, "%s", state->coveragefile);

    fprintf(stdout, "Coverage blockmap is \"%s\".\n", fn);

    empty = ((state->coverageblockmap = fopen(fn, "rb")) == NULL);
    fprintf(stdout, "Coverage blockmap file is %s.\n",
        (empty ? "EMPTY" : "NOT EMPTY"));

    if(!empty) {
#ifdef _WIN32
        // set binary mode for Win32
        setmode(fileno(state->coverageblockmap), O_BINARY);
#endif
#ifdef __linux
        fcntl(fileno(state->coverageblockmap), F_SETFL, O_LARGEFILE);
#endif

        fprintf(stdout, "Reading blocksize from coverage blockmap file.\n");

        // read block size and make sure it matches user-specified block size
        if(fread(&blocksize, sizeof(unsigned int), 1, state->coverageblockmap) != 1) {
            fprintf(stderr,
                "Error reading coverage blockmap blocksize in\ncoverage blockmap file: %s\n",
                fn);
            fprintf(state->auditFile,
                "Error reading coverage blockmap blocksize in\ncoverage blockmap file: %s\n",
                fn);
            return SCALPEL_ERROR_FATAL_READ;
        }

        if(state->coverageblocksize != 0 && blocksize != state->coverageblocksize) {
            fprintf(stderr,
                "User-specified blocksize does not match blocksize in\ncoverage blockmap file: %s; aborting.\n",
                fn);
            fprintf(state->auditFile,
                "User-specified blocksize does not match blocksize in\ncoverage blockmap file: %s\n",
                fn);
            return SCALPEL_GENERAL_ABORT;
        }

        state->coverageblocksize = blocksize;
        fprintf(stdout, "Blocksize for coverage blockmap is %u.\n",
            state->coverageblocksize);

        state->coveragenumblocks =
            (unsigned long
            long)(ceil((double)filesize / (double)state->coverageblocksize));

        fprintf(stdout, "# of blocks in coverage blockmap is %" PRIu64 ".\n",
            state->coveragenumblocks);


        fprintf(stdout, "Allocating and clearing in-core coverage bitmap.\n");
        // for bitmap, 8 bits per unsigned char, with each bit representing one
        // block
        state->coveragebitmap =
            (unsigned char *)malloc((state->coveragenumblocks / 8) *
            sizeof(unsigned char));
        checkMemoryAllocation(state, state->coveragebitmap, __LINE__, __FILE__,
            "coveragebitmap");

        // zap coverage bitmap 
        for(k = 0; k < state->coveragenumblocks / 8; k++) {
            state->coveragebitmap[k] = 0;
        }

        fprintf(stdout,
            "Reading existing coverage blockmap...this may take a while.\n");

        for(i = 0; i < state->coveragenumblocks; i++) {
            fseeko(state->coverageblockmap, (i + 1) * sizeof(unsigned int), SEEK_SET);
            if(fread(&entry, sizeof(unsigned int), 1, state->coverageblockmap) != 1) {
                fprintf(stderr,
                    "Error reading coverage blockmap entry (blockmap truncated?): %s\n",
                    fn);
                fprintf(state->auditFile,
                    "Error reading coverage blockmap entry (blockmap truncated?): %s\n",
                    fn);
                return SCALPEL_ERROR_FATAL_READ;
            }
            if(entry) {
                state->coveragebitmap[i / 8] |= 1 << (i % 8);
            }
        }
    }
    else if(empty && state->useCoverageBlockmap && !state->updateCoverageBlockmap) {
        fprintf(stderr,
            "-u option requires that the blockmap file %s exist.\n", fn);
        fprintf(state->auditFile,
            "-u option requires that the blockmap file %s exist.\n", fn);
        return SCALPEL_GENERAL_ABORT;
    }
    else {
        // empty coverage blockmap
        if(state->coverageblocksize == 0) {	// user didn't override default
            state->coverageblocksize = 512;
        }
        fprintf(stdout, "Blocksize for coverage blockmap is %u.\n",
            state->coverageblocksize);
        state->coveragenumblocks =
            (unsigned long long)ceil((double)filesize /
            (double)state->coverageblocksize);

        fprintf(stdout, "# of blocks in coverage blockmap is %" PRIu64 ".\n",
            state->coveragenumblocks);

        fprintf(stdout, "Allocating and clearing in-core coverage bitmap.\n");
        // for bitmap, 8 bits per unsigned char, with each bit representing one
        // block
        state->coveragebitmap =
            (unsigned char *)malloc((state->coveragenumblocks / 8) *
            sizeof(unsigned char));
        checkMemoryAllocation(state, state->coveragebitmap, __LINE__, __FILE__,
            "coveragebitmap");

        // zap coverage bitmap 
        for(k = 0; k < state->coveragenumblocks / 8; k++) {
            state->coveragebitmap[k] = 0;
        }
    }

    // change mode to read/write for future updates if coverage blockmap will be updated
    if(state->updateCoverageBlockmap) {
        if(state->modeVerbose) {
            fprintf(stdout, "Changing mode of coverage blockmap file to R/W.\n");
        }

        if(!empty) {
            fclose(state->coverageblockmap);
        }
        if((state->coverageblockmap = fopen(fn, (empty ? "w+b" : "r+b"))) == NULL) {
            fprintf(stderr, "Error writing to coverage blockmap file: %s\n", fn);
            fprintf(state->auditFile,
                "Error writing to coverage blockmap file: %s\n", fn);
            return SCALPEL_ERROR_FILE_WRITE;
        }

#ifdef _WIN32
        // set binary mode for Win32
        setmode(fileno(state->coverageblockmap), O_BINARY);
#endif
#ifdef __linux
        fcntl(fileno(state->coverageblockmap), F_SETFL, O_LARGEFILE);
#endif

        if(empty) {
            // create entries in empty coverage blockmap file
            fprintf(stdout,
                "Writing empty coverage blockmap...this may take a while.\n");
            entry = 0;
            if(fwrite
                (&(state->coverageblocksize), sizeof(unsigned int), 1,
                state->coverageblockmap) != 1) {
                    fprintf(stderr,
                        "Error writing initial entry in coverage blockmap file!\n");
                    fprintf(state->auditFile,
                        "Error writing initial entry in coverage blockmap file!\n");
                    return SCALPEL_ERROR_FILE_WRITE;
            }
            for(k = 0; k < state->coveragenumblocks; k++) {
                if(fwrite
                    (&entry, sizeof(unsigned int), 1, state->coverageblockmap) != 1) {
                        fprintf(stderr, "Error writing to coverage blockmap file!\n");
                        fprintf(state->auditFile,
                            "Error writing to coverage blockmap file!\n");
                        return SCALPEL_ERROR_FILE_WRITE;
                }
            }
        }
    }

    fprintf(stdout, "Finished setting up coverage blockmap.\n");

    return SCALPEL_OK;

}

// map carve->start ... carve->stop into a queue of 'fragments' that
// define a carved file in the disk image.  
static void
    generateFragments(struct scalpelState *state, Queue * fragments,
    CarveInfo * carve) {

    unsigned long long curblock, neededbytes =
        carve->stop - carve->start + 1, bytestoskip, morebytes, totalbytes =
        0, curpos;

    Fragment frag;


    init_queue(fragments, sizeof(struct Fragment), TRUE, 0, TRUE);

    if(!state->useCoverageBlockmap) {
        // no translation necessary
        frag.start = carve->start;
        frag.stop = carve->stop;
        add_to_queue(fragments, &frag, 0);
        return;
    }
    else {
        curpos = positionUseCoverageBlockmap(state, carve->start);
        curblock = curpos / state->coverageblocksize;

        while (totalbytes < neededbytes && curblock < state->coveragenumblocks) {

            morebytes = 0;
            bytestoskip = 0;

            // skip covered blocks
            while (curblock < state->coveragenumblocks &&
                (state->coveragebitmap[curblock / 8] & (1 << (curblock % 8)))) {
                    bytestoskip += state->coverageblocksize -
                        curpos % state->coverageblocksize;
                    curblock++;
            }

            curpos += bytestoskip;

            // accumulate uncovered blocks in fragment
            while (curblock < state->coveragenumblocks &&
                ((state->
                coveragebitmap[curblock / 8] & (1 << (curblock % 8))) == 0)
                && totalbytes + morebytes < neededbytes) {

                    morebytes += state->coverageblocksize -
                        curpos % state->coverageblocksize;

                    curblock++;
            }

            // cap size
            if(totalbytes + morebytes > neededbytes) {
                morebytes = neededbytes - totalbytes;
            }

            frag.start = curpos;
            curpos += morebytes;
            frag.stop = curpos - 1;
            totalbytes += morebytes;

            add_to_queue(fragments, &frag, 0);
        }
    }
}


// If the coverage blockmap is used to guide carving, then use the
// coverage blockmap to map a logical index in the disk image (i.e.,
// the index skips covered blocks) to an actual disk image index.  If
// the coverage blockmap isn't being used, just returns the second
// argument.  
//
// ***This function assumes that the 'position' does NOT lie
// within a covered block! ***
static unsigned long long
    positionUseCoverageBlockmap(struct scalpelState *state,
    unsigned long long position) {

    unsigned long long totalbytes = 0, neededbytes = position,
        morebytes, curblock = 0, curpos = 0, bytestoskip;

    if(!state->useCoverageBlockmap) {
        return position;
    }
    else {
        while (totalbytes < neededbytes && curblock < state->coveragenumblocks) {
            morebytes = 0;
            bytestoskip = 0;

            // skip covered blocks
            while (curblock < state->coveragenumblocks &&
                (state->coveragebitmap[curblock / 8] & (1 << (curblock % 8)))) {
                    bytestoskip += state->coverageblocksize -
                        curpos % state->coverageblocksize;
                    curblock++;
            }

            curpos += bytestoskip;

            // accumulate uncovered blocks
            while (curblock < state->coveragenumblocks &&
                ((state->
                coveragebitmap[curblock / 8] & (1 << (curblock % 8))) == 0)
                && totalbytes + morebytes < neededbytes) {

                    morebytes += state->coverageblocksize -
                        curpos % state->coverageblocksize;
                    curblock++;
            }

            // cap size
            if(totalbytes + morebytes > neededbytes) {
                morebytes = neededbytes - totalbytes;
            }

            curpos += morebytes;
            totalbytes += morebytes;
        }

        return curpos;
    }
}


// update the coverage blockmap for a carved file (if appropriate) and write entries into
// the audit log describing the carved file.  If the file is fragmented, then multiple
// lines are written to indicate where the fragments occur. 
static int
    auditUpdateCoverageBlockmap(struct scalpelState *state,
                                struct CarveInfo *carve) {

    struct Queue fragments;
    Fragment *frag;
    int err;
    unsigned long long k;

    // If the coverage blockmap used to guide carving, then carve->start and
    // carve->stop may not correspond to addresses in the disk image--the coverage blockmap
    // processing layer in Scalpel may have skipped "in use" blocks.  Transform carve->start
    // and carve->stop into a list of fragments that contain real disk image offsets.
    generateFragments(state, &fragments, carve);

    rewind_queue(&fragments);
    while (!end_of_queue(&fragments)) {
        frag = (Fragment *) pointer_to_current(&fragments);
        fprintf(state->auditFile, "%s", base_name(carve->filename));
#ifdef _WIN32
        fprintf(state->auditFile, "%13I64u\t\t", frag->start);
#else
        fprintf(state->auditFile, "%13llu\t\t", frag->start);
#endif

        fprintf(state->auditFile, "%3s", carve->chopped ? "YES   " : "NO    ");

#ifdef _WIN32
        fprintf(state->auditFile, "%13I64u\t\t", frag->stop - frag->start + 1);
#else
        fprintf(state->auditFile, "%13llu\t\t", frag->stop - frag->start + 1);
#endif

        fprintf(state->auditFile, "%s\n", base_name(scalpelInputGetId(state->inReader)));

        fflush(state->auditFile);

        // update coverage blockmap, if appropriate
        if(state->updateCoverageBlockmap) {
            for(k = frag->start / state->coverageblocksize;
                k <= frag->stop / state->coverageblocksize; k++) {
                    if((err = updateCoverageBlockmap(state, k)) != SCALPEL_OK) {
                        destroy_queue(&fragments);
                        return err;
                    }
            }
        }
        next_element(&fragments);
    }

    destroy_queue(&fragments);

    return SCALPEL_OK;
}


static int
    updateCoverageBlockmap(struct scalpelState *state, unsigned long long block) {

    unsigned int entry;

    if(state->updateCoverageBlockmap) {
        // first entry in file is block size, so seek one unsigned int further
        fseeko(state->coverageblockmap, (block + 1) * sizeof(unsigned int),
            SEEK_SET);
        if(fread(&entry, sizeof(unsigned int), 1, state->coverageblockmap) != 1) {
            fprintf(stderr, "Error reading coverage blockmap entry!\n");
            fprintf(state->auditFile, "Error reading coverage blockmap entry!\n");
            return SCALPEL_ERROR_FATAL_READ;
        }
        entry++;
        // first entry in file is block size, so seek one unsigned int further 
        fseeko(state->coverageblockmap, (block + 1) * sizeof(unsigned int),
            SEEK_SET);
        if(fwrite(&entry, sizeof(unsigned int), 1, state->coverageblockmap)
            != 1) {
                fprintf(stderr, "Error writing to coverage blockmap file!\n");
                fprintf(state->auditFile, "Error writing to coverage blockmap file!\n");
                return SCALPEL_ERROR_FILE_WRITE;
        }
    }

    return SCALPEL_OK;
}



static void destroyCoverageMaps(struct scalpelState *state) {

    //  memory associated with coverage bitmap, close coverage blockmap file

    if(state->coveragebitmap) {
        free(state->coveragebitmap);
        state->coveragebitmap = NULL;
    }

    if(state->useCoverageBlockmap || state->updateCoverageBlockmap) {
        fclose(state->coverageblockmap);
        state->coverageblockmap = NULL;
    }
}


// simple wrapper for fseeko with SEEK_CUR semantics that uses the
// coverage bitmap to skip over covered blocks, IF the coverage
// blockmap is being used.  The offset is adjusted so that covered
// blocks are silently skipped when seeking if the coverage blockmap
// is used, otherwise an fseeko() with an umodified offset is
// performed.
static int
fseeko_use_coverage_map(struct scalpelState *state, ScalpelInputReader * const inReader, off64_t offset) {

    off64_t currentpos;

    // BUG TEST: revision 5 changed curbloc to unsigned, removed all tests for 
    // curblock >= 0 from each of next three while loops
    // we should put back guards to make sure we don't underflow
    // what did I break? anything? maybe not?

    unsigned long long curblock, bytestoskip, bytestokeep, totalbytes = 0;
    int sign;

    if(state->useCoverageBlockmap) {
        currentpos = scalpelInputTello(state->inReader);
        sign = (offset > 0 ? 1 : -1);

        curblock = currentpos / state->coverageblocksize;

        while (totalbytes < (offset > 0 ? offset : offset * -1) &&
            curblock < state->coveragenumblocks) {
                bytestoskip = 0;

                // covered blocks increase offset
                while (curblock < state->coveragenumblocks &&
                    (state->coveragebitmap[curblock / 8] & (1 << (curblock % 8)))) {

                        bytestoskip += (state->coverageblocksize -
                            currentpos % state->coverageblocksize);
                        curblock += sign;
                }

                offset += (bytestoskip * sign);
                currentpos += (bytestoskip * sign);

                bytestokeep = 0;

                // uncovered blocks don't increase offset
                while (curblock < state->coveragenumblocks &&
                    ((state->
                    coveragebitmap[curblock / 8] & (1 << (curblock % 8))) == 0)
                    && totalbytes < (offset > 0 ? offset : offset * -1)) {

                        bytestokeep += (state->coverageblocksize -
                            currentpos % state->coverageblocksize);

                        curblock += sign;
                }

                totalbytes += bytestokeep;
                currentpos += (bytestokeep * sign);
        }
    }

    return scalpelInputSeeko(state->inReader, offset, SCALPEL_SEEK_CUR);
}


// simple wrapper for ftello() that uses the coverage bitmap to
// report the current file position *minus* the contribution of
// marked blocks, IF the coverage blockmap is being used.  If a
// coverage blockmap isn't in use, just performs a standard ftello()
// call.
//
// GGRIII:  *** This could use optimization, e.g., use of a pre-computed
// table to avoid walking the coverage bitmap on each call.

static off64_t ftello_use_coverage_map(struct scalpelState *state, ScalpelInputReader * const inReader) {

    off64_t currentpos, decrease = 0;
    unsigned long long endblock, k;

    currentpos = scalpelInputTello(state->inReader);

    if(state->useCoverageBlockmap) {
        endblock = currentpos / state->coverageblocksize;

        // covered blocks don't contribute to current file position
        for(k = 0; k <= endblock; k++) {
            if(state->coveragebitmap[k / 8] & (1 << (k % 8))) {
                decrease += state->coverageblocksize;
            }
        }

        if(state->coveragebitmap[endblock / 8] & (1 << (endblock % 8))) {
            decrease += (state->coverageblocksize -
                currentpos % state->coverageblocksize);
        }

        if(state->modeVerbose && state->useCoverageBlockmap) {
            fprintf(stdout,
                "Coverage map decreased current file position by %" PRIu64 " bytes.\n",
                (unsigned long long)decrease);
        }
    }

    return currentpos - decrease;
}

// simple wrapper for fread() that uses the coverage bitmap--the read silently
// skips blocks that are marked covered (corresponding bit in coverage
// bitmap is 1)
static size_t
fread_use_coverage_map(struct scalpelState *state, void *ptr,
                       size_t size, size_t nmemb, ScalpelInputReader * const inReader) {

    unsigned long long curblock, neededbytes = nmemb * size, bytestoskip,
        bytestoread, bytesread, totalbytesread = 0, curpos;
    int shortread;
    //  gettimeofday_t readnow, readthen;

    //  gettimeofday(&readthen, 0);

    if(state->useCoverageBlockmap) {
        if(state->modeVerbose) {
            fprintf(stdout,
                "Issuing coverage map-based READ, wants %" PRIu64 " bytes.\n",
                neededbytes);
        }

        curpos = scalpelInputTello(inReader);
        curblock = curpos / state->coverageblocksize;
        shortread = 0;

        while (totalbytesread < neededbytes
            && curblock < state->coveragenumblocks && !shortread) {
                bytestoread = 0;
                bytestoskip = 0;

                // skip covered blocks
                while (curblock < state->coveragenumblocks &&
                    (state->coveragebitmap[curblock / 8] & (1 << (curblock % 8)))) {
                        bytestoskip += (state->coverageblocksize -
                            curpos % state->coverageblocksize);
                        curblock++;
                }

                curpos += bytestoskip;


                if(state->modeVerbose) {
                    fprintf(stdout,
                        "fread using coverage map to skip %" PRIu64 " bytes.\n", bytestoskip);
                }

                scalpelInputSeeko(inReader, (off64_t) bytestoskip, SCALPEL_SEEK_CUR);

                // accumulate uncovered blocks for read
                while (curblock < state->coveragenumblocks &&
                    ((state->
                    coveragebitmap[curblock / 8] & (1 << (curblock % 8))) == 0)
                    && totalbytesread + bytestoread <= neededbytes) {

                        bytestoread += (state->coverageblocksize -
                            curpos % state->coverageblocksize);

                        curblock++;
                }

                // cap read size
                if(totalbytesread + bytestoread > neededbytes) {
                    bytestoread = neededbytes - totalbytesread;
                }


                if(state->modeVerbose) {
                    fprintf(stdout,
                        "fread using coverage map found %" PRIu64 " consecutive bytes.\n",
                        bytestoread);
                }

                if((bytesread =
                    scalpelInputRead(state->inReader, (char *)ptr + totalbytesread, 1, (size_t) bytestoread))
                    < bytestoread)
                {
                    shortread = 1;
                }

                totalbytesread += bytesread;
                curpos += bytestoread;

                if(state->modeVerbose) {
                    fprintf(stdout, "fread using coverage map read %" PRIu64 " bytes.\n",
                        bytesread);
                }
        }

        if(state->modeVerbose) {
            fprintf(stdout, "Coverage map-based READ complete.\n");
        }

        // conform with fread() semantics by returning # of items read
        return totalbytesread / size;
    }
    else {
        size_t ret = scalpelInputRead(state->inReader, ptr, size, nmemb);
        return ret;
    }
}

#ifdef MULTICORE_THREADING

// threaded header/footer search
static void *threadedFindAll(void *args) {

    int id = ((ThreadFindAllParams *) args)->id;
    char *str;
    size_t length;
    char *startpos;
    long offset;
    char **foundat;
    size_t *foundatlens;
    size_t *table = 0;
    regex_t *regexp = 0;
    int strisRE;
    int casesensitive;
    int nosearchoverlap;
    struct scalpelState *state;

    regmatch_t *match;

    // wait for work
    //  sem_wait(&workavailable[id]);

    // you need to be holding the workcomplete mutex initially
    pthread_mutex_lock(&workcomplete[id]);
    pthread_mutex_lock(&workavailable[id]);

    while (1) {
        // get args that define current workload
        str = ((ThreadFindAllParams *) args)->str;
        length = ((ThreadFindAllParams *) args)->length;
        startpos = ((ThreadFindAllParams *) args)->startpos;
        offset = ((ThreadFindAllParams *) args)->offset;
        foundat = ((ThreadFindAllParams *) args)->foundat;
        foundatlens = ((ThreadFindAllParams *) args)->foundatlens;
        strisRE = ((ThreadFindAllParams *) args)->strisRE;
        if(strisRE) {
            regexp = ((ThreadFindAllParams *) args)->regex;
        }
        else {
            table = ((ThreadFindAllParams *) args)->table;
        }
        casesensitive = ((ThreadFindAllParams *) args)->casesensitive;
        nosearchoverlap = ((ThreadFindAllParams *) args)->nosearchoverlap;
        state = ((ThreadFindAllParams *) args)->state;

        if(state->modeVerbose) {
            printf("needle search thread # %d awake.\n", id);
        }

        while (startpos) {
            if(!strisRE) {
                startpos = bm_needleinhaystack(str,
                    length,
                    startpos,
                    offset - (long)startpos,
                    table, casesensitive);
            }
            else {
                //printf("Before regexp search, startpos = %p\n", startpos);
                match = re_needleinhaystack(regexp, startpos, offset - (long)startpos);
                if(!match) {
                    startpos = 0;
                }
                else {
                    startpos = match->rm_so + startpos;
                    length = match->rm_eo - match->rm_so;
                    free(match);
                    match = NULL;
                    //printf("After regexp search, startpos = %p\n", startpos);
                }
            }

            if(startpos) {
                // remember match location
                foundat[(long)(foundat[MAX_MATCHES_PER_BUFFER])] = startpos;
                foundatlens[(long)(foundat[MAX_MATCHES_PER_BUFFER])] = length;
                foundat[MAX_MATCHES_PER_BUFFER]++;

                // move past match position.  Foremost 0.69 didn't find overlapping
                // headers/footers.  If you need that behavior, specify "-r" on the
                // command line.  Scalpel's default behavior is to find overlapping
                // headers/footers.

                if(nosearchoverlap) {
                    startpos += length;
                }
                else {
                    startpos++;
                }
            }
        }

        if(state->modeVerbose) {
            printf("needle search thread # %d asleep.\n", id);
        }

        // signal completion of work
        //    sem_post(&workcomplete[id]);
        pthread_mutex_unlock(&workcomplete[id]);

        // wait for more work
        //    sem_wait(&workavailable[id]);
        pthread_mutex_lock(&workavailable[id]);

    }

    return 0;

}

#endif


// Buffers for reading image in and holding gpu results.
// The ourCudaMallocHost call MUST be executed by the 
// gpu_handler thread.
void init_store() {

    // 3 queues:
    //              inque filled by reader, emptied by gpu_handler
    //              outque filled by gpu_handler, emptied by host thread
    //              freequeue filled by host, emptied by reader

    // the queues hold pointers to full and empty SIZE_OF_BUFFER read buffers 
    full_readbuf = syncqueue_init("full_readbuf", QUEUELEN);
    empty_readbuf = syncqueue_init("empty_readbuf", QUEUELEN);
#ifdef GPU_THREADING
    results_readbuf = syncqueue_init("results_readbuf", QUEUELEN);
#endif

    // backing store of actual buffers pointed to above, along with some  
    // necessary bookeeping info
    //readbuf_info *readbuf_store;
    if((readbuf_store =
        (readbuf_info *)malloc(QUEUELEN * sizeof(readbuf_info))) == 0) {
            fprintf(stderr, (char *)"malloc %lu failed in streaming reader\n",
                (unsigned long)QUEUELEN * sizeof(readbuf_info));
    }

    // initialization
    int g;
    for(g = 0; g < QUEUELEN; g++) {
        readbuf_store[g].bytesread = 0;
        readbuf_store[g].beginreadpos = 0;

        // for fast gpu operation we need to use the CUDA pinned-memory allocations
#ifdef GPU_THREADING
        ourCudaMallocHost((void **)&(readbuf_store[g].readbuf), SIZE_OF_BUFFER);
#else
        readbuf_store[g].readbuf = (char *)malloc(SIZE_OF_BUFFER);
#endif

        // put pointer to empty but initialized readbuf in the empty que        
        put(empty_readbuf, (void *)(&readbuf_store[g]));
    }
}


//free storage initialized in init_store()
void destroyStore() {
    if (full_readbuf) {
        syncqueue_destroy(full_readbuf);
        full_readbuf = NULL;
    }

    if (empty_readbuf) {
        syncqueue_destroy(empty_readbuf);
        empty_readbuf = NULL;
    }

    if (readbuf_store) {
        for(int g = 0; g < QUEUELEN; g++) {

#ifdef GPU_THREADING
            //TODO free this ourCudaMallocHost((void **)&(readbuf_store[g].readbuf), SIZE_OF_BUFFER);
#else
            free(readbuf_store[g].readbuf);
            readbuf_store[g].readbuf = NULL;
#endif
        }
        free(readbuf_store);
        readbuf_store = NULL;
    }
}


// initialize thread-related data structures for either GPU_THREADING or 
// MULTICORE_THREADING models
int init_threading_model(struct scalpelState *state) {

    int i;

#ifdef GPU_THREADING

    printf("GPU-based threading model enabled.\n");

#endif

#ifdef MULTICORE_THREADING

    printf("Multi-core CPU threading model enabled.\n");
    printf("Initializing thread group data structures.\n");

    // initialize global data structures for threads
    searchthreads = (pthread_t *) malloc(state->specLines * sizeof(pthread_t));
    checkMemoryAllocation(state, searchthreads, __LINE__, __FILE__,
        "searchthreads");
    threadargs =
        (ThreadFindAllParams *) malloc(state->specLines *
        sizeof(ThreadFindAllParams));
    checkMemoryAllocation(state, threadargs, __LINE__, __FILE__, "args");
    foundat = (char ***)malloc(state->specLines * sizeof(char *));
    checkMemoryAllocation(state, foundat, __LINE__, __FILE__, "foundat");
    foundatlens = (size_t **) malloc(state->specLines * sizeof(size_t));
    checkMemoryAllocation(state, foundatlens, __LINE__, __FILE__, "foundatlens");
    workavailable = (pthread_mutex_t *)malloc(state->specLines * sizeof(pthread_mutex_t));

    checkMemoryAllocation(state, workavailable, __LINE__, __FILE__,
        "workavailable");
    workcomplete = (pthread_mutex_t *)malloc(state->specLines * sizeof(pthread_mutex_t));

    checkMemoryAllocation(state, workcomplete, __LINE__, __FILE__,
        "workcomplete");

    printf("Creating threads...\n");
    for(i = 0; i < state->specLines; i++) {
        foundat[i] = (char **)malloc((MAX_MATCHES_PER_BUFFER + 1) * sizeof(char *));
        checkMemoryAllocation(state, foundat[i], __LINE__, __FILE__, "foundat");
        foundatlens[i] = (size_t *) malloc(MAX_MATCHES_PER_BUFFER * sizeof(size_t));
        checkMemoryAllocation(state, foundatlens[i], __LINE__, __FILE__,
            "foundatlens");
        foundat[i][MAX_MATCHES_PER_BUFFER] = 0;

        // BUG:  NEED PROPER ERROR CODE/MESSAGE FOR MX CREATION FAILURE
        if(pthread_mutex_init(&workavailable[i], 0)) {
            //return SCALPEL_ERROR_PTHREAD_FAILURE;
            std::string msg ("COULDN'T CREATE MUTEX\n");
            fprintf(stderr, "%s", msg.c_str());
            throw std::runtime_error(msg);
        }

        pthread_mutex_lock(&workavailable[i]);

        if(pthread_mutex_init(&workcomplete[i], 0)) {
            std::string msg ("COULDN'T CREATE MUTEX\n");
            fprintf(stderr, "%s", msg.c_str());
            throw std::runtime_error(msg);
        }		

        // create a thread in the thread pool; thread will block on workavailable[] 
        // semaphore until there's work to do

        // FIX:  NEED PROPER ERROR CODE/MESSAGE FOR THREAD CREATION FAILURE
        threadargs[i].id = i;	// thread needs to read id before blocking
        if(pthread_create
            (&searchthreads[i], NULL, &threadedFindAll, &threadargs[i])) {
                //return SCALPEL_ERROR_PTHREAD_FAILURE;
                std::string msg ("COULDN'T CREATE THREAD\n");
                fprintf(stderr, "%s", msg.c_str());
                throw std::runtime_error(msg);
        }
    }
    printf("Thread creation completed.\n");

#endif

    return 0;
}

void destroy_threading_model(struct scalpelState *state) {

#ifdef MULTICORE_THREADING
    for(int i = 0; i < state->specLines; i++) {

        if (foundat) {
            free(foundat[i]);
            foundat[i]= NULL;
        }

        if (foundatlens) {
            free(foundatlens[i]);
            foundatlens[i] = NULL;
        }

        if (workavailable) {
            pthread_mutex_destroy(&workavailable[i]);
        }
        if (workcomplete) {
            pthread_mutex_destroy(&workcomplete[i]);
        }        
    }

    if (workcomplete) {
        free(workcomplete);
        workcomplete = NULL;
    }
    if (workavailable) {
        free(workavailable);
        workavailable = NULL;
    }

    if (foundatlens) {
        free(foundatlens);
        foundatlens = NULL;
    }
    if (foundat) {
        free(foundat);
        foundat = NULL;
    }
    if (threadargs) {
        free(threadargs);
        threadargs = NULL;
    }
    if (searchthreads) {
        free (searchthreads);
        searchthreads = NULL;
    }

#endif

}

// write header/footer database for current image file into the
// Scalpel output directory. No information is written into the
// database for file types without a suffix.  The filename used
// is the current image filename with ".hfd" appended.  The 
// format of the database file is straightforward:
//
// suffix_#1 (string)
// number_of_headers (unsigned long long)
// header_pos_#1 (unsigned long long)
// header_pos_#2 (unsigned long long) 
// ...
// number_of_footers (unsigned long long)
// footer_pos_#1 (unsigned long long)
// footer_pos_#2 (unsigned long long) 
// ...
// suffix_#2 (string)
// number_of_headers (unsigned long long)
// header_pos_#1 (unsigned long long)
// header_pos_#2 (unsigned long long) 
// ...
// number_of_footers (unsigned long long)
// footer_pos_#1 (unsigned long long)
// footer_pos_#2 (unsigned long long) 
// ...
// ...
//
// If state->useCoverageBlockmap, then translation is required to
// produce real disk image addresses for the generated header/footer
// database file, because the Scalpel carving engine isn't aware of
// gaps created by blocks that are covered by previously carved files.

static int writeHeaderFooterDatabase(struct scalpelState *state) {

    FILE *dbfile;
    char fn[MAX_STRING_LENGTH];	// filename for header/footer database
    int needlenum;
    struct SearchSpecLine *currentneedle;
    unsigned long long i;

    // generate unique name for header/footer database
    snprintf(fn, MAX_STRING_LENGTH, "%s/%s.hfd",
        state->outputdirectory, base_name(scalpelInputGetId(state->inReader)));

    if((dbfile = fopen(fn, "w")) == NULL) {
        fprintf(stderr, "Error writing to header/footer database file: %s\n", fn);
        fprintf(state->auditFile,
            "Error writing to header/footer database file: %s\n", fn);
        return SCALPEL_ERROR_FILE_WRITE;
    }

#ifdef _WIN32
    // set binary mode for Win32
    setmode(fileno(dbfile), O_BINARY);
#endif
#ifdef __linux
    fcntl(fileno(dbfile), F_SETFL, O_LARGEFILE);
#endif

    for(needlenum = 0; needlenum < state->specLines; needlenum++) {
        currentneedle = &(state->SearchSpec[needlenum]);

        if(currentneedle->suffix[0] != SCALPEL_NOEXTENSION) {
            // output current suffix
            if(fprintf(dbfile, "%s\n", currentneedle->suffix) <= 0) {
                fprintf(stderr,
                    "Error writing to header/footer database file: %s\n", fn);
                fprintf(state->auditFile,
                    "Error writing to header/footer database file: %s\n", fn);
                return SCALPEL_ERROR_FILE_WRITE;
            }

            // # of headers
            if(fprintf(dbfile, "%" PRIu64 "\n", currentneedle->offsets.numheaders)
                <= 0) {

                    fprintf(stderr,
                        "Error writing to header/footer database file: %s\n", fn);
                    fprintf(state->auditFile,
                        "Error writing to header/footer database file: %s\n", fn);
                    return SCALPEL_ERROR_FILE_WRITE;
            }

            // all header positions for current suffix
            for(i = 0; i < currentneedle->offsets.numheaders; i++) {
#ifdef _WIN32
                if(fprintf
                    (dbfile, "%" PRIu64 "\n",
                    positionUseCoverageBlockmap(state,
                    currentneedle->offsets.
                    headers[i])) <= 0) {
#else
                if(fprintf
                    (dbfile, "%llu\n",
                    positionUseCoverageBlockmap(state,
                    currentneedle->offsets.
                    headers[i])) <= 0) {
#endif
                        fprintf(stderr,
                            "Error writing to header/footer database file: %s\n", fn);
                        fprintf(state->auditFile,
                            "Error writing to header/footer database file: %s\n", fn);
                        return SCALPEL_ERROR_FILE_WRITE;
                }
            }

            // # of footers
            if(fprintf(dbfile, "%" PRIu64 "\n", currentneedle->offsets.numfooters)
                <= 0) {
                    fprintf(stderr,
                        "Error writing to header/footer database file: %s\n", fn);
                    fprintf(state->auditFile,
                        "Error writing to header/footer database file: %s\n", fn);
                    return SCALPEL_ERROR_FILE_WRITE;
            }

            // all footer positions for current suffix
            for(i = 0; i < currentneedle->offsets.numfooters; i++) {
                if(fprintf
                    (dbfile, "%" PRIu64 "\n",
                    positionUseCoverageBlockmap(state,
                    currentneedle->offsets.
                    footers[i])) <= 0) {

                        fprintf(stderr,
                            "Error writing to header/footer database file: %s\n", fn);
                        fprintf(state->auditFile,
                            "Error writing to header/footer database file: %s\n", fn);
                        return SCALPEL_ERROR_FILE_WRITE;
                }
            }
        }
    }
    fclose(dbfile);
    return SCALPEL_OK;
}
	
