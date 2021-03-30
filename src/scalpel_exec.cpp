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

//executable scalpel program

#include "scalpel.h"

static void registerSignalHandlers();
static void catch_alarm(int signum);
static void digAllFiles(char **argv, struct scalpelState *state);
static void processCommandLineArgs(int argc, char **argv,
		struct scalpelState *state);
static void usage();

int main(int argc, char **argv) {

    time_t starttime = time(0);
    struct scalpelState state;

    ///////// JUST  A TEST FOR WIN32 TSK LINKAGE ///////////
    //      TSK_TCHAR *imgs=_TSK_T("c:\\128MB.dd");
    //      TSK_IMG_INFO *img = tsk_img_open(_TSK_T("raw"), 1, (const TSK_TCHAR **)&imgs);
    //      printf("TSK size is %d bytes\n", (int)img->size);
    ///////// END JUST  A TEST FOR WIN32 TSK LINKAGE ///////////

    if (ldiv(SIZE_OF_BUFFER, SCALPEL_BLOCK_SIZE).rem != 0) {
        fprintf(stderr, SCALPEL_SIZEOFBUFFER_PANIC_STRING);
        exit(-1);
    }

    fprintf(stdout, SCALPEL_BANNER_STRING);

    try {
        initializeState(argv, &state);

        registerSignalHandlers();

        processCommandLineArgs(argc, argv, &state);
        convertFileNames(&state);
    } catch (std::runtime_error &e) {
        fprintf(stderr, "Error initializing: %s\n", e.what());
        freeState(&state);
        exit (-1);
    }
    catch (...) {
        fprintf(stderr, "Unexpected error initializing\n");
        freeState(&state);
        exit (-1);
    }

    // read configuration file
    int err;
    if ((err = readSearchSpecFile(&state))) {
        // problem with config file
        try {
            handleError(&state, err);
        } catch (...) {
            fprintf(stderr, "Unexpected error while reading search spec file. \n");
            freeState(&state);
            exit(-1);
        }
    }

    setttywidth();

    argv += optind;
    if (*argv != NULL || state.useInputFileList) {
        // prepare audit file and make sure output directory is empty.
        int err;
        if ((err = openAuditFile(&state))) {
            try {
                handleError(&state, err);
            } catch (std::runtime_error &e) {
                fprintf(stderr, "Error opening audit file: %s\n", e.what());
                freeState(&state);
                exit(-1);
            }
            catch (...) {
                fprintf(stderr, "Unexpected error opening audit file\n");
                freeState(&state);
                exit (-1);
            }

        }

        // Initialize the backing store of buffer to read-in, process image data.
        try {
            init_store();
        } catch (...) {
            fprintf(stderr, "Unexpected error initializing store\n");
            freeState(&state);
            exit (-1);
        }

        // Initialize threading model for cpu or gpu search.
        try {
            init_threading_model(&state);
        } catch (...) {
            fprintf(stderr, "Unexpected error initializing threading model\n");
            destroyStore();
            freeState(&state);
            exit (-1);
        }

        try {
            digAllFiles(argv, &state);
        } catch (std::runtime_error & e) {
            fprintf(stderr, "Error during carving: %s\n", e.what());
        }
        catch (...) {
            fprintf(stderr, "Unexpected error during carving\n");
        }

        try {
            closeAuditFile(state.auditFile);
        } catch (...) {
            fprintf(stderr, "Unexpected error closing audit file. \n");
        }
    } else {
        usage();
        fprintf(stdout, "\nERROR: No image files specified.\n\n");
    }

    fprintf(stdout,
        "\nScalpel is done, files carved = %" PRIu64 ", elapsed  = %ld secs.\n",
        state.fileswritten, (int)time(0) - starttime);

    destroy_threading_model(&state);
    destroyStore();
    freeState(&state);

    return 0;
}

