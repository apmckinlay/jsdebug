/* Copyright 2014 (c) Suneido Software Corp. All rights reserved.
 * Licensed under GPLv2.
 */

#ifdef _MSC_VER
#pragma warning (disable : 4001) /*nonstandard extension 'single line comment'*/
#endif // _MSC_VER

//==============================================================================
// file: locals.c
// auth: Victor Schappert
// date: 20140608
// desc: Code for pushing local variables data to Java using JVMTI
//==============================================================================

#include <jvmti.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>

// =============================================================================
//                                 CONSTANTS
// =============================================================================

enum
{
    DEFAULT_LINE_NUMBER = -1,
    NATIVE_METHOD_JLOCATION = -1,
    SKIP_FRAMES = 1,
    MAX_STACK_FRAMES = 128,
};

enum
{
    ACC_PUBLIC      = 0x0001,
    ACC_STATIC      = 0x0008,
};

enum method_name
{
    METHOD_NAME_UNKNOWN = 0x000,
    METHOD_NAME_EVAL    = 0x100,
    METHOD_NAME_EVAL0   = METHOD_NAME_EVAL | 10,
    METHOD_NAME_EVAL1   = METHOD_NAME_EVAL | 11,
    METHOD_NAME_EVAL2   = METHOD_NAME_EVAL | 12,
    METHOD_NAME_EVAL3   = METHOD_NAME_EVAL | 13,
    METHOD_NAME_EVAL4   = METHOD_NAME_EVAL | 14,
    METHOD_NAME_CALL    = 0x200,
    METHOD_NAME_CALL0   = METHOD_NAME_CALL | 10,
    METHOD_NAME_CALL1   = METHOD_NAME_CALL | 11,
    METHOD_NAME_CALL2   = METHOD_NAME_CALL | 12,
    METHOD_NAME_CALL3   = METHOD_NAME_CALL | 13,
    METHOD_NAME_CALL4   = METHOD_NAME_CALL | 14,
};

static const char * JAVA_LANG_THROWABLE_CLASS       = "java/lang/Throwable";
static const char * JAVA_LANG_STRING_CLASS          = "java/lang/String";
static const char * JAVA_LANG_OBJECT_CLASS          = "java/lang/Object";
static const char * ARRAY_OF_JAVA_LANG_STRING_CLASS = "[Ljava/lang/String;";
static const char * ARRAY_OF_JAVA_LANG_OBJECT_CLASS = "[Ljava/lang/Object;";
static const char * THROWABLE_GET_MSG_METHOD_NAME   = "getMessage";
static const char * THROWABLE_GET_MSG_METHOD_SIGNATURE =
    "()Ljava/lang/String;";

static const char * REPO_CLASS                      = "suneido/debug/StackInfo";
static const char * STACK_FRAME_CLASS               = "suneido/runtime/SuCallable";
static const char * LOCALS_NAME_FIELD_NAME          = "localsNames";
static const char * LOCALS_NAME_FIELD_SIGNATURE     = "[[Ljava/lang/String;";
static const char * LOCALS_VALUE_FIELD_NAME         = "localsValues";
static const char * LOCALS_VALUE_FIELD_SIGNATURE    = "[[Ljava/lang/Object;";
static const char * is_call_FIELD_NAME              = "isCall";
static const char * is_call_FIELD_SIGNATURE         = "[Z";
static const char * LINE_NUMBERS_FIELD_NAME         = "lineNumbers";
static const char * LINE_NUMBERS_FIELD_SIGNATURE    = "[I";
static const char * IS_INITIALIZED_FIELD_NAME       = "isInitialized";
static const char * IS_INITIALIZED_FIELD_SIGNATURE  = "Z";
static const char * BREAKPT_METHOD_NAME             = "fetchInfo";
static const char * BREAKPT_METHOD_SIGNATURE        = "()V";

// =============================================================================
//                                  GLOBALS
// =============================================================================

static jclass     g_java_lang_throwable_class;
static jclass     g_java_lang_string_class;
static jclass     g_java_lang_object_class;
static jclass     g_array_of_java_lang_string_class;
static jclass     g_array_of_java_lang_object_class;
static jmethodID  g_throwable_get_message_method;

static jclass     g_repo_class;                 // Repository for stack info
static jclass     g_stack_frame_class;          // For filtering stack frames
static jclass     g_stack_frame_annotn_class;   // For filtering stack frames
static jfieldID   g_locals_name_field;
static jfieldID   g_locals_value_field;
static jfieldID   g_is_call_field;
static jfieldID   g_line_numbers_field;
static jfieldID   g_is_initialized_field;

// =============================================================================
//                          ERROR LOGGING FUNCTIONS
// =============================================================================

struct tostr_buf
{
    char chars[20];
};

