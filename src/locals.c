/* Copyright 2014 (c) Suneido Software Corp. All rights reserved.
 * Licensed under GPLv2.
 */

//==============================================================================
// file: locals.c
// auth: Victor Schappert
// date: 20140608
// desc: Code for pushing local variables data to Java using JVMTI
//==============================================================================

#include <jvmti.h>

#include <assert.h>
#include <string.h>

// =============================================================================
//                                 CONSTANTS
// =============================================================================

enum
{
    NATIVE_METHOD_JLOCATION = -1,
    SKIP_FRAMES = 1,
    MAX_FRAMES = 128
};

static const char * LOCALS_CLASS                    = "suneido/debug/Locals";
static const char * LOCALS_BREAKPT_METHOD_NAME      = "fetchLocals";
static const char * LOCALS_BREAKPT_METHOD_SIGNATURE = "()V";
static const char * LOCALS_NAME_FIELD_NAME          = "localsNames";
static const char * LOCALS_NAME_FIELD_SIGNATURE     = "[[Ljava/lang/String;";
static const char * LOCALS_VALUE_FIELD_NAME         = "localsValues";
static const char * LOCALS_VALUE_FIELD_SIGNATURE    = "[[Ljava/lang/Object;";
static const char * ARRAY_OF_JAVA_LANG_STRING_CLASS = "[Ljava/lang/String;";
static const char * ARRAY_OF_JAVA_LANG_OBJECT_CLASS = "[Ljava/lang/Object;";
static const char * JAVA_LANG_STRING_CLASS          = "java/lang/String";
static const char * JAVA_LANG_OBJECT_CLASS          = "java/lang/Object";

// =============================================================================
//                                  GLOBALS
// =============================================================================

static jclass     g_locals_class;
static jfieldID   g_locals_name_field;
static jfieldID   g_locals_value_field;
static jclass     g_array_of_java_lang_string_class;
static jclass     g_array_of_java_lang_object_class;
static jclass     g_java_lang_string_class;
static jclass     g_java_lang_object_class;

// =============================================================================
//                             STATIC FUNCTIONS
// =============================================================================

static int initGlobalRefs(JNIEnv * env)
{
    jclass   clazz;
    // Get a global reference to the class.
    clazz = (*env)->FindClass(env, LOCALS_CLASS);
    if (!clazz)
    {
        // TODO: error message
        // TODO: may want to suppress NoClassDefFound ?
        return 0;
    }
    g_locals_class = (*env)->NewGlobalRef(env, clazz);
    // Get global references to the class fields.
    g_locals_name_field = (*env)->GetFieldID(env, g_locals_class,
                                             LOCALS_NAME_FIELD_NAME,
                                             LOCALS_NAME_FIELD_SIGNATURE);
    if (!g_locals_name_field)
    {
        // TODO: error message
        return 0;
    }
    g_locals_value_field = (*env)->GetFieldID(env, g_locals_class,
                                              LOCALS_VALUE_FIELD_NAME,
                                              LOCALS_VALUE_FIELD_SIGNATURE);
    if (!g_locals_value_field)
    {
        // TODO: error message
        return 0;
    }
    // Get a global reference to class for java.lang.String[]
    clazz = (*env)->FindClass(env, ARRAY_OF_JAVA_LANG_STRING_CLASS);
    if (!clazz)
    {
        // TODO: error message
        return 0;
    }
    g_array_of_java_lang_string_class = (*env)->NewGlobalRef(env, clazz);
    // Get a global reference to class for java.lang.Object[]
    clazz = (*env)->FindClass(env, ARRAY_OF_JAVA_LANG_OBJECT_CLASS);
    if (!clazz)
    {
        // TODO: error message
        return 0;
    }
    g_array_of_java_lang_object_class = (*env)->NewGlobalRef(env, clazz);
    // Get a global reference to class for java.lang.String
    clazz = (*env)->FindClass(env, JAVA_LANG_STRING_CLASS);
    if (!clazz)
    {
        // TODO: error message
        return 0;
    }
    g_java_lang_string_class = (*env)->NewGlobalRef(env, clazz);
    // Get a global reference to class for java.lang.Object
    clazz = (*env)->FindClass(env, JAVA_LANG_OBJECT_CLASS);
    if (!clazz)
    {
        // TODO: error message
        return 0;
    }
    g_java_lang_object_class = (*env)->NewGlobalRef(env, clazz);
    // Return success
    return 1;
}

