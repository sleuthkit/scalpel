/*
Copyright (C) 2013, Basis Technology Corp.
 * 
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
*/

/*
 * libscalpel_jni.cpp
 *
 * JNI bridge between TSK and libscalpel.
 * Allows to carve TSK java datamodel inputs (ReadContentInputStream)
 */

/* This code interfaces with TSK InputStream class:
 *
public class org.sleuthkit.datamodel.ReadContentInputStream extends java.io.Inpu
tStream {
  private long position;
    Signature: J
  private long length;
    Signature: J
  private org.sleuthkit.datamodel.Content content;
    Signature: Lorg/sleuthkit/datamodel/Content;
  private static final java.util.logging.Logger logger;
    Signature: Ljava/util/logging/Logger;
  public org.sleuthkit.datamodel.ReadContentInputStream(org.sleuthkit.datamodel.
Content);
    Signature: (Lorg/sleuthkit/datamodel/Content;)V

  public int read() throws java.io.IOException;
    Signature: ()I

  public int read(byte[]) throws java.io.IOException;
    Signature: ([B)I

  public int read(byte[], int, int) throws java.io.IOException;
    Signature: ([BII)I

  public int available() throws java.io.IOException;
    Signature: ()I

  public long skip(long) throws java.io.IOException;
    Signature: (J)J

  public void close() throws java.io.IOException;
    Signature: ()V

  public boolean markSupported();
    Signature: ()Z

  public long getLength();
    Signature: ()J

  public long getCurPosition();
    Signature: ()J

  public long seek(long);
    Signature: (J)J

  static {};
    Signature: ()V
}
 */

#include "libscalpel_jni.h"

#include "scalpel.h"
#include "input_reader.h"

//C++ STL headers
#include <exception>
#include <stdexcept>
#include <string>
#include <sstream>

#include <jni.h>


#define TSK_INPUTSTREAM_CLASS "org/sleuthkit/datamodel/ReadContentInputStream"
#define JAVA_READ_BUFFER_SIZE (1024 * 512)



//protos
static ScalpelInputReader * createInputReaderTsk(JNIEnv & env, const char * inputId, jobject jInputStream);
static void freeInputReaderTsk(JNIEnv & env, ScalpelInputReader * tskReader);
static void printVerbose(const char * const msg, ...);

//globs
static JavaVM *gJavaVM;
static int jniLogVerbose;
//extern int inputReaderVerbose;

/**
 * Sets flag to throw an ScalpelException back up to the Java code with a specific message.
 * Note: exception is thrown to Java code after the native function returns
 * not when setThrowTskCoreError() is invoked - this must be addressed in the code following the exception
 * @param the java environment to send the exception to
 * @param msg message string
 */
static void setThrowScalpelException(JNIEnv & env, const char *msg) {
	jclass exception;
	exception = env.FindClass(
			"org/sleuthkit/autopsy/scalpel/jni/ScalpelException");
	env.ThrowNew(exception, msg);
}

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    gJavaVM = vm;
    return JNI_VERSION_1_6;
}