static char * uintToHexStr(unsigned int x, struct tostr_buf * buffer)
{
    char * i = buffer->chars + sizeof(buffer->chars) - 1;
    *i-- = '\0';
uintToStr_loop:
    *i = "0123456789abcdef"[x % 16];
    if (x)
    {
        x /= 16;
        --i;
        goto uintToStr_loop;
    }
    return i;
}

static void fatalErrorPrefix()
{
    fputs("FATAL: jsdebug: ", stderr);
}

static void error1(const char * message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    fflush(stderr);
}

static void error2(const char * prefix, const char * suffix)
{
    fputs(prefix, stderr);
    fputs(suffix, stderr);
    fputc('\n', stderr);
    fflush(stderr);
}

static void errorJVMTI(jvmtiEnv * jvmti_env, jvmtiError error,
                       const char * message)
{
    char *           name = NULL;
    struct tostr_buf buffer;
    assert(JVMTI_ERROR_NONE != error);
    fputs(message, stderr);
    if (JVMTI_ERROR_NONE == (*jvmti_env)->GetErrorName(jvmti_env, error, &name))
    {
        fputs(" (", stderr);
        fputs(name, stderr);
        fputs("<0x", stderr);
    }
    else
        fputs(" (jvmti error code <0x", stderr);
    fputs(uintToHexStr((unsigned int)error, &buffer), stderr);
    fputs(">)\n", stderr);
    fflush(stderr);
}

static void fatalError1(const char * message)
{
    fatalErrorPrefix();
    error1(message);
}

static void fatalError2(const char * prefix, const char * suffix)
{
    fatalErrorPrefix();
    error2(prefix, suffix);
}

static void fatalErrorJVMTI(jvmtiEnv * jvmti_env, jvmtiError error,
                            const char * message)
{
    fatalErrorPrefix();
    errorJVMTI(jvmti_env, error, message);
}

static void exceptionDescribe(JNIEnv * jni_env)
{
    jthrowable   throwable        = (jthrowable)NULL;
    jstring      message          = (jstring)NULL;
    const char * message_chars    = (const char *)NULL;
    throwable = (*jni_env)->ExceptionOccurred(jni_env);
    assert(throwable || !"Do not call function if no JNI exception pending");
    (*jni_env)->ExceptionClear(jni_env);
    if (! g_java_lang_throwable_class || ! g_throwable_get_message_method)
    {
        fputs("can't describe exception because required global references not "
              "available", stderr);
        return;
    }
    message = (*jni_env)->CallObjectMethod(
        jni_env, throwable, g_throwable_get_message_method);
    if ((*jni_env)->ExceptionCheck(jni_env))
    {
        fatalError2("exception while trying to describe exception", "");
        return;
    }
    else if (! message)
        return;
    message_chars = (*jni_env)->GetStringUTFChars(jni_env, message, NULL);
    if (message_chars)
    {
        fputs("exception message: \"", stderr);
        fputs(message_chars, stderr);
        fputs("\"\n", stderr);
        fflush(stderr);
        (*jni_env)->ReleaseStringUTFChars(jni_env, message, message_chars);
    }
}

// =============================================================================
//                             HELPER FUNCTIONS
// =============================================================================

static int getClassGlobalRef(JNIEnv * jni_env, jclass * pclass,
                             const char * name)
{
    jclass clazz = (*jni_env)->FindClass(jni_env, name);
    if ((*jni_env)->ExceptionCheck(jni_env))
    {
        fatalError2("exception while finding class: ", name);
        exceptionDescribe(jni_env);
        return 0;
    }
    else if (!clazz)
    {
        fatalError2("can't find class: ", name);
        return 0;
    }
    *pclass = (*jni_env)->NewGlobalRef(jni_env, clazz);
    if (!*pclass)
    {
        fatalError2("can't convert class to global reference: ", name);
        return 0;
    }
    // Return success
    return 1;
}

static int getFieldID(JNIEnv * jni_env, jclass clazz, jfieldID * pfieldID,
                      const char * name, const char * sig)
{
    *pfieldID = (*jni_env)->GetFieldID(jni_env, clazz, name, sig);
    if ((*jni_env)->ExceptionCheck(jni_env))
    {
        fatalError2("exception while getting field name: ", name);
        exceptionDescribe(jni_env);
        return 0;
    }
    else if (!*pfieldID)
    {
        fatalError2("can't get field name: ", name);
        return 0;
    }
    // Return success
    return 1;
}

static int getMethodID(JNIEnv * jni_env, jclass clazz, jmethodID * pmethodID,
                       const char * name, const char * sig)
{
    *pmethodID = (*jni_env)->GetMethodID(jni_env, clazz, name, sig);
    if ((*jni_env)->ExceptionCheck(jni_env))
    {
        fatalError2("exception while getting method name: ", name);
        exceptionDescribe(jni_env);
        return 0;
    }
    else if (!pmethodID)
    {
        fatalError2("can't get method name: ", name);
        return 0;
    }
    // Return success
    return 1;
}

