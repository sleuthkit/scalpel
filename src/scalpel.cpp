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

// globals defined in scalpel.h

// current wildcard character
char wildcard;

// signal has been caught by signal handler
int signal_caught;

// width of tty, for progress bar
int ttywidth;

// propagate verbose flag to reader layer
int inputReaderVerbose;


int extractSearchSpecData(struct scalpelState *state, struct SearchSpecLine *s,
		char **tokenarray) {

	int err = 0;

	// process one line from config file:
	//     token[0] = suffix
	//     token[1] = case sensitive?
	//     token[2] = maximum carve size
	//     token[3] = begintag
	//     token[4] = endtag
	//     token[5] = search type (optional)

	s->suffix = (char *) malloc(MAX_SUFFIX_LENGTH * sizeof(char));
	checkMemoryAllocation(state, s->suffix, __LINE__, __FILE__, "s->suffix");
	s->begin = (char *) malloc(MAX_STRING_LENGTH * sizeof(char));
	checkMemoryAllocation(state, s->begin, __LINE__, __FILE__, "s->begin");
	s->end = (char *) malloc(MAX_STRING_LENGTH * sizeof(char));
	checkMemoryAllocation(state, s->end, __LINE__, __FILE__, "s->end");
	s->begintext = (char *) malloc(MAX_STRING_LENGTH * sizeof(char));
	checkMemoryAllocation(state, s->begintext, __LINE__, __FILE__,
			"s->begintext");
	s->endtext = (char *) malloc(MAX_STRING_LENGTH * sizeof(char));
	checkMemoryAllocation(state, s->endtext, __LINE__, __FILE__, "s->endtext");

	if (!strncasecmp(tokenarray[0], SCALPEL_NOEXTENSION_SUFFIX,
			strlen(SCALPEL_NOEXTENSION_SUFFIX))) {
		s->suffix[0] = SCALPEL_NOEXTENSION;
		s->suffix[1] = 0;
	} else {
		memcpy(s->suffix, tokenarray[0], MAX_SUFFIX_LENGTH);
	}

	// case sensitivity check
	s->casesensitive = (!strncasecmp(tokenarray[1], "y", 1)
			|| !strncasecmp(tokenarray[1], "yes", 3));

	//#ifdef _WIN32
	//    s->length = _atoi64(tokenarray[2]);
	//#else
	//  s->length = atoull(tokenarray[2]);
	//#endif

	char split[MAX_STRING_LENGTH];
	char *maxcarvelength;

	strcpy(split, tokenarray[2]);
	maxcarvelength = strchr(split, ':');
	if (!maxcarvelength) {
		s->minlength = 0;
		s->length = strtoull(split, 0, 10);
	} else {
		*maxcarvelength = 0;
		maxcarvelength++;
		s->minlength = strtoull(split, 0, 10);
		s->length = strtoull(maxcarvelength, 0, 10);
	}

	// determine search type for this needle
	s->searchtype = SEARCHTYPE_FORWARD;
	if (!strncasecmp(tokenarray[5], "REVERSE", strlen("REVERSE"))) {
		s->searchtype = SEARCHTYPE_REVERSE;
	} else if (!strncasecmp(tokenarray[5], "NEXT", strlen("NEXT"))) {
		s->searchtype = SEARCHTYPE_FORWARD_NEXT;
	}
	// FORWARD is the default, but OK if the user defines it explicitly
	else if (!strncasecmp(tokenarray[5], "FORWARD", strlen("FORWARD"))) {
		s->searchtype = SEARCHTYPE_FORWARD;
	}

	// regular expressions must be handled separately

	if (isRegularExpression(tokenarray[3])) {

#ifdef GPU_THREADING
		// GPU execution does not support regex needles.
		std::stringstream ss;
		ss << "ERROR: GPU search for regex headers is not supported!\n";
		ss << "Please modify the config file for non-regex headers only.\n";
		std::string msg = ss.str();
		fprintf(stderr, msg.c_str());
		throw std::runtime_error(msg);
#endif

		// copy RE, zap leading/training '/' and prepare for regular expression compilation
		s->beginisRE = 1;
		strcpy(s->begin, tokenarray[3]);
		strcpy(s->begintext, tokenarray[3]);
		s->beginlength = strlen(tokenarray[3]);
		s->begin[s->beginlength] = 0;
		// compile regular expression
		err = regncomp(&(s->beginstate.re), s->begin + 1, s->beginlength - 2,
				REG_EXTENDED | (REG_ICASE * (!s->casesensitive)));
		if (err) {
			return SCALPEL_ERROR_BAD_HEADER_REGEX;
		}
	} else {
		// non-regular expression header
		s->beginisRE = 0;
		strcpy(s->begintext, tokenarray[3]);
		s->beginlength = translate(tokenarray[3]);
		memcpy(s->begin, tokenarray[3], s->beginlength);
		init_bm_table(s->begin, s->beginstate.bm_table, s->beginlength,
				s->casesensitive);
	}

	if (isRegularExpression(tokenarray[4])) {

#ifdef GPU_THREADING
		// GPU execution does not support regex needles.
		std::stringstream ss;
		ss << "ERROR: GPU search for regex footers is not supported!\n";
		ss << "Please modify the config file for non-regex footers only.\n";
		std::string msg = ss.str();
		fprintf(stderr, msg.c_str());
		throw std::runtime_error(msg);
#endif  

		// copy RE, zap leading/training '/' and prepare for for regular expression compilation
		s->endisRE = 1;
		strcpy(s->end, tokenarray[4]);
		strcpy(s->endtext, tokenarray[4]);
		s->endlength = strlen(tokenarray[4]);
		s->end[s->endlength] = 0;
		// compile regular expression
		err = regncomp(&(s->endstate.re), s->end + 1, s->endlength - 2,
				REG_EXTENDED | (REG_ICASE * (!s->casesensitive)));

		if (err) {
			return SCALPEL_ERROR_BAD_FOOTER_REGEX;
		}
	} else {
		s->endisRE = 0;
		strcpy(s->endtext, tokenarray[4]);
		s->endlength = translate(tokenarray[4]);
		memcpy(s->end, tokenarray[4], s->endlength);
		init_bm_table(s->end, s->endstate.bm_table, s->endlength,
				s->casesensitive);
	}


	return SCALPEL_OK;
}