/*
 * Class:     org_sleuthkit_autopsy_scalpel_jni_ScalpelCarver
 * Method:    carveNat
 * Signature: (Ljava/lang/String;Lorg/sleuthkit/datamodel/ReadContentInputStream;Ljava/lang/String;Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_sleuthkit_autopsy_scalpel_jni_ScalpelCarver_carveNat
  (JNIEnv * env, jclass callerClass, jstring jInputStreamId, jobject jInputStream, jstring jConfPath, jstring jOutputDirPath) {

	//inputReaderVerbose = 1;
	jniLogVerbose = 1;

	//check args
	if (!jInputStream) {
		setThrowScalpelException(*env, "Missing input stream object. ");
		return;
	}

	if (! env->IsInstanceOf(jInputStream, env->FindClass(TSK_INPUTSTREAM_CLASS) ) ) {
		setThrowScalpelException(*env, "Wrong input stream object type. ");
		return;
	}

	if (!jConfPath) {
		setThrowScalpelException(*env, "Missing scalpel configuration path object. ");
		return;
	}

	if (!jOutputDirPath) {
		setThrowScalpelException(*env, "Missing scalpel output dir path object. ");
		return;
	}


	//convert strings
	jboolean isCopyConfPath;
	const char *confPath = env->GetStringUTFChars(jConfPath, &isCopyConfPath);

	jboolean isCopyOutputDir;
	const char *outputDirPath = env->GetStringUTFChars(jOutputDirPath, &isCopyOutputDir);

	jboolean isCopyId;
	const char * inputId = env->GetStringUTFChars(jInputStreamId, &isCopyId);

	if (!confPath || !outputDirPath || !inputId) {
		fprintf(stdout, "Java_org_sleuthkit_autopsy_scalpel_jni_ScalpelCarver_carveNat - malloc() id ERROR converting strings\n ");
		setThrowScalpelException(*env, "Java_org_sleuthkit_autopsy_scalpel_jni_ScalpelCarver_carveNat - ERROR converting strings");
		return;
	}

	size_t len = env->GetStringUTFLength(jConfPath);
	char * confPathS = (char*) malloc(sizeof(char) * (len+1));
	if (!confPathS) {
		fprintf(stdout, "Java_org_sleuthkit_autopsy_scalpel_jni_ScalpelCarver_carveNat - malloc() id ERROR converting strings\n ");
		setThrowScalpelException(*env, "Java_org_sleuthkit_autopsy_scalpel_jni_ScalpelCarver_carveNat - ERROR converting strings");
		env->ReleaseStringUTFChars(jConfPath, confPath);
		env->ReleaseStringUTFChars(jOutputDirPath, outputDirPath);
		env->ReleaseStringUTFChars(jInputStreamId, inputId);
		return;
	}
	strncpy(confPathS, confPath, len);
	confPathS[len] = 0;
	env->ReleaseStringUTFChars(jConfPath, confPath);

	len = env->GetStringUTFLength(jOutputDirPath);
	char * outputDirPathS = (char*) malloc(sizeof(char) * (len+1));
	if (!outputDirPathS) {
		fprintf(stdout, "Java_org_sleuthkit_autopsy_scalpel_jni_ScalpelCarver_carveNat - malloc() id ERROR converting strings\n ");
		setThrowScalpelException(*env, "Java_org_sleuthkit_autopsy_scalpel_jni_ScalpelCarver_carveNat - ERROR converting strings");
		env->ReleaseStringUTFChars(jOutputDirPath, outputDirPath);
		env->ReleaseStringUTFChars(jInputStreamId, inputId);
		free(confPathS);
		confPathS = NULL;
		return;
	}
	strncpy(outputDirPathS, outputDirPath, len);
	outputDirPathS[len] = 0;
	env->ReleaseStringUTFChars(jOutputDirPath, outputDirPath);

	len = env->GetStringUTFLength(jInputStreamId);
	char * inputIdS = (char*) malloc(sizeof(char) * (len+1));
	if (!inputIdS) {
		fprintf(stdout, "Java_org_sleuthkit_autopsy_scalpel_jni_ScalpelCarver_carveNat - malloc() id ERROR converting strings\n ");
		setThrowScalpelException(*env, "Java_org_sleuthkit_autopsy_scalpel_jni_ScalpelCarver_carveNat - ERROR converting strings");
		env->ReleaseStringUTFChars(jInputStreamId, inputId);
		free(confPathS);
		confPathS = NULL;
		free(outputDirPathS);
		outputDirPathS = NULL;
		return;
	}
	strncpy(inputIdS, inputId, len);
	inputIdS[len] = 0;
	env->ReleaseStringUTFChars(jInputStreamId, inputId);

	//end convert strings

	fprintf(stdout, "Java_org_sleuthkit_autopsy_scalpel_jni_ScalpelCarver_carveNat %s %s %s\n", inputIdS, confPathS, outputDirPathS);
	//printVerbose("Java_org_sleuthkit_autopsy_scalpel_jni_ScalpelCarver_carveNat %s %s %s\n", inputIdS, confPathS, outputDirPathS);

	//setup stream wrapper

	jobject jInputStreamRef = jInputStream; //env->NewGlobalRef(jInputStream);
	if (!jInputStreamRef) {
		fprintf(stdout, "Java_org_sleuthkit_autopsy_scalpel_jni_ScalpelCarver_carveNat - ERROR creating stream global ref\n ");
		setThrowScalpelException(*env, "Java_org_sleuthkit_autopsy_scalpel_jni_ScalpelCarver_carveNat - ERROR creating stream global ref");

		//cleanup
		free(confPathS);
		confPathS = NULL;
		free(outputDirPathS);
		outputDirPathS = NULL;
		free(inputIdS);
		inputIdS = NULL;

		return;
	}

	ScalpelInputReader * tskInputStream = createInputReaderTsk(*env, inputIdS, jInputStreamRef);
	if (! tskInputStream) {
		setThrowScalpelException(*env, "Error creating ScalpelInputReader ");

		//cleanup
		free(confPathS);
		confPathS = NULL;
		free(outputDirPathS);
		outputDirPathS = NULL;
		free(inputIdS);
		inputIdS = NULL;

		//env->DeleteGlobalRef(jInputStreamRef);
		return;
	}

	//call scalpel

	try {
		int scalpErr = scalpel_carveSingleInput(tskInputStream, confPathS, outputDirPathS,
				FALSE, FALSE,
				TRUE,
				TRUE,  //preview mode
			   FALSE, FALSE);
		if (scalpErr) {
			std::stringstream ss;
			ss << "Error while carving, code: " << scalpErr;
			std::string msg = ss.str();
			fprintf(stdout, "%s\n", msg.c_str() );
			setThrowScalpelException(*env, msg.c_str());
		}

		fprintf(stdout, "libscalpel_jni done, libscalp result: %d\n", scalpErr);
	}

	catch (std::runtime_error & e) {
		std::stringstream ss;
		ss << "Java_org_sleuthkit_autopsy_scalpel_jni_ScalpelCarver_carveNat Error during carving: " << e.what();
		std::string msg = ss.str();
		fprintf(stdout, "%s\n", msg.c_str() );
		setThrowScalpelException(*env, msg.c_str());
	}
	catch (...) {
		std::string msg ("Unexpected error during carving");
		fprintf(stdout, "%s\n", msg.c_str());
		setThrowScalpelException(*env, msg.c_str());
	}

	//cleanup
	freeInputReaderTsk(*env, tskInputStream);

	free(confPathS);
	confPathS = NULL;
	free(outputDirPathS);
	outputDirPathS = NULL;
	free(inputIdS);
	inputIdS = NULL;

	//env->DeleteGlobalRef(jInputStreamRef);

	return;
}


/********** tsk datamodel implementation of ScalpelInputReader stream ***********/