static int initGlobalRefs(JNIEnv * jni_env)
{
    return
        getClassGlobalRef(jni_env, &g_java_lang_throwable_class,
            JAVA_LANG_THROWABLE_CLASS) &&
        getMethodID(jni_env, g_java_lang_throwable_class,
            &g_throwable_get_message_method,
            THROWABLE_GET_MSG_METHOD_NAME,
            THROWABLE_GET_MSG_METHOD_SIGNATURE) &&
        getClassGlobalRef(jni_env, &g_java_lang_string_class,
            JAVA_LANG_STRING_CLASS) &&
        getClassGlobalRef(jni_env, &g_java_lang_object_class,
            JAVA_LANG_OBJECT_CLASS) &&
        getClassGlobalRef(jni_env, &g_array_of_java_lang_string_class,
            ARRAY_OF_JAVA_LANG_STRING_CLASS) &&
        getClassGlobalRef(jni_env, &g_array_of_java_lang_object_class,
            ARRAY_OF_JAVA_LANG_OBJECT_CLASS) &&
        getClassGlobalRef(jni_env, &g_repo_class, REPO_CLASS) &&
        getFieldID(jni_env, g_repo_class, &g_locals_name_field,
            LOCALS_NAME_FIELD_NAME, LOCALS_NAME_FIELD_SIGNATURE) &&
        getFieldID(jni_env, g_repo_class, &g_locals_value_field,
            LOCALS_VALUE_FIELD_NAME, LOCALS_VALUE_FIELD_SIGNATURE) &&
        getFieldID(jni_env, g_repo_class, &g_is_call_field,
            is_call_FIELD_NAME, is_call_FIELD_SIGNATURE) &&
        getFieldID(jni_env, g_repo_class, &g_line_numbers_field,
            LINE_NUMBERS_FIELD_NAME, LINE_NUMBERS_FIELD_SIGNATURE) &&
        getFieldID(jni_env, g_repo_class, &g_is_initialized_field,
            IS_INITIALIZED_FIELD_NAME, IS_INITIALIZED_FIELD_SIGNATURE) &&
        getClassGlobalRef(jni_env, &g_stack_frame_class, STACK_FRAME_CLASS);
}

static int initLocalsBreakpoint(jvmtiEnv * jvmti_env, JNIEnv * jni_env)
{
    jmethodID  method_id;
    jvmtiError error;
    jlocation  start_location;
    jlocation  end_location;
    assert(g_repo_class || !"Class not found");
    // Get the method ID where we want the breakpoint set.
    method_id = (*jni_env)->GetMethodID(jni_env, g_repo_class,
                                        BREAKPT_METHOD_NAME,
                                        BREAKPT_METHOD_SIGNATURE);
    if ((*jni_env)->ExceptionCheck(jni_env))
    {
        fatalError2("exception getting breakpoint method: ",
                    BREAKPT_METHOD_NAME);
        exceptionDescribe(jni_env);
        return 0;
    }
    else if (!method_id)
    {
        fatalError2("failed to get breakpoint method: ", BREAKPT_METHOD_NAME);
        return 0;
    }
    // Get the location of the method
    error = (*jvmti_env)->GetMethodLocation(jvmti_env, method_id,
                                            &start_location, &end_location);
    if (JVMTI_ERROR_NONE != error)
    {
        fatalErrorJVMTI(jvmti_env, error,
                        "failed to get breakpoint method location");
        return 0;
    }
    // Set the breakpoint
    error = (*jvmti_env)->SetBreakpoint(jvmti_env, method_id, start_location);
    if (JVMTI_ERROR_NONE != error)
    {
        fatalErrorJVMTI(jvmti_env, error, "failed to set breakpoint");
        return 0;
    }
    // Return success
    return 1;
}

static int objArrNew(JNIEnv * jni_env, jclass clazz, jint length,
                     jobjectArray * parr)
{
    jobjectArray arr = (*jni_env)->NewObjectArray(jni_env, length, clazz,
                                                  (jobject)NULL);
    if ((*jni_env)->ExceptionCheck(jni_env))
    {
        error1("exception in objArrNew");
        exceptionDescribe(jni_env);
        return 0;
    }
    else if (!arr)
    {
        error1("in objArrNew, NewObjectArray returned NULL");
        return 0;
    }
    else
    {
        *parr = arr;
        return 1;
    }
}

static int objArrPut(JNIEnv * jni_env, jobjectArray arr, jint index,
                     jobject value)
{
    (*jni_env)->SetObjectArrayElement(jni_env, arr, index, value);
    if ((*jni_env)->ExceptionCheck(jni_env))
    {
        error1("exception in objArrPut");
        exceptionDescribe(jni_env);
        return 0;
    }
    return 1;
}