int processSearchSpecLine(struct scalpelState *state, char *buffer,
		int lineNumber) {

	char *buf = buffer;
	char *token;
	int i = 0, err = 0, len = strlen(buffer);

	// murder CTRL-M (0x0d) characters
	//  if(buffer[len - 2] == 0x0d && buffer[len - 1] == 0x0a) {
	if (len >= 2 && buffer[len - 2] == 0x0d && buffer[len - 1] == 0x0a) {
		buffer[len - 2] = buffer[len - 1];
		buffer[len - 1] = buffer[len];
	}

	buf = (char *) skipWhiteSpace(buf);
	token = strtok(buf, " \t\n");

	// lines beginning with # are comments
	if (token == NULL || token[0] == '#') {
		return SCALPEL_OK;
	}

	// allow wildcard to be changed
	if (!strncasecmp(token, "wildcard", 9)) {
		if ((token = strtok(NULL, " \t\n")) != NULL ) {
			translate(token);
		} else {
			fprintf(stdout,
			"Warning: Empty wildcard in configuration file line %d. Ignoring.\n",
	      lineNumber);
			return SCALPEL_OK;
		}

		if(strlen(token) > 1) {
			fprintf(stderr, "Warning: Wildcard can only be one character,"
					" but you specified %d characters.\n"
					"         Using the first character, \"%c\", as the wildcard.\n",
					(int)strlen(token), token[0]);
		}

		wildcard = token[0];
		return SCALPEL_OK;
	}

	char **tokenarray = (char **) malloc(
			6 * sizeof(char[MAX_STRING_LENGTH + 1]));

	checkMemoryAllocation(state, tokenarray, __LINE__, __FILE__, "tokenarray");

	while (token && (i < NUM_SEARCH_SPEC_ELEMENTS)) {
		tokenarray[i] = token;
		i++;
		token = strtok(NULL, " \t\n");
	}

	switch (NUM_SEARCH_SPEC_ELEMENTS - i) {
	case 2:
		tokenarray[NUM_SEARCH_SPEC_ELEMENTS - 1] = (char *) "";
		tokenarray[NUM_SEARCH_SPEC_ELEMENTS - 2] = (char *) "";
		break;
	case 1:
		tokenarray[NUM_SEARCH_SPEC_ELEMENTS - 1] = (char *) "";
		break;
	case 0:
		break;
	default:
		fprintf(stderr,
		"\nERROR: In line %d of the configuration file, expected %d tokens,\n"
		"       but instead found only %d.\n",
		lineNumber, NUM_SEARCH_SPEC_ELEMENTS, i);
        free(tokenarray);
		return SCALPEL_ERROR_NO_SEARCH_SPEC;
		break;

	}

	if ((err = extractSearchSpecData(state,
			&(state->SearchSpec[state->specLines]), tokenarray))) {
		switch (err) {
		case SCALPEL_ERROR_BAD_HEADER_REGEX:
			fprintf(stderr,
"\nERROR: In line %d of the configuration file, bad regular expression for header.\n",
	      lineNumber)			;
			break;
			case SCALPEL_ERROR_BAD_FOOTER_REGEX:
			fprintf(stderr,
					"\nERROR: In line %d of the configuration file, bad regular expression for footer.\n",
					lineNumber);
			break;

			default:
			fprintf(stderr,
					"\nERROR: Unknown error on line %d of the configuration file.\n",
					lineNumber);
		}
	}
	state->specLines++;
    free(tokenarray);
	return SCALPEL_OK;
}