//attach thread and get the env, or NULL if failed
static JNIEnv * attachThread() {
	JNIEnv *env;
	int status;

	if ((status = gJavaVM->GetEnv((void**)&env, JNI_VERSION_1_6)) < 0) {
		if ((status = gJavaVM->AttachCurrentThreadAsDaemon((void**)&env, NULL)) < 0) {
			fprintf(stdout, "attachThread() - ERROR getting env.");
			return NULL;
		}

	}
	return env;
}

//detach thread env, if previously attached
//do not call if attach had failed
static void detachThread() {
	//return; //TODO remove
	jint ret = gJavaVM->DetachCurrentThread();
	if (ret) {
		fprintf(stdout, "detachThread() - WARN can't detach thread, perhaps it is the main thread.\n");
	}
}

//encapsulates java ReadContentInputStream object along with its helper data
typedef struct _TskInputStreamSourceInfo {
	bool firstOpen;

	jobject jInputStream; //org.sleuthkit.datamodel.ReadContentInputStream
	jbyteArray jReadBuffer; //jbyte[] to pass over JNI from C to java ReadContentInputStream

	//cache methods IDs so we do not need to lookup each time
	//for performance
	jmethodID jReadMethodId;
	jmethodID jGetSizeMethodId;
	jmethodID jGetPositionMethodId;
	jmethodID jSeekMethodId;


} TskInputStreamSourceInfo;

static inline TskInputStreamSourceInfo * castTskDataSource(
		ScalpelInputReader * reader) {
	TskInputStreamSourceInfo * tskData = (TskInputStreamSourceInfo *) reader->dataSource;
	if (!tskData) {
		return NULL ;
	}
	return tskData;
}