static int objFieldPut(JNIEnv * jni_env, jobject obj, jfieldID field_id,
                       jobject field_value)
{
    (*jni_env)->SetObjectField(jni_env, obj, field_id, field_value);
    if ((*jni_env)->ExceptionCheck(jni_env))
    {
        error1("exception in objFieldPut");
        exceptionDescribe(jni_env);
        return 0;
    }
    return 1;
}

static void deallocateLocalVariableTable(
    jvmtiEnv * jvmti_env, jvmtiLocalVariableEntry * table, jint count)

{
    jint k;
    assert(table || !"Local variable table should not be null");
    for (k = 0; k < count; ++k)
    {
        (*jvmti_env)->Deallocate(jvmti_env, (unsigned char *)table[k].name);
        (*jvmti_env)->Deallocate(jvmti_env,
                                (unsigned char *)table[k].signature);
        (*jvmti_env)->Deallocate(jvmti_env,
                                 (unsigned char *)table[k].generic_signature);
    }
    (*jvmti_env)->Deallocate(jvmti_env, (unsigned char *)table);
}

// =============================================================================
//                            JVM INIT CALLBACKS
// =============================================================================

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4100) // unreferenced formal parameter
#endif // _MSC_VER

static void JNICALL callback_JVMInit(jvmtiEnv * jvmti_env, JNIEnv * jni_env,
                                     jthread thread)
{
    jvmtiError error;
    // Enable breakpoint events
    error = (*jvmti_env)->SetEventNotificationMode(jvmti_env, JVMTI_ENABLE,
                                                   JVMTI_EVENT_BREAKPOINT,
                                                   (jthread)NULL);
    if (JVMTI_ERROR_NONE != error)
    {
        fatalErrorJVMTI(jvmti_env, error, "failed to enable breakpoint events");
        goto callback_JVMInit_fatal;
    }
    // Initialize certain global references needed so we can store the locals
    // back into Java.
    if (!initGlobalRefs(jni_env))
        goto callback_JVMInit_fatal;
    // Set the breakpoint.
    if (!initLocalsBreakpoint(jvmti_env, jni_env))
        goto callback_JVMInit_fatal;
     // Successful initialization
    return;
    // Failed initialization
callback_JVMInit_fatal:
    (*jni_env)->FatalError(jni_env, "initialization failed");
}

#ifdef _MSC_VER
#pragma warning (pop)
#endif // _MSC_VER

// =============================================================================
//                         BREAKPOINT EVENT HANDLER
// =============================================================================

static int fetchLineNumbers(jvmtiEnv * jvmti_env, jmethodID method,
                            jlocation location, jint * line_numbers_arr,
                            jint frame_index)
{
    int                    result = 0;
    jvmtiError             error;
    jvmtiLineNumberEntry * line_number_table = NULL;
    jint                   line_number_entry_count = 0;
    jint                   line_number = DEFAULT_LINE_NUMBER;
    jint                   lo, mi, hi, len; // for binary search
    // Get the line number table entry
    error = (*jvmti_env)->GetLineNumberTable(jvmti_env, method,
                                             &line_number_entry_count,
                                             &line_number_table);
    if (JVMTI_ERROR_ABSENT_INFORMATION == error)
        goto fetchLineNumbers_store; // Store default value
    else if (JVMTI_ERROR_NONE != error)
    {
        errorJVMTI(jvmti_env, error, "failed to get line number table");
        goto fetchLineNumbers_end;
    }
    else if (line_number_entry_count < 1)
        goto fetchLineNumbers_store;
    assert(line_number_table || !"Line number table should not be null");
    // Find the greatest location in the table that is less than or equal to
    // the given location.
    lo = 0;
    hi = line_number_entry_count; // hi is "one past the end"
fetchLineNumbers_binary_search:
    len = hi - lo;
    if (len < 16)
    {
        assert(0 < len);
        do
        {
            line_number = line_number_table[lo].line_number;
            if (location <= line_number_table[lo].start_location)
                break;
        }
        while (++lo < hi);
        goto fetchLineNumbers_store;
    }
    else // 16 < len
    {
        mi = lo + len / 2;
        if (line_number_table[mi].start_location < location)
            lo = mi + 1;
        else
            hi = mi; // hi is "one past the end"
        goto fetchLineNumbers_binary_search;
    }
fetchLineNumbers_store:
    line_numbers_arr[frame_index] = line_number;
    // Finished with success
    result = 1;
fetchLineNumbers_end:
    if (line_number_table) // Contains no pointers so don't need sub-deallocates
        (*jvmti_env)->Deallocate(jvmti_env, (unsigned char *)line_number_table);
    // Return the success or failure code
    return result;
}