// process configuration file
int readSearchSpecFile(struct scalpelState *state) {

	int lineNumber = 0, status;
	FILE *f;

	char *buffer = (char *) malloc(
			(NUM_SEARCH_SPEC_ELEMENTS * MAX_STRING_LENGTH + 1) * sizeof(char));
	checkMemoryAllocation(state, buffer, __LINE__, __FILE__, "buffer");

	f = fopen(state->conffile, "r");
	if (f == NULL ) {
		fprintf(stderr,
		"ERROR: Couldn't open configuration file:\n%s -- %s\n",
		state->conffile, strerror(errno));
		free(buffer);
		buffer = NULL;
		return SCALPEL_ERROR_FATAL_READ;
	}

	while (fgets(buffer, NUM_SEARCH_SPEC_ELEMENTS * MAX_STRING_LENGTH, f)) {
		lineNumber++;

		if (state->specLines > MAX_FILE_TYPES) {
			fprintf(stderr, "Your conf file contains too many file types.\n");
			fprintf(stderr,
					"This version was compiled with MAX_FILE_TYPES == %d.\n",
					MAX_FILE_TYPES);
			fprintf(stderr,"Increase MAX_FILE_TYPES, recompile, and try again.\n");
      free(buffer);
      buffer = NULL;
      return SCALPEL_ERROR_TOO_MANY_TYPES;
    }

		if ((status = processSearchSpecLine(state, buffer, lineNumber))
				!= SCALPEL_OK) {
			free(buffer);
			buffer = NULL;
			return status;
		}
	}

	// add an empty object to the end of the list as a marker

	state->SearchSpec[state->specLines].suffix = NULL;
	state->SearchSpec[state->specLines].casesensitive = 0;
	state->SearchSpec[state->specLines].length = 0;
	state->SearchSpec[state->specLines].begin = NULL;
	state->SearchSpec[state->specLines].beginlength = 0;
	state->SearchSpec[state->specLines].end = NULL;
	state->SearchSpec[state->specLines].endlength = 0;

	// GGRIII: offsets field is uninitialized--it doesn't
	// matter, since we won't use this entry.

	fclose(f);
	free(buffer);
	buffer = NULL;
	return SCALPEL_OK;
}