// Register the signal-handler that will write to the audit file and
// close it if we catch a SIGINT or SIGTERM
static void registerSignalHandlers() {
    if (signal(SIGINT, catch_alarm) == SIG_IGN ) {
        signal(SIGINT, SIG_IGN );
    }
    if (signal(SIGTERM, catch_alarm) == SIG_IGN ) {
        signal(SIGTERM, SIG_IGN );
    }

#ifndef _WIN32
    // *****GGRIII:  is this problematic?
    // From foremost 0.69:
    /* Note: I haven't found a way to get notified of
    console resize events in Win32.  Right now the statusbar
    will be too long or too short if the user decides to resize
    their console window while foremost runs.. */

    //    signal(SIGWINCH, setttywidth);
#endif
}

// signal handler, sets global variable 'signal_caught' which is
// checked periodically during carve operations.  Allows clean
// shutdown.
void catch_alarm(int signum) {
    signal_caught = signum;
    signal(signum, catch_alarm);

#ifdef __DEBUG
    fprintf(stderr, "\nCaught signal: %s.\n", (char *)strsignal(signum));
#endif

    fprintf(stderr, "\nKill signal detected. Cleaning up...\n");
}

// GGRIII: for each file, build header/footer offset database first,
// then carve files based on this database.  Need to clear the
// header/footer offset database after processing of each file.

//TODO this function should be refactored and have notion of scalpelState removed
//it should be calling library function
//scalpel_carveSingleInput(ScalpelInputReader*, bool option1, ...)
static void digAllFiles(char **argv, struct scalpelState *state) {

    char inputFile[MAX_STRING_LENGTH];
    int i = 0, j = 0;
    FILE *listoffiles = NULL;

    if (state->useInputFileList) {
        fprintf(stdout, "Batch mode: reading list of images from %s.\n",
            state->inputFileList);
        listoffiles = fopen(state->inputFileList, "r");
        if(listoffiles == NULL) {
            fprintf(stderr, "Couldn't open file:\n%s -- %s\n",
                (*(state->inputFileList) ==
                '\0') ? "<blank>" : state->inputFileList, strerror(errno));
            try {
                handleError(state, SCALPEL_ERROR_FATAL_READ);
            }
            catch (std::runtime_error & e) {
                fprintf(stderr, "Couldn't open file from the input list, %s\n", e.what());
                return;
            }
        }
        j = 0;
        do {
            j++;

            if(fgets(inputFile, MAX_STRING_LENGTH, listoffiles) == NULL) {
                if (!feof(listoffiles)) {
                    fprintf(stderr,
                        "Error reading line %d of %s. Skipping line.\n",
                        j, state->inputFileList);
                }
                continue;
            }
            if(inputFile[strlen(inputFile) - 1] == '\n') {
                inputFile[strlen(inputFile) - 1] = '\x00';
            }

            // GGRIII: this function now *only* builds the header/footer
            // database.  Carving is handled afterward, in carveImageFile().

            ScalpelInputReader * inputReader = scalpel_createInputReaderFile(inputFile);
            if (!inputReader) {
                //error
                printf("Error creating inputReader for file %s\n", inputFile);
                return;
            }
            state->inReader = inputReader;

            if((i = digImageFile(state)) != SCALPEL_OK) {
                try {
                    handleError(state, i);
                }
                catch (std::runtime_error & e) {
                    printf("Error digging file %s\n", e.what());
                    scalpel_freeInputReaderFile(state->inReader);
                    state->inReader = NULL;
                }
                continue;
            }
            else {
                // GGRIII: "digging" is now complete and header/footer database
                // has been built.  The function carveImageFile() performs
                // extraction of files based on this database.

                if((i = carveImageFile(state)) != SCALPEL_OK) {
                    try {
                        handleError(state, i);
                    }
                    catch (std::runtime_error & e) {
                        printf("Error carving file %s\n", e.what());
                        scalpel_freeInputReaderFile(state->inReader);
                        state->inReader = NULL;
                    }
                    continue;
                }
            }
            scalpel_freeInputReaderFile(state->inReader);
            state->inReader = NULL;
        }
        while (!feof(listoffiles));
        fclose(listoffiles);
    }
    else {
        do {
            strncpy(inputFile, *argv, MAX_STRING_LENGTH);
            state->inReader = scalpel_createInputReaderFile(inputFile);
            if (!state->inReader) {
                //error
                printf("Error creating inputReader for file %s\n", inputFile);
                return;
            }

            // GGRIII: this function now *only* builds the header/footer
            // database.  Carving is handled afterward, in carveImageFile().

            if((i = digImageFile(state))) {
                try {
                    handleError(state, i);
                }
                catch (std::runtime_error & e) {
                    printf("Error digging file %s\n", e.what());
                    scalpel_freeInputReaderFile(state->inReader);
                    state->inReader = NULL;
                }
                continue;
            }
            else {
                // GGRIII: "digging" is now complete and header/footer database
                // has been built.  The function carveImageFile() performs extraction
                // of files based on this database.

                if((i = carveImageFile(state))) {
                    try {
                        handleError(state, i);
                    }
                    catch (std::runtime_error & e) {
                        printf("Error carving file %s\n", e.what());
                        scalpel_freeInputReaderFile(state->inReader);
                        state->inReader = NULL;
                    }
                    continue;
                }
            }
            ++argv;
            scalpel_freeInputReaderFile(state->inReader);
            state->inReader = NULL;
        }
        while (*argv);
    }
}