static int fetchLocals(jvmtiEnv * jvmti_env, JNIEnv * jni_env, jthread thread,
                      jmethodID method, jlocation location,
                      jobjectArray names_arr, jobjectArray values_arr,
                      jint frame_index)
{
    int                       result = 0;
    jvmtiError                error;
    jvmtiLocalVariableEntry * local_entry_table = NULL;
    jint                      local_entry_count = 0;
    jobjectArray              frame_names_arr = (jobjectArray)NULL;
    jobjectArray              frame_values_arr = (jobjectArray)NULL;
    jint                      table_index;
    jint                      array_index;
    jstring                   var_name;
    jobject                   var_value;
    // Get the local variable table for this method
    error = (*jvmti_env)->GetLocalVariableTable(jvmti_env, method,
        &local_entry_count,
        &local_entry_table);
    if (JVMTI_ERROR_ABSENT_INFORMATION == error)
    {
        result = 1;
        goto fetchLocals_end;
    }
    else if (JVMTI_ERROR_NONE != error)
    {
        errorJVMTI(jvmti_env, error, "getting local variable table");
        goto fetchLocals_end;
    }
    // NOTE: If the entry count is zero, GetLocalVariableTable() sometimes
    //       doesn't allocate memory for the table itself.
    if (local_entry_count < 1)
    {
        result = 1;
        goto fetchLocals_end;
    }
    assert(local_entry_table || !"Local variable table should not be null");
    // Create arrays that can hold all the possible local variables for this
    // method and attach these arrays into the master arrays.
    if (! objArrNew(jni_env, g_java_lang_string_class, local_entry_count,
                    &frame_names_arr) ||
        ! objArrPut(jni_env, names_arr, frame_index, frame_names_arr) ||
        ! objArrNew(jni_env, g_java_lang_object_class, local_entry_count,
                    &frame_values_arr) ||
        ! objArrPut(jni_env, values_arr, frame_index, frame_values_arr))
    {
        error1("failed to initialize frame arrays");
        goto fetchLocals_end;
    }
    // Iterate over the entries in the local variables table. For every
    // valid entry that is a non-null reference to an Object, add its name and
    // value to the arrays.
    table_index = 0;
    array_index = 0;
    for (; table_index < local_entry_count; ++table_index)
    {
        jvmtiLocalVariableEntry * entry = &local_entry_table[table_index];
        if (location < entry->start_location) continue;
        if (entry->start_location + entry->length < location) continue;
        if ('L' != entry->signature[0] && '[' != entry->signature[0]) continue;
        var_name  = (jstring)NULL;
        var_value = (jobject)NULL;
        error = (*jvmti_env)->GetLocalObject(jvmti_env, thread,
                                             SKIP_FRAMES + frame_index,
                                             entry->slot, &var_value);
        if (JVMTI_ERROR_TYPE_MISMATCH == error) continue; // Not an Object
        if (JVMTI_ERROR_NONE != error)
        {
            errorJVMTI(jvmti_env, error, "failed to get local variable value");
            goto fetchLocals_loop_error;
        }
        if (!var_value) continue; // Don't store null values
        var_name = (*jni_env)->NewStringUTF(jni_env, entry->name);
        if (!var_name)
        {
            error1("failed to get local variable name");
            goto fetchLocals_loop_error;
        }
        if (! objArrPut(jni_env, frame_names_arr, array_index, var_name) ||
            ! objArrPut(jni_env, frame_values_arr, array_index, var_value))
        {
            error1("failed to store local variable name or value");
            goto fetchLocals_loop_error;
        }
        (*jni_env)->DeleteLocalRef(jni_env, var_name);
        if (var_value)
            (*jni_env)->DeleteLocalRef(jni_env, var_value);
        ++array_index;
        continue;
fetchLocals_loop_error:
        if (var_value)
            (*jni_env)->DeleteLocalRef(jni_env, var_value);
        if (var_name)
            (*jni_env)->DeleteLocalRef(jni_env, var_name);
        goto fetchLocals_end;
    } // for
    // Set the success flag
    result = 1;
fetchLocals_end:
    // Clean up local variable table
    deallocateLocalVariableTable(jvmti_env, local_entry_table,
                                 local_entry_count);
    // Clean up any lingering local references
    if (frame_names_arr)
        (*jni_env)->DeleteLocalRef(jni_env, frame_names_arr);
    if (frame_values_arr)
        (*jni_env)->DeleteLocalRef(jni_env, frame_values_arr);
    // Return the success or failure code
    return result;
}