// initialize state variable and copy command line arguments if passed in (argv can be NULL)
void initializeState(char ** argv, struct scalpelState *state) {
	char **argvcopy = argv;

	int sss;
	int i;

	state->inReader = NULL;

	// Allocate memory for state
	state->inputFileList = (char *) malloc(MAX_STRING_LENGTH * sizeof(char));
	checkMemoryAllocation(state, state->inputFileList, __LINE__, __FILE__,
			"state->inputFileList");
	state->conffile = (char *) malloc(MAX_STRING_LENGTH * sizeof(char));
	checkMemoryAllocation(state, state->conffile, __LINE__, __FILE__,
			"state->conffile");
	state->outputdirectory = (char *) malloc(MAX_STRING_LENGTH * sizeof(char));
	checkMemoryAllocation(state, state->conffile, __LINE__, __FILE__,
			"state->outputdirectory");
	state->invocation = (char *) malloc(MAX_STRING_LENGTH * sizeof(char));
	checkMemoryAllocation(state, state->invocation, __LINE__, __FILE__,
			"state->invocation");

	// GGRIII: memory allocation made more sane, because we're storing
	// more information in Scalpel than foremost had to, for each file
	// type.
	sss = (MAX_FILE_TYPES + 1) * sizeof(struct SearchSpecLine);
	state->SearchSpec = (struct SearchSpecLine *) malloc(sss);
    memset(state->SearchSpec, 0, sss);
	checkMemoryAllocation(state, state->SearchSpec, __LINE__, __FILE__,
			"state->SearchSpec");
	state->specLines = 0;

	// GGRIII: initialize header/footer offset data, carved file count,
	// et al.  The header/footer database is re-initialized in "dig.c"
	// after each image file is processed (numfilestocarve and
	// organizeDirNum are not). Storage for the header/footer offsets
	// will be reallocated as needed.

	for (i = 0; i < MAX_FILE_TYPES; i++) {
		state->SearchSpec[i].offsets.headers = 0;
		state->SearchSpec[i].offsets.headerlens = 0;
		state->SearchSpec[i].offsets.footers = 0;
		state->SearchSpec[i].offsets.footerlens = 0;
		state->SearchSpec[i].offsets.numheaders = 0;
		state->SearchSpec[i].offsets.numfooters = 0;
		state->SearchSpec[i].offsets.headerstorage = 0;
		state->SearchSpec[i].offsets.footerstorage = 0;
		state->SearchSpec[i].numfilestocarve = 0;
		state->SearchSpec[i].organizeDirNum = 0;
	}

	state->fileswritten = 0;
	state->skip = 0;
	state->organizeMaxFilesPerSub = MAX_FILES_PER_SUBDIRECTORY;
	state->modeVerbose = FALSE;
	state->modeNoSuffix = FALSE;
	state->useInputFileList = FALSE;
	state->carveWithMissingFooters = FALSE;
	state->noSearchOverlap = FALSE;
	state->generateHeaderFooterDatabase = FALSE;
	state->updateCoverageBlockmap = FALSE;
	state->useCoverageBlockmap = FALSE;
	state->coverageblocksize = 0;
	state->blockAlignedOnly = FALSE;
	state->organizeSubdirectories = TRUE;
	state->previewMode = FALSE;
	state->handleEmbedded = FALSE;
	state->auditFile = NULL;
	inputReaderVerbose = FALSE;

	// default values for output directory, config file, wildcard character,
	// coverage blockmap directory
	strncpy(state->outputdirectory, SCALPEL_DEFAULT_OUTPUT_DIR,
			strlen(SCALPEL_DEFAULT_OUTPUT_DIR));
	strncpy(state->conffile, SCALPEL_DEFAULT_CONFIG_FILE, MAX_STRING_LENGTH);
	state->coveragefile = state->outputdirectory;
	wildcard = SCALPEL_DEFAULT_WILDCARD;
	signal_caught = 0;
	state->invocation[0] = 0;

	// copy the invocation string into the state
    do 
    {
        strncat(state->invocation, *argvcopy,
                MAX_STRING_LENGTH - strlen(state->invocation));
        strncat(state->invocation, " ",
                MAX_STRING_LENGTH - strlen(state->invocation));
        ++argvcopy;
	} while (*argvcopy);
}

static void freeOffsets(SearchSpecOffsets * offsets) {
	if (offsets->footers) {
		free(offsets->footers);
		offsets->footers = NULL;
	}
	if (offsets->headers) {
		free(offsets->headers);
		offsets->headers = NULL;
	}
	if (offsets->headerlens) {
			free(offsets->headerlens);
			offsets->headerlens = NULL;
		}
	if (offsets->footerlens) {
				free(offsets->footerlens);
				offsets->footerlens = NULL;
			}

}

static void freeSearchSpec(struct SearchSpecLine *s) {

	for (int i = 0; i < MAX_FILE_TYPES; i++) {
		if (s[i].suffix) {
			free(s[i].suffix);
			s[i].suffix = NULL;
		}
		if (s[i].begin) {
			free(s[i].begin);
			s[i].begin = NULL;
		}
		if (s[i].end) {
			free(s[i].end);
			s[i].end = NULL;
		}
		if (s[i].begintext) {
			free(s[i].begintext);
			s[i].begintext = NULL;
		}
		if (s[i].endtext) {
			free(s[i].endtext);
			s[i].endtext = NULL;
		}

		freeOffsets(&(s[i].offsets) );
	}


	free(s);

}