// parse command line arguments
//TODO should not have notion of static (should return bools or so and pass them to lib function)
void processCommandLineArgs(int argc, char **argv, struct scalpelState *state) {
    int i;
    int numopts = 1;

    while ((i = getopt(argc, argv, "behvVu:ndpq:rc:o:s:i:m:M:O")) != -1) {
        numopts++;
        switch (i) {

        case 'V':
            fprintf(stdout, SCALPEL_COPYRIGHT_STRING);
            exit(1);

        case 'h':
            usage();
            exit(1);

        case 's':
            numopts++;
            state->skip = strtoull(optarg, NULL, 10);
            fprintf(stdout,
                "Skipping the first %" PRIu64 " bytes of each image file.\n", state->skip);
            break;

        case 'c':
            numopts++;
            strncpy(state->conffile, optarg, MAX_STRING_LENGTH);
            break;

        case 'd':
            state->generateHeaderFooterDatabase = TRUE;
            break;

        case 'e':
            state->handleEmbedded = TRUE;
            break;

            /*

            case 'm':
            numopts++;
            state->updateCoverageBlockmap = TRUE;
            state->useCoverageBlockmap = TRUE;
            state->coveragefile = (char *)malloc(MAX_STRING_LENGTH * sizeof(char));
            checkMemoryAllocation(state, state->coveragefile, __LINE__,
            __FILE__, "state->coveragefile");
            strncpy(state->coveragefile, optarg, MAX_STRING_LENGTH);
            break;

            case 'u':
            numopts++;
            state->useCoverageBlockmap = TRUE;
            state->coveragefile = (char *)malloc(MAX_STRING_LENGTH * sizeof(char));
            checkMemoryAllocation(state, state->coveragefile, __LINE__,
            __FILE__, "state->coveragefile");
            strncpy(state->coveragefile, optarg, MAX_STRING_LENGTH);
            break;

            case 'M':
            numopts++;
            state->coverageblocksize = strtoul(optarg, NULL, 10);
            if(state->coverageblocksize <= 0) {
            fprintf(stderr,
            "\nERROR: Invalid blocksize for -M command line option.\n");
            exit(1);
            }
            break;

            */

        case 'o':
            numopts++;
            strncpy(state->outputdirectory, optarg, MAX_STRING_LENGTH);
            break;

        case 'O':
            state->organizeSubdirectories = FALSE;
            break;

        case 'p':
            state->previewMode = TRUE;
            break;

        case 'b':
            state->carveWithMissingFooters = TRUE;
            break;

        case 'i':
            state->useInputFileList = TRUE;
            state->inputFileList = optarg;
            break;

        case 'n':
            state->modeNoSuffix = TRUE;
            fprintf(stdout, "Extracting files without filename extensions.\n");
            break;

        case 'q':
            numopts++;
            state->blockAlignedOnly = TRUE;
            state->alignedblocksize = strtoul(optarg, NULL, 10);
            if(state->alignedblocksize <= 0) {
                fprintf(stderr,
                    "\nERROR: Invalid blocksize for -q command line option.\n");
                exit(1);
            }
            break;

        case 'r':
            state->noSearchOverlap = TRUE;
            break;

        case 'v':
            inputReaderVerbose = state->modeVerbose = TRUE;
            break;

        default:
            exit(1);
        }
    }

    // check for incompatible options

    if ((state->useInputFileList || argc - numopts > 1)
        && (state->updateCoverageBlockmap || state->useCoverageBlockmap)) {

            fprintf(stderr, "%d %d\n", argc, numopts);

            fprintf(stderr,
                "\nCoverage blockmaps can be processed only if a single image filename is\n"
                "specified on the command line.\n");
            exit(1);
    }
}