static int initLocalsBreakpoint(jvmtiEnv * jvmti_env, JNIEnv * jni_env)
{
    jmethodID  method_id;
    jvmtiError error;
    jlocation  start_location;
    jlocation  end_location;
    assert(g_locals_class || !"Class not found");
    // Get the method ID where we want the breakpoint set.
    method_id = (*jni_env)->GetMethodID(jni_env, g_locals_class,
                                        LOCALS_BREAKPT_METHOD_NAME,
                                        LOCALS_BREAKPT_METHOD_SIGNATURE);
    if (!method_id)
    {
        // TODO: error message
        return 0;
    }
    // Get the location of the method
    error = (*jvmti_env)->GetMethodLocation(jvmti_env, method_id,
                                            &start_location, &end_location);
    if (JVMTI_ERROR_NONE != error)
    {
        // TODO: error message
        return 0;
    }
    // Set the breakpoint
    error = (*jvmti_env)->SetBreakpoint(jvmti_env, method_id, start_location);
    if (JVMTI_ERROR_NONE != error)
    {
        // TODO: error message
        return 0;
    }
    // Return success
    return 1;
}

// =============================================================================
//                            JVM INIT CALLBACKS
// =============================================================================

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
        // TODO: report error
        return;
    }
    // Initialize certain global references needed so we can store the locals
    // back into Java.
    if (!initGlobalRefs(jni_env)) return;
    // Set the breakpoint.
    if (!initLocalsBreakpoint(jvmti_env, jni_env)) return;
}

static void JNICALL callback_JVMDeath(jvmtiEnv * jvmti_env, JNIEnv * jni_env)
{
    // TODO: free resources
}

// =============================================================================
//                         BREAKPOINT EVENT HANDLER
// =============================================================================

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