void freeState(struct scalpelState *state) {
//return; //TODO @@@ validate freeState is correct then reenable
	if (state->inputFileList) {
		free(state->inputFileList);
		state->inputFileList = NULL;
	}

	if (state->conffile) {
			free(state->conffile);
			state->conffile = NULL;
	}

	if (state->outputdirectory) {
		free(state->outputdirectory);
		state->outputdirectory = NULL;
	}

	if (state->invocation) {
		free(state->invocation);
		state->invocation = NULL;
	}

	if (state->SearchSpec) {
		freeSearchSpec(state->SearchSpec);
		state->SearchSpec = NULL;
	}

}

// full pathnames for all files used
void convertFileNames(struct scalpelState *state) {

	char fn[MAX_STRING_LENGTH]; // should be [PATH_MAX +1] from limits.h

	if (realpath(state->outputdirectory, fn)) {
		strncpy(state->outputdirectory, fn, MAX_STRING_LENGTH);
	} else {
		//		perror("realpath");
	}

	if (realpath(state->conffile, fn)) {
		strncpy(state->conffile, fn, MAX_STRING_LENGTH);
	} else {
		//		perror("realpath");
	}

}

int libscalpel_initialize(scalpelState ** state, char * confFilePath, 
                          char * outDir, const scalpelState & options)
{
    std::string funcname("libscalpel_initialize");
    
    if (state == NULL)
        throw std::runtime_error(funcname + ": state argument must not be NULL.");
        
    if (*state != NULL)
        throw std::runtime_error(funcname + ": state has already been allocated.");

    if (outDir == NULL || strlen(outDir) == 0)
        throw std::runtime_error(funcname + ": no output directory provided.");

    if (confFilePath == NULL || strlen(confFilePath) == 0)
        throw std::runtime_error(funcname + ": no configuration file path provided.");

    scalpelState * pState = new scalpelState(options);
    
    char * argv[2];
    argv[0] = confFilePath;
    argv[1] = outDir;
    
    initializeState(&argv[0], pState);
    
    const size_t outDirLen = strlen(outDir);
    strncpy(pState->outputdirectory, outDir, outDirLen + 1);
    pState->outputdirectory[outDirLen + 1] = 0;
    const size_t confFilePathLen = strlen(confFilePath);
    strncpy(pState->conffile, confFilePath, confFilePathLen + 1);
    pState->conffile[confFilePathLen + 1] = 0;
    
    convertFileNames(pState);

    int err = 0;
    
    // prepare audit file and make sure output directory is empty.
    if ((err = openAuditFile(pState))) {
        handleError(pState, err); //can throw
        std::stringstream ss;
        ss << ": Error opening audit file, error code: " << err;
        throw std::runtime_error(funcname + ss.str());
    }

    // read configuration file
    if ((err = readSearchSpecFile(pState))) {
        // problem with config file
        handleError(pState, err); //can throw
        std::stringstream ss;
        ss << ": Error reading spec file, error code: " << err;
        throw std::runtime_error(funcname + ss.str());
    }

    // Initialize the backing store of buffer to read-in, process image data.
    init_store();

    // Initialize threading model for cpu or gpu search.
    init_threading_model(pState);

    *state = pState;
    
    return SCALPEL_OK;
}

int libscalpel_carve_input(scalpelState * state, ScalpelInputReader * const reader)
{
    std::string funcname("libscalpel_carve_input");
    
    if (state == NULL)
        throw std::runtime_error(funcname + ": NULL pointer provided for state.");
    
    if (reader == NULL)
        throw std::runtime_error(funcname + ": NULL pointer provided for Reader.");
        
    if (!reader->dataSource || !reader->id) {
        throw std::runtime_error(funcname + ": Reader datasource or id not set.");
    }

    if (!reader->open || !reader->read || !reader->seeko || !reader->tello
            || !reader->close || !reader->getError || !reader->getSize) {
        throw std::runtime_error(funcname + ": Reader callbacks not setup");
    }

    state->inReader = reader;

    int err = 0;
    
    if ((err = digImageFile(state))) {
        handleError(state, err); //can throw
        std::stringstream ss;
        ss << ": Error digging file, error code: " << err;
        throw std::runtime_error(funcname + ss.str());
    }

    if ((err = carveImageFile(state))) {
        handleError(state, err); //can throw
        std::stringstream ss;
        ss << ": Error carving file, error code: " << err;
        throw std::runtime_error(funcname + ss.str());
    }

    return SCALPEL_OK;
}