static int getMethodName(jvmtiEnv * jvmti_env, jmethodID method,
                         enum method_name * mn)
{
    char     * str;
    jvmtiError error;
    error = (*jvmti_env)->GetMethodName(jvmti_env, method, &str, NULL, NULL);
    if (JVMTI_ERROR_NONE != error)
    {
        errorJVMTI(jvmti_env, error, "failed to get method name");
        return 0;
    }
    assert(str || !"Method name can't be null");
    if ('e' == str[0] && 'v' == str[1] && 'a' == str[2] && 'l' == str[3])
    {
        switch (str[4])
        {
            case '\0': *mn = METHOD_NAME_EVAL;  return 1;
            case '0':  *mn = METHOD_NAME_EVAL0; return 1;
            case '1':  *mn = METHOD_NAME_EVAL1; return 1;
            case '2':  *mn = METHOD_NAME_EVAL2; return 1;
            case '3':  *mn = METHOD_NAME_EVAL3; return 1;
            case '4':  *mn = METHOD_NAME_EVAL4; return 1;
        }
    }
    else if ('c' == str[0] && 'a' == str[1] && 'l' == str[2] && 'l' == str[3])
    {
        switch (str[4])
        {
            case '\0': *mn = METHOD_NAME_CALL;  return 1;
            case '0':  *mn = METHOD_NAME_CALL0; return 1;
            case '1':  *mn = METHOD_NAME_CALL1; return 1;
            case '2':  *mn = METHOD_NAME_CALL2; return 1;
            case '3':  *mn = METHOD_NAME_CALL3; return 1;
            case '4':  *mn = METHOD_NAME_CALL4; return 1;
        }
    }
    *mn = METHOD_NAME_UNKNOWN;
    return 1;
}

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4100) // unreferenced formal parameter
#endif // _MSC_VER