//needs be thread safe
static int tskDataSourceRead(ScalpelInputReader * const reader, void * buf,
		size_t size, size_t count) {
	printVerbose("tskDataSourceRead()\n");

	JNIEnv *env = NULL;

	if (size == 0 || count == 0) {
		return 0;
	}

	env = attachThread();
	if (!env) {
		fprintf(stdout, "ERROR tskDataSourceRead, cannot get env\n");
		return 0;
	}

	const TskInputStreamSourceInfo * tskData = castTskDataSource(reader);
	if (!tskData) {
		setThrowScalpelException(*env, "tskDataSourceRead() - ERROR object not initialized");
		return 0;
	}

	//tskData->env->MonitorEnter(tskData->jInputStream);

	const size_t bytesToReadTotal = size * count;
	size_t bytesReadTotal = 0;

	//need to read small chunks from java into a smaller java buffer
	//because we can't allocate huge buffers libscalpel might be requesting
	//at the same time we are reusing a single preallocated buffer, which might help performance

	//read: reader is C and writer is java, so need to write to java buffer and then copy back to user C buffer
	//calling ReadInputContentStream method: public int read(byte[], int, int) throws java.io.IOException;

	//char tempBuf[JAVA_READ_BUFFER_SIZE]; //temporary C buffer to copy chunk to, before inserting the chunk in user C buffer

	jvalue args[3];
	jvalue * argsP = args;
	memset((void*)argsP, 0, sizeof(jvalue));
	memset((void*) ((jvalue*)argsP+1), 0, sizeof(jvalue));
	memset((void*)((jvalue*)argsP+2), 0, sizeof(jvalue));;

	while (bytesReadTotal < bytesToReadTotal) {
		jint remainToRead = bytesToReadTotal - bytesReadTotal;
		jint bytesToRead = remainToRead < JAVA_READ_BUFFER_SIZE ? remainToRead : JAVA_READ_BUFFER_SIZE;

		args[0].l = tskData->jReadBuffer;
		args[1].i = 0;
		args[2].i = bytesToRead;

		jint bytesRead = 0;

		bytesRead = env->CallIntMethod(tskData->jInputStream, tskData->jReadMethodId,
				tskData->jReadBuffer,
				0,
				bytesToRead
				);

		//check read exception from java and throw it back to java as scalpel exception
		jthrowable readExc = env->ExceptionOccurred();
		if (readExc) {
			setThrowScalpelException(*env, "tskDataSourceRead() - ERROR while reading from the input stream");
			env->ExceptionDescribe(); //log to stderr
			env->ExceptionClear();
			//tskData->env->MonitorExit(tskData->jInputStream);
			detachThread();
			return bytesReadTotal;
		}
		else if (bytesRead > 0) {
			//copy java buffer into right offset in C buffer

			jboolean isCopy;
			jbyte * tempBuf = env->GetByteArrayElements(tskData->jReadBuffer, &isCopy);
			if (!tempBuf) {
				setThrowScalpelException(*env, "tskDataSourceRead() - ERROR copying buffers while reading from the input stream ");
				env->ExceptionDescribe(); //log to stderr
				env->ExceptionClear();
				//tskData->env->MonitorExit(tskData->jInputStream);
				detachThread();
				return bytesReadTotal;
			}
			memcpy( (void*)((char*)buf + bytesReadTotal*sizeof(char)), (void*)tempBuf, bytesRead);
			env->ReleaseByteArrayElements(tskData->jReadBuffer, tempBuf, 0);

		}
		else {
			//fprintf(stdout, "tskDataSourceRead() - read less than expected, must be eof read: %d\n", bytesReadTotal);
			return bytesReadTotal;
		}

		bytesReadTotal += bytesRead;
	}

	if (bytesReadTotal < 0) {
		//adapt return value
		bytesReadTotal = 0;
	}

	//tskData->env->MonitorExit(tskData->jInputStream);
	fprintf(stdout, "\ntskDataSourceRead() BEFORE DETACH\n" );
	detachThread();
	fprintf(stdout, "\ntskDataSourceRead() AFTER DETACH, read %d bytes\n", bytesReadTotal );

	return bytesReadTotal;
}