int libscalpel_finalize(scalpelState ** state)
{
    std::string funcname("libscalpel_finalize");
    
    if (state == NULL)
        throw std::runtime_error(funcname + ": state argument must not be NULL.");
        
    if (*state == NULL)
        throw std::runtime_error(funcname + ": state has not been allocated.");

    closeAuditFile((*state)->auditFile);
    destroy_threading_model(*state);
    destroyStore();
    freeState(*state);

    return SCALPEL_OK;
}

// the exposed libscalpel API
// NOTE: This function is deprecated and will be removed. Use the
// libscalpel_* functions instead.
// TODO make the driver in scalpel_exec.c use this (minor refactoring needed)
// TODO add support for the remaining options avail from cmd-line
// returns SCALPEL_OK on no error, can throw runtime_error exception on errors
int scalpel_carveSingleInput(ScalpelInputReader * const reader, const char * const confFilePath,
		const char * const outDir, const unsigned char generateFooterDb,
		const unsigned char handleEmbedded, const unsigned char organizeSubdirs,
		const unsigned char previewMode,
		const unsigned char carveWithMissingFooters,
		const unsigned char noSearchOverlap) throw (std::runtime_error) {

	if (!reader || ! confFilePath || ! outDir) {
		//invalid args
		throw std::runtime_error("Invalid empty arguments");
	}

	if (!reader->dataSource || !reader->id) {
		throw std::runtime_error("Invalid empty input reader arguments");
	}

	//check fns
	if (!reader->open || !reader->read || !reader->seeko || !reader->tello
			|| !reader->close || !reader->getError || !reader->getSize) {
		throw std::runtime_error("Reader callbacks not setup");
	}

	struct scalpelState state;

	std::string processorName ("scalpel_carveSingleInput()");
	char * args[5];
	args[0] = const_cast<char*> ( processorName.c_str());
	args[1] = reader->id;
	args[2] = const_cast<char*> (confFilePath);
	args[3] = const_cast<char*> (outDir);
	args[4] = 0;


	initializeState(args, &state);

	//setup input
	state.inReader = reader;

	//setup options
	const size_t outDirLen = strlen(outDir);
	strncpy(state.outputdirectory, outDir, outDirLen);
	state.outputdirectory[outDirLen] = 0;
	const size_t confFilePathLen = strlen(confFilePath);
	strncpy(state.conffile, confFilePath, confFilePathLen);
	state.conffile[confFilePathLen] = 0;
	state.generateHeaderFooterDatabase = generateFooterDb;
	state.handleEmbedded = handleEmbedded;
	state.organizeSubdirectories = organizeSubdirs;
	state.previewMode = previewMode;
	state.carveWithMissingFooters = carveWithMissingFooters;
	state.noSearchOverlap = noSearchOverlap;

	convertFileNames(&state);

	// read configuration file
	int err;
	if ((err = readSearchSpecFile(&state))) {
		// problem with config file
		handleError(&state, err); //can throw
		freeState(&state);
		std::stringstream ss;
		ss << "Error reading spec file, error code: " << err;
		throw std::runtime_error(ss.str());
	}

	// prepare audit file and make sure output directory is empty.
	if ((err = openAuditFile(&state))) {
		handleError(&state, err); //can throw
		freeState(&state);
		std::stringstream ss;
		ss << "Error opening audit file, error code: " << err;
		throw std::runtime_error(ss.str());
	}

	// Initialize the backing store of buffer to read-in, process image data.
	init_store();

	// Initialize threading model for cpu or gpu search.
	init_threading_model(&state);

	if ((err = digImageFile(&state))) {
		handleError(&state, err); //can throw
		closeAuditFile(state.auditFile);
		destroyStore();
		freeState(&state);
		std::stringstream ss;
		ss << "Error digging file, error code: " << err;
		throw std::runtime_error(ss.str());
	}

	if ((err = carveImageFile(&state))) {
		handleError(&state, err); //can throw
		closeAuditFile(state.auditFile);
		destroy_threading_model(&state);
		destroyStore();
		freeState(&state);
		std::stringstream ss;
		ss << "Error carving file, error code: " << err;
		throw std::runtime_error(ss.str());
	}

	closeAuditFile(state.auditFile);
	destroy_threading_model(&state);
	destroyStore();
	freeState(&state);

	return SCALPEL_OK;
}