static void JNICALL callback_Breakpoint(jvmtiEnv * jvmti_env, JNIEnv * jni_env,
                                        jthread breakpoint_thread,
                                        jmethodID breakpoint_method,
                                        jlocation breakpoint_location)
{
    jvmtiError       error;
    jvmtiFrameInfo   frame_buffer_stack[MAX_STACK_FRAMES];
    jvmtiFrameInfo * frame_buffer       = NULL;
    jint             frame_count        = 0;
    jobject          repo_ref           = (jobject)NULL;
    jobject          this_ref_cur       = (jobject)NULL;
    jobject          this_ref_above     = (jobject)NULL;
    jobjectArray     locals_names_arr   = (jobjectArray)NULL;
    jobjectArray     locals_values_arr  = (jobjectArray)NULL;
    jbooleanArray    is_call_arr        = (jbooleanArray)NULL;
    jboolean *       is_call_arr_       = NULL;
    jintArray        line_numbers_arr   = (jintArray)NULL;
    jint *           line_numbers_arr_  = NULL;
    jint             method_modifiers   = 0;
    jclass           class_ref          = (jclass)NULL;
    enum method_name method_name_cur    = METHOD_NAME_UNKNOWN;
    enum method_name method_name_above  = METHOD_NAME_UNKNOWN;
    // Fetch the current thread's frame count
    error = (*jvmti_env)->GetFrameCount(jvmti_env, breakpoint_thread,
                                        &frame_count);
    if (JVMTI_ERROR_NONE != error)
    {
        errorJVMTI(jvmti_env, error, "from GetFrameCount()");
        goto callback_Breakpoint_cleanup;
    }
    if (frame_count - SKIP_FRAMES <= MAX_STACK_FRAMES)
        frame_buffer = frame_buffer_stack;
    else
    {
        frame_buffer = (jvmtiFrameInfo *)malloc(
                           frame_count * sizeof(jvmtiFrameInfo));
        if (!frame_buffer)
        {
            error1("frame_buffer malloc returned NULL");
            goto callback_Breakpoint_cleanup;
        }
    }
    // Fetch the basic stack trace
    error = (*jvmti_env)->GetStackTrace(jvmti_env, breakpoint_thread,
                                        SKIP_FRAMES, frame_count - SKIP_FRAMES,
                                        frame_buffer, &frame_count);
    // Retrieve the "this" reference for the frame where the exception was
    // raised. This is the "this" reference to the repository object of type
    // REPO_CLASS in whose fields we will store the local variable values.
    error = (*jvmti_env)->GetLocalInstance(jvmti_env, breakpoint_thread, 0,
                                           &repo_ref);
    if (JVMTI_ERROR_NONE != error)
    {
        errorJVMTI(jvmti_env, error,
                   "attempting to get GetLocalInstance() for repo_ref");
        goto callback_Breakpoint_cleanup;
    }
    assert(repo_ref || !"Failed to get 'this' for repo_ref");
    // Create the locals JNI data structures and assign them to the repository
    // object.
    if (!objArrNew(jni_env, g_array_of_java_lang_string_class, frame_count, &locals_names_arr) ||
        !objArrNew(jni_env, g_array_of_java_lang_object_class, frame_count, &locals_values_arr))
    {
        error1("failed to create locals data structures");
        goto callback_Breakpoint_cleanup;
    }
    is_call_arr = (*jni_env)->NewBooleanArray(jni_env, frame_count);
    if (!is_call_arr)
    {
        error1("failed to create iscall? array");
        goto callback_Breakpoint_cleanup;
    }
    is_call_arr_ = (*jni_env)->GetBooleanArrayElements(jni_env, is_call_arr,
                                                       NULL);
    if (!is_call_arr_)
    {
        error1("failed to get iscall? array elements");
        goto callback_Breakpoint_cleanup;
    }
    line_numbers_arr = (*jni_env)->NewIntArray(jni_env, frame_count);
    if (!line_numbers_arr)
    {
        error1("failed to create line numbers array");
        goto callback_Breakpoint_cleanup;
    }
    line_numbers_arr_ = (*jni_env)->GetIntArrayElements(jni_env,
                                                        line_numbers_arr, NULL);
    if (!line_numbers_arr_)
    {
        error1("failed to get line numbers array elements");
        goto callback_Breakpoint_cleanup;
    }
    // Store the locals JNI data structures into "this".
    if (!objFieldPut(jni_env, repo_ref, g_locals_name_field, locals_names_arr) ||
        !objFieldPut(jni_env, repo_ref, g_locals_value_field, locals_values_arr) ||
        !objFieldPut(jni_env, repo_ref, g_is_call_field, is_call_arr) ||
        !objFieldPut(jni_env, repo_ref, g_line_numbers_field, line_numbers_arr))
    {
        error1("failed to store locals data structures into repo object");
        goto callback_Breakpoint_cleanup;
    }
    // Walk the stack looking for frames where the method's class is an instance
    // of g_stack_frame_class.
    jint k = 0;
    for (; k < frame_count; ++k)
    {
        // Keep track of the method name and "this" value in the frame we just
        // looked at (the frame "above" the current frame in the stack trace).
        // This information is needed to determine which Java stack frames
        // actually constitute Suneido stack frames since it may take 3-4 Java
        // stack frames to invoke a Suneido callable.
        method_name_above = method_name_cur;
        method_name_cur = METHOD_NAME_UNKNOWN;
        if (this_ref_above)
            (*jni_env)->DeleteLocalRef(jni_env, this_ref_above);
        this_ref_above = this_ref_cur;
        this_ref_cur = NULL;
        // Skip native methods
        if (NATIVE_METHOD_JLOCATION == frame_buffer[k].location)
            continue;
        // Get the method modifiers
        error = (*jvmti_env)->GetMethodModifiers(
            jvmti_env, frame_buffer[k].method, &method_modifiers);
        if (JVMTI_ERROR_NONE != error)
        {
            errorJVMTI(jvmti_env, error, "failed to get method modifiers");
            goto callback_Breakpoint_cleanup;
        }
        // Skip non-public methods
        if (ACC_PUBLIC != (ACC_PUBLIC & method_modifiers))
            continue;
        // Skip static methods
        if (ACC_STATIC == (ACC_STATIC & method_modifiers))
            continue;
        // Get the declaring class of the method.
        error = (*jvmti_env)->GetMethodDeclaringClass(
            jvmti_env, frame_buffer[k].method, &class_ref);
        if (JVMTI_ERROR_NONE != error)
        {
            errorJVMTI(jvmti_env, error,
                       "failed to get method declaring class");
            goto callback_Breakpoint_cleanup;
        }
        // If the declaring class is not assignable to g_stack_frame_class, we
        // don't want stack frame data from it.
        if (!(*jni_env)->IsAssignableFrom(jni_env, class_ref, g_stack_frame_class))
        {
            (*jni_env)->DeleteLocalRef(jni_env, class_ref);
            continue;
        }
        // Get the method name and skip any methods whose names don't
        // correspond to Suneido callable code.
        if (!getMethodName(jvmti_env, frame_buffer[k].method, &method_name_cur))
            goto callback_Breakpoint_cleanup;
        if (METHOD_NAME_UNKNOWN == method_name_cur)
            continue;
        // Get the "this" instance so we can determine if this stack frame is
        // the same as the previous stack frame.
        error = (*jvmti_env)->GetLocalInstance(jvmti_env, breakpoint_thread, 0,
                                               &this_ref_cur);
        if (JVMTI_ERROR_NONE != error)
        {
            errorJVMTI(jvmti_env, error,
                       "attempting to get GetLocalInstance() for this_ref_cur");
            goto callback_Breakpoint_cleanup;
        }
        assert(this_ref_cur || !"Failed to get 'this' for current stack frame");
        // If the "this" instance for this Java stack frame is the same as the
        // "this" instance of the immediately preceding Java stack frame, both
        // frames may logically be part of the same Suneido callable invocation
        // and we only want the top frame, which we have already seen...
        if (this_ref_above &&
            (*jni_env)->IsSameObject(jni_env, this_ref_cur, this_ref_above) &&
            method_name_above != method_name_cur)
            continue;
        // Tag methods that are calls.
        if (METHOD_NAME_CALL & method_name_cur)
            is_call_arr_[k] = JNI_TRUE;
        // Fetch the locals for this frame
        if (!fetchLocals(jvmti_env, jni_env, breakpoint_thread,
                         frame_buffer[k].method, frame_buffer[k].location,
                         locals_names_arr, locals_values_arr, k))
            goto callback_Breakpoint_cleanup; // Error already reported
        if (!fetchLineNumbers(jvmti_env, frame_buffer[k].method,
                              frame_buffer[k].location, line_numbers_arr_, k))
            goto callback_Breakpoint_cleanup; // Error already reported
    } // for k in [0 .. frame_count)
    // Write back the iscall? array
    assert(is_call_arr_);
    (*jni_env)->ReleaseBooleanArrayElements(jni_env, is_call_arr, is_call_arr_,
                                            0);
    is_call_arr_ = NULL;
    // Write back the line numbers array
    assert(line_numbers_arr_);
    (*jni_env)->ReleaseIntArrayElements(jni_env, line_numbers_arr,
                                        line_numbers_arr_, 0);
    line_numbers_arr_ = NULL;
    // Mark the stack info repository as fully initialized
    (*jni_env)->SetBooleanField(jni_env, repo_ref, g_is_initialized_field,
                                JNI_TRUE);
    if ((*jni_env)->ExceptionCheck(jni_env))
    {
        error1("exception while attempting to mark repo as initialized");
        exceptionDescribe(jni_env);
    }
callback_Breakpoint_cleanup:
    // If frame buffer allocated on the heap, clean it up
    if (frame_buffer != frame_buffer_stack)
        free(frame_buffer);
    // If the iscall? array is still consuming heap space, release it
    if (is_call_arr_)
        (*jni_env)->ReleaseBooleanArrayElements(jni_env, is_call_arr,
                                                is_call_arr_, JNI_ABORT);
    // If the line numbers array is still consuming heap space, release it
    if (line_numbers_arr_)
        (*jni_env)->ReleaseIntArrayElements(jni_env, line_numbers_arr,
                                            line_numbers_arr_, JNI_ABORT);
}