static void usage() {

    printf(
        "Scalpel carves files or data fragments from a disk image based on a set of\n"
        "file carving patterns, which include headers, footers, and other information.\n\n"

        "Usage: scalpel [-b] [-c <config file>] [-d] [-e] [-h] [-i <file>]\n"
        "[-n] [-o <outputdir>] [-O] [-p] [-q <clustersize>] [-r]\n"

        /*	 "[-s] [-m <blockmap file>] [-M <blocksize>] [-n] [-o <outputdir>]\n" */
        /*	 "[-O] [-p] [-q <clustersize>] [-r] [-s <num>] [-u <blockmap file>]\n" */

        "[-v] [-V] <imgfile> [<imgfile>] ...\n\n"

        "Options:\n"

        "-b  Carve files even if defined footers aren't discovered within\n"
        "    maximum carve size for file type [foremost 0.69 compat mode].\n"

        "-c  Choose configuration file.\n"

        "-d  Generate header/footer database; will bypass certain optimizations\n"
        "    and discover all footers, so performance suffers.  Doesn't affect\n"
        "    the set of files carved.  **EXPERIMENTAL**\n"

        "-e  Do nested header/footer matching, to deal with structured files that may\n"
        "    contain embedded files of the same type.  Applicable only to\n"
        "    FORWARD / NEXT patterns.\n"

        "-h  Print this help message and exit.\n"

        "-i  Read names of disk images from specified file.  Note that minimal parsing of\n"
        "    the pathnames is performed and they should be formatted to be compliant C\n"
        "    strings; e.g., under Windows, backslashes must be properly quoted, etc.\n"

        /*

        "-m  Use and update carve coverage blockmap file.  If the blockmap file does\n"
        "    not exist, it is created. For new blockmap files, 512 bytes is used as\n"
        "    a default blocksize unless the -M option overrides this value. In the\n"
        "    blockmap file, the first 32bit unsigned int in the file identifies the\n"
        "    block size.  Thereafter each 32bit unsigned int entry in the blockmap\n"
        "    file corresponds to one block in the image file.  Each entry counts how\n"
        "    many carved files contain this block. Requires more system resources.\n"
        "    This feature is currently experimental.\n"

        "-M  Set blocksize for new coverage blockmap file.\n"

        */

        "-n  Don't add extensions to extracted files.\n"

        "-o  Set output directory for carved files.\n"

        "-O  Don't organize carved files by type. Default is to organize carved files\n"
        "    into subdirectories.\n"

        "-p  Perform image file preview; audit log indicates which files\n"
        "    would have been carved, but no files are actually carved.  Useful for\n"
        "    indexing file or data fragment locations or supporting in-place file\n"
        "    carving.\n"

        "-q  Carve only when header is cluster-aligned.\n"

        "-r  Find only first of overlapping headers/footers [foremost 0.69 compat mode].\n"

        /*

        "-s  Skip num bytes in each disk image before carving.\n"


        "-u  Use (but don't update) carve coverage blockmap file when carving.\n"
        "    Carve only sections of the image whose entries in the blockmap are 0.\n"
        "    These areas are treated as contiguous regions.  This feature is\n"
        "    currently experimental.\n"

        */

        "-V  Print copyright information and exit.\n"

        "-v  Verbose mode.\n");
}