static unsigned long long tskDataSourceTellO(ScalpelInputReader * const reader) {
	printVerbose("tskDataSourceTellO()\n");
	JNIEnv * env = attachThread();
	const TskInputStreamSourceInfo * tskData = castTskDataSource(reader);
	if (!tskData) {
		setThrowScalpelException(*env, "tskDataSourceTellO() - ERROR object not initialized");
		detachThread();
		return 0;
	}

	const jlong joff = env->CallLongMethod(tskData->jInputStream, tskData->jGetPositionMethodId);

	detachThread();

	fprintf(stdout, "tskDataSourceTellO() ret %" PRIu64 "\n", joff );

	return (unsigned long long) joff;
}

static void tskDataSourceClose(ScalpelInputReader * const reader) {
	printVerbose("tskDataSourceClose()\n");
	//nothing to be done, client closed the stream on java side and reset pos
	if (! reader->isOpen) {
		return;
	}

	reader->isOpen = FALSE;

	return;
}

static int tskDataSourceGetError(ScalpelInputReader * const reader) {
	printVerbose("tskDataSourceGetError()\n");
	//not implemented
	return 0;
}



//return size, or -1 on error
static long long tskDataSourceGetSize(ScalpelInputReader * const reader) {
	printVerbose("tskDataSourceGetSize()\n");
	JNIEnv * env = attachThread();
	const TskInputStreamSourceInfo * tskData = castTskDataSource(reader);
	if (!tskData) {

		setThrowScalpelException(* (env), "tskDataSourceGetSize() - ERROR object not initialized");
		detachThread();
		return -1;
	}

	const jlong jsize = env->CallLongMethod(tskData->jInputStream, tskData->jGetSizeMethodId);

	detachThread();

	return (long long) jsize;
}

//return 0 on no error
static int tskDataSourceOpen(ScalpelInputReader * const reader) {
	printVerbose("tskDataSourceOpen()\n");

	JNIEnv * env = attachThread();
	TskInputStreamSourceInfo * tskData = castTskDataSource(reader);
	if (!tskData) {
		setThrowScalpelException(* (env), "tskDataSourceOpen() - ERROR object not initialized");
		detachThread();
		return -1;
	}

	if (reader->isOpen) {
		fprintf(stdout, "tskDataSourceOpen() WARNING stream already open\n");
		//already open, should really close first, reset
		jlong zerOff = env->CallLongMethod(tskData->jInputStream, tskData->jSeekMethodId, (jlong)0);
		fprintf(stdout, "tskDataSourceOpen() rewinded, new offset: %" PRI64 "\n", (long long) zerOff);
	}
	else if (!tskData->firstOpen) {
		//closed but had already been open, so need to rewind to start
		//const jlong jnewOff =
		jlong zerOff = env->CallLongMethod(tskData->jInputStream, tskData->jSeekMethodId, (jlong)0);
		fprintf(stdout, "tskDataSourceOpen() rewinded, new offset: %" PRI64 "\n", (long long) zerOff);
	}


	reader->isOpen = TRUE;
	tskData->firstOpen = FALSE;
	detachThread();

	return 0;
}