static int fetchLocals(jvmtiEnv * jvmti_env, JNIEnv * jni_env, jthread thread,
                       jmethodID method, jlocation location,
                       jobjectArray names_arr, jobjectArray values_arr,
                       jint frame_index)
{
    int                       result = 0;
    jvmtiError                error;
    jvmtiLocalVariableEntry * local_entry_table = (jvmtiLocalVariableEntry *)NULL;
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
    if (JVMTI_ERROR_NONE != error)
    {
        // TODO: report error
        return 0;
    }
    // NOTE: If the entry count is zero, GetLocalVariableTable() sometimes
    //       doesn't allocate memory for the table itself.
    if (0 == local_entry_count)
        return 1;
    assert(local_entry_table || !"Local variable table should not be null");
    // Create arrays that can hold all the possible local variables for this
    // method and attach these arrays into the master arrays.
    frame_names_arr = (*jni_env)->NewObjectArray(jni_env, local_entry_count,
                                                 g_java_lang_string_class,
                                                 (jobject)NULL);
    if (!frame_names_arr)
    {
        // TODO: report error
        goto fetchLocals_end;
    }
    frame_values_arr = (*jni_env)->NewObjectArray(jni_env, local_entry_count,
                                                  g_java_lang_object_class,
                                                  (jobject)NULL);
    if (!frame_values_arr)
    {
        // TODO: report error
        goto fetchLocals_end;
    }
    (*jni_env)->SetObjectArrayElement(jni_env, names_arr, frame_index,
                                      frame_names_arr);
    (*jni_env)->SetObjectArrayElement(jni_env, values_arr, frame_index,
                                      frame_values_arr);
    if ((*jni_env)->ExceptionCheck(jni_env))
    {
        // TODO: report error
        goto fetchLocals_end;
    }
    // Iterate over the entries in the local variables table. For every
    // valid entry that is an instance of Object, add its name and value
    // to the arrays.
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
        error = (*jvmti_env)->GetLocalObject(jvmti_env, thread, SKIP_FRAMES + frame_index,
                                             entry->slot, &var_value);
        if (JVMTI_ERROR_TYPE_MISMATCH == error) continue; // Not an Object
        if (JVMTI_ERROR_NONE != error)
        {
            // TODO: report error
            goto fetchLocals_loop_error;
        }
        if (!var_value)
        {
            (*jni_env)->DeleteLocalRef(jni_env, var_value);
            continue;
        }
        var_name = (*jni_env)->NewStringUTF(jni_env, entry->name);
        if (!var_name)
        {
            // TODO: report error
            goto fetchLocals_loop_error;
        }
        (*jni_env)->SetObjectArrayElement(jni_env, frame_names_arr, array_index,
                                          var_name);
        (*jni_env)->SetObjectArrayElement(jni_env, frame_values_arr, array_index,
                                          var_value);
        if ((*jni_env)->ExceptionCheck(jni_env))
        {
            // TODO: report error
            goto fetchLocals_loop_error;
        }
        ++array_index;
        continue;
fetchLocals_loop_error:
        if (var_value)
        {
            (*jni_env)->DeleteLocalRef(jni_env, var_value);
            if (var_name)
                (*jni_env)->DeleteLocalRef(jni_env, var_name);
        }
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

static void JNICALL callback_Breakpoint(jvmtiEnv * jvmti_env, JNIEnv * jni_env,
                                        jthread thread, jmethodID method,
                                        jlocation location)
{
    jvmtiError     error;
    jvmtiFrameInfo frame_buffer[MAX_FRAMES];
    jint           frame_count       = 0;
    jobject        thisref           = (jobject)NULL;
    jobjectArray   locals_names_arr  = (jobjectArray)NULL;
    jobjectArray   locals_values_arr = (jobjectArray)NULL;
    jclass         class_of_method   = (jclass)NULL;
    char         * class_source_file = (char *)NULL;
    int            flag;
    // Fetch the basic stack trace
    error = (*jvmti_env)->GetStackTrace(jvmti_env, thread, SKIP_FRAMES,
                                        MAX_FRAMES, frame_buffer, &frame_count);
    if (JVMTI_ERROR_NONE != error)
    {
        // TODO: report error
        return;
    }
    if (frame_count < 1)
        return;
    // Retrieve the "this" reference for the frame where the exception was
    // raised. This is the "this" reference to the object in whose fields we
    // will store the local variable values.
    error = (*jvmti_env)->GetLocalInstance(jvmti_env, thread, 0, &thisref);
    if (JVMTI_ERROR_NONE != error)
    {
        // TODO: report error
        return;
    }
    // Create the locals JNI data structures and assign them to the exception.
    locals_names_arr = (*jni_env)->NewObjectArray(
        jni_env, frame_count, g_array_of_java_lang_string_class, (jobject)NULL);
    if (!locals_names_arr)
    {
        // TODO: Report error
        return;
    }
    locals_values_arr = (*jni_env)->NewObjectArray(
        jni_env, frame_count, g_array_of_java_lang_object_class, (jobject)NULL);
    if (!locals_values_arr)
    {
        // TODO: Report error
        return;
    }
    // Store the locals JNI data structures into "this".
    (*jni_env)->SetObjectField(jni_env, thisref, g_locals_name_field,
                               locals_names_arr);
    (*jni_env)->SetObjectField(jni_env, thisref, g_locals_value_field,
                               locals_values_arr);
    // Walk the stack looking for local variables in each frame.
    for (jint k = 0; k < frame_count; ++k)
    {
        // Skip native methods
        if (NATIVE_METHOD_JLOCATION == frame_buffer[k].location)
            continue;
        // Check whether the method's declaring class is one whose local
        // variables should be kept.
        error = (*jvmti_env)->GetMethodDeclaringClass(jvmti_env,
                                                      frame_buffer[k].method,
                                                      &class_of_method);
        if (JVMTI_ERROR_NONE != error)
        {
            // TODO: report error
            return;
        }
        error = (*jvmti_env)->GetSourceFileName(jvmti_env, class_of_method,
                                                &class_source_file);
        (*jni_env)->DeleteLocalRef(jni_env, class_of_method);
        if (JVMTI_ERROR_NONE != error)
        {
            // TODO: report error
            return;
        }
        flag = 'M' == class_source_file[0];
        (*jvmti_env)->Deallocate(jvmti_env, class_source_file);
        if (flag)
        {
            if (! fetchLocals(jvmti_env, jni_env, thread,
                              frame_buffer[k].method, frame_buffer[k].location,
                              locals_names_arr, locals_values_arr, k))
                return; // Error already reported
        }
    }
}

// =============================================================================
//                                AGENT init
// =============================================================================

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
        return error;
    // Indicate the capabilities we want
    memset(&caps, 0, sizeof(caps));
    caps.can_access_local_variables     = 1; // Windows: Agent_OnLoad(...) only
    caps.can_generate_breakpoint_events = 1; // Windows: Agent_OnLoad(...) only
    caps.can_get_source_file_name       = 1; // Windows: Agent_OnAttach(...) too
    error = (*jvmti)->AddCapabilities(jvmti, &caps);
    if (JVMTI_ERROR_NONE != error)
        return error;
    // Set the callback that will be called to force us to fetch local
    // variables.
    error = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
                                               JVMTI_EVENT_VM_INIT,
                                               (jthread)NULL);
    if (JVMTI_ERROR_NONE != error)
        return error;
    error = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
                                               JVMTI_EVENT_VM_DEATH,
                                               (jthread)NULL);
    if (JVMTI_ERROR_NONE != error)
        return error;
    if (JVMTI_ERROR_NONE != error)
        return error;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.VMInit     = callback_JVMInit;
    callbacks.VMDeath    = callback_JVMDeath;
    callbacks.Breakpoint = callback_Breakpoint;
    error = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
    // Initialized OK
    return JNI_OK;
}

JNIEXPORT void JNICALL Agent_OnUnload(JavaVM *vm)
{
    // TODO: cleanup
}