#ifdef _MSC_VER
#pragma warning (pop)
#endif // _MSC_VER

// =============================================================================
//                                AGENT init
// =============================================================================

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4100) // unreferenced formal parameter
#endif // _MSC_VER

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM * jvm, char * options,
                                    void * reserved)
{
    jvmtiEnv *          jvmti;
    jvmtiError          error;
    jvmtiCapabilities   caps;
    jvmtiEventCallbacks callbacks;
    // Obtain a pointer to the JVMTI environment
    error = (*jvm)->GetEnv(jvm, (void **)&jvmti, JVMTI_VERSION_1_0);
    if (JVMTI_ERROR_NONE != error)
    {
        fatalError1("Agent_OnLoad failed to get JVMTI environment");
        return error;
    }
    // Indicate the capabilities we want
    memset(&caps, 0, sizeof(caps));
    caps.can_access_local_variables     = 1;
    caps.can_get_line_numbers           = 1;
    caps.can_generate_breakpoint_events = 1;
    error = (*jvmti)->AddCapabilities(jvmti, &caps);
    if (JVMTI_ERROR_NONE != error)
    {
        fatalError1("Agent_OnLoad failed to get required capabilities");
        return error;
    }
    // Install the required callbacks
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.VMInit     = callback_JVMInit;
    callbacks.Breakpoint = callback_Breakpoint;
    error = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    if (JVMTI_ERROR_NONE != error)
    {
        fatalError1("Agent_OnLoad failed to install callbacks");
        return error;
    }
    // Turn on the required callbacks
    error = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
                                               JVMTI_EVENT_VM_INIT,
                                               (jthread)NULL);
    if (JVMTI_ERROR_NONE != error)
    {
        fatalError1("Agent_OnLoad failed to enable VMInit callback");
        return error;
    }
    error = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
                                               JVMTI_EVENT_BREAKPOINT,
                                               (jthread)NULL);
    if (JVMTI_ERROR_NONE != error)
    {
        fatalError1("Agent_OnLoad failed to enable Breakpoint callback");
        return error;
    }
    // Initialized OK
    return JNI_OK;
}

JNIEXPORT void JNICALL Agent_OnUnload(JavaVM * jvm)
{ }

#ifdef _MSC_VER
#pragma warning (pop)
#endif // _MSC_VER