//return 0 on no-error
static int tskDataSourceSeekO(ScalpelInputReader * const reader, long long offset,
		scalpel_SeekRel whence) {
	printVerbose("tskDataSourceSeekO()\n");
	//fprintf (stdout, "tskDataSourceSeekO() offset: %"PRI64 "whence: %d\n", offset, whence);
	JNIEnv * env = attachThread();
	const TskInputStreamSourceInfo * tskData = castTskDataSource(reader);
	if (!tskData) {
		setThrowScalpelException(* (env), "tskDataSourceSeekO() - ERROR object not initialized");
		detachThread();
		return -1;
	}

	jlong newOffset = 0;
	jlong joffRel = 0;
	switch (whence) {
	case SCALPEL_SEEK_SET:
		newOffset = offset;
		break;
	case SCALPEL_SEEK_CUR:
		//get cur
		joffRel = env->CallLongMethod(tskData->jInputStream, tskData->jGetPositionMethodId);
		newOffset = offset + joffRel;
		break;
	case SCALPEL_SEEK_END:
		//get size
		joffRel = env->CallLongMethod(tskData->jInputStream, tskData->jGetSizeMethodId);
		newOffset = joffRel - offset; //TODO verify if need -1
		break;
	default:
		break;
	}

	if (newOffset < 0) {
		setThrowScalpelException(* (env), "tskDataSourceSeekO() - ERROR invalid negative resulting offset.");
		detachThread();
		return -1;
	}

	//const jlong jnewOff =
	env->CallLongMethod(tskData->jInputStream, tskData->jSeekMethodId, newOffset);

	if (env->ExceptionCheck() ){
		env->ExceptionDescribe();
		env->ExceptionClear();
		setThrowScalpelException(* (env), "tskDataSourceSeekO() - ERROR seek failed.");
		detachThread();
		return -1;
	}

	detachThread();

	//fprintf(stdout, "tskDataSourceSeekO() deltaOffset: %"PRI64", new offset: %"PRI64 "\n", newOffset, (long long) jnewOff);

	return 0;

}


static void printVerbose(const char * const format, ...) {
	if (jniLogVerbose) {
		va_list args;
		va_start(args, format);
		//fprintf(stderr, format, args);
		fprintf(stdout, format, args);
	    va_end(args);
	}
	return;
}

static ScalpelInputReader * createInputReaderTsk(JNIEnv & env, const char * inputId, jobject jInputStream) {
	printVerbose("createInputReaderTsk()\n");

	ScalpelInputReader * tskReader = (ScalpelInputReader *) malloc(
			sizeof(ScalpelInputReader));
	if (!tskReader) {
		fprintf(stdout, "createInputReaderTsk() - malloc() ScalpelInputReader ERROR tskReader not created\n ");
		setThrowScalpelException(env, "createInputReaderTsk() - malloc() ScalpelInputReader ERROR tskReader not created");
		return NULL ;
	}

	//setup data
	tskReader->id = const_cast<char*> (inputId);

	tskReader->dataSource = (TskInputStreamSourceInfo*) malloc(sizeof(TskInputStreamSourceInfo));
	if (!tskReader->dataSource) {
		free(tskReader);
		tskReader = NULL;
		fprintf(stdout, "createInputReaderTsk() - malloc() TskInputStreamSourceInfo ERROR tskReader not created\n ");
		setThrowScalpelException(env, "createInputReaderTsk() - malloc() TskInputStreamSourceInfo  ERROR tskReader not created");
		return NULL ;
	}

	TskInputStreamSourceInfo* tskData = (TskInputStreamSourceInfo*) tskReader->dataSource;

	tskData->firstOpen = TRUE;

	tskData->jInputStream = jInputStream;

	//alloc java read buffer
	jbyteArray jReadBuffer = env.NewByteArray(JAVA_READ_BUFFER_SIZE);
	if (!jReadBuffer || env.ExceptionCheck()) {
		fprintf(stdout, "createInputReaderTsk() - ERROR allocating read buffer\n ");
		if (env.ExceptionCheck() ){
			env.ExceptionDescribe();
		}
		setThrowScalpelException(env,  "createInputReaderTsk() - ERROR allocating read buffer");
		tskData->jInputStream = NULL;
		free(tskReader->dataSource);
		tskReader->dataSource = NULL;
		free(tskReader);
		tskReader = NULL;
		return NULL ;
	}

	tskData->jReadBuffer = (jbyteArray) env.NewGlobalRef(jReadBuffer);
	if (! tskData->jReadBuffer) {
		fprintf(stdout, "createInputReaderTsk() - ERROR creating read buffer global ref\n ");
		if (env.ExceptionCheck() ){
			env.ExceptionDescribe();
			env.ExceptionClear();
		}
		setThrowScalpelException(env,  "createInputReaderTsk() - ERROR creating read buffer global ref");
		jReadBuffer = NULL; // local ref will be automatically deleted anyways
		tskData->jInputStream = NULL;
		free(tskReader->dataSource);
		tskReader->dataSource = NULL;
		free(tskReader);
		tskReader = NULL;
		return NULL ;
	}



	tskReader->isOpen = 0;

	//initialize java method IDs
	jclass clazz = env.FindClass(TSK_INPUTSTREAM_CLASS);
	if (!clazz) {
		fprintf(stdout, "createInputReaderTsk() - ERROR cannot load java class\n ");
		setThrowScalpelException(env,  "createInputReaderTsk() - ERROR cannot load java class");

		env.DeleteGlobalRef(tskData->jReadBuffer);

		free(tskReader->dataSource);
		tskReader->dataSource = NULL;
		free(tskReader);
		tskReader = NULL;
		return NULL ;
	}

	//using disassembler to get the correct signatures like javap -s -p org.sleuthkit.datamodel.ReadContentInputStream
	tskData->jGetPositionMethodId = env.GetMethodID(clazz, "getCurPosition", "()J");
	tskData->jGetSizeMethodId = env.GetMethodID(clazz, "getLength", "()J");
	tskData->jReadMethodId = env.GetMethodID(clazz, "read", "([BII)I");
	tskData->jSeekMethodId = env.GetMethodID(clazz, "seek", "(J)J");


	if (!tskData->jGetPositionMethodId
			|| !tskData->jGetSizeMethodId
			|| !tskData->jReadMethodId
			|| !tskData->jSeekMethodId
			|| env.ExceptionCheck()
			) {
		fprintf(stdout, "createInputReaderTsk() - ERROR getting java method ids for the input stream class, check API version\n ");
		if (env.ExceptionCheck()) {
			env.ExceptionDescribe();
			env.ExceptionClear();
		}
		setThrowScalpelException(env,  "createInputReaderTsk() - ERROR getting java method ids for the input stream class, check API version");

		env.DeleteGlobalRef(tskData->jReadBuffer);

		free(tskReader->dataSource);
		tskReader->dataSource = NULL;
		free(tskReader);
		tskReader = NULL;
		return NULL ;
	}


	//set up functions
	tskReader->open = tskDataSourceOpen;
	tskReader->close = tskDataSourceClose;
	tskReader->getError = tskDataSourceGetError;
	tskReader->getSize = tskDataSourceGetSize;
	tskReader->seeko = tskDataSourceSeekO;
	tskReader->tello = tskDataSourceTellO;
	tskReader->read = tskDataSourceRead;

	printVerbose("createInputReaderTsk -- input reader created\n");

	return tskReader;
}

static void freeInputReaderTsk(JNIEnv & env, ScalpelInputReader * tskReader) {
	printVerbose("freeInputReaderTsk()\n");
	if (!tskReader) {
		return;
	}

	if (!tskReader->dataSource) {
		fprintf(stdout, "freeInputReaderTsk() - ERROR dataSource not set, can't free\n ");
		return; //ERROR
	}

	TskInputStreamSourceInfo* tskData = (TskInputStreamSourceInfo*) tskReader->dataSource;
	if (!tskData) {
		fprintf(stdout, "freeInputReaderTsk() - ERROR TskInputStreamSourceInfo not set, can't free\n ");
		free(tskReader);
		tskReader = NULL;
		return; //ERROR
	}


	if (tskData->jReadBuffer) {
		env.DeleteGlobalRef(tskData->jReadBuffer);
		tskData->jReadBuffer = NULL;
	}


	if (tskData->jInputStream) {
		//env.DeleteGlobalRef(tskData->jInputStream); //done by carveNat()
		tskData->jInputStream = NULL;
	}

	//tskData->env = NULL;

	//these do not need to be freed
	tskData->jGetPositionMethodId = NULL;
	tskData->jGetSizeMethodId = NULL;
	tskData->jReadMethodId = NULL;
	tskData->jSeekMethodId = NULL;


	//reset fn pointers
	tskReader->open = NULL;
	tskReader->close = NULL;
	tskReader->getError = NULL;
	tskReader->getSize = NULL;
	tskReader->seeko = NULL;
	tskReader->tello = NULL;
	tskReader->read = NULL;


	//java client side is responsible for closing the stream it created
	tskReader->isOpen = FALSE;


	tskReader->id = NULL; //freed by jni ReleaseStringUTFChars()

	free(tskReader->dataSource);
	tskReader->dataSource = NULL;

	free(tskReader);
	tskReader = NULL;
}




