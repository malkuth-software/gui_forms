#ifndef GUI_FORMS_TESTS_ABI_GF_CONFORMANCE_PTHREAD_H
#define GUI_FORMS_TESTS_ABI_GF_CONFORMANCE_PTHREAD_H

/* The kit's threads. Every host but the MSVC target (clang on
 * x86_64-pc-windows-msvc) has POSIX threads: MinGW's winpthread, glibc,
 * musl and libSystem. The MSVC target has none, so this header supplies the
 * subset the kit uses over Win32 threads, with the POSIX names and results
 * (0 on success, an error number otherwise):
 *   pthread_create and pthread_join (no attributes; the result travels back
 *     through the join);
 *   pthread_self and pthread_equal (by thread id);
 *   mutexes over SRW locks and condition variables over Win32 condition
 *     variables (default attributes only);
 *   clock_gettime for CLOCK_REALTIME, the clock pthread_cond_timedwait's
 *     absolute deadline is measured on.
 * Used only inside the kit's own sources (the installed static library);
 * never installed, and never seen by a consumer. */

#if defined(_WIN32) && !defined(__MINGW32__)

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/* The handle and the result cell are owned by the creating thread until
 * pthread_join closes and frees them; pthread_self gives neither (only the
 * id, which pthread_equal compares). */
typedef struct gf_conformance_thread_result {
    void* value;
} gf_conformance_thread_result;

typedef struct pthread_t {
    HANDLE handle;
    DWORD id;
    gf_conformance_thread_result* result;
} pthread_t;

typedef struct pthread_mutex_t {
    SRWLOCK lock;
} pthread_mutex_t;

typedef struct pthread_cond_t {
    CONDITION_VARIABLE variable;
} pthread_cond_t;

/* Attributes are accepted only as null (the kit passes none). */
typedef struct gf_conformance_no_attributes gf_conformance_no_attributes;
typedef gf_conformance_no_attributes pthread_attr_t;
typedef gf_conformance_no_attributes pthread_mutexattr_t;
typedef gf_conformance_no_attributes pthread_condattr_t;

/* One thread's start, allocated by pthread_create and freed by the
 * trampoline before the body runs. */
typedef struct gf_conformance_thread_start {
    void* (*body)(void*);
    void* argument;
    gf_conformance_thread_result* result;
} gf_conformance_thread_start;

static inline DWORD WINAPI gf_conformance_thread_trampoline(LPVOID parameter) {
    const gf_conformance_thread_start start = *(const gf_conformance_thread_start*)parameter;
    free(parameter);
    void* const value = start.body(start.argument);
    (*start.result).value = value;
    return 0;
}

static inline int pthread_create(pthread_t* thread, const pthread_attr_t* attributes, void* (*body)(void*),
                                 void* argument) {
    if (attributes != NULL) {
        return EINVAL;
    }
    gf_conformance_thread_result* const result =
        (gf_conformance_thread_result*)calloc(1, sizeof(gf_conformance_thread_result));
    gf_conformance_thread_start* const start = (gf_conformance_thread_start*)malloc(sizeof(gf_conformance_thread_start));
    if (result == NULL || start == NULL) {
        free(result);
        free(start);
        return EAGAIN;
    }
    (*start).body = body;
    (*start).argument = argument;
    (*start).result = result;
    DWORD id = 0;
    const HANDLE handle = CreateThread(NULL, 0, &gf_conformance_thread_trampoline, start, 0, &id);
    if (handle == NULL) {
        free(result);
        free(start);
        return EAGAIN;
    }
    (*thread).handle = handle;
    (*thread).id = id;
    (*thread).result = result;
    return 0;
}

/* The thread's result is read only after its handle signals, so the
 * trampoline's store happens before the read. */
static inline int pthread_join(pthread_t thread, void** value) {
    if (thread.handle == NULL || thread.result == NULL) {
        return EINVAL;
    }
    if (WaitForSingleObject(thread.handle, INFINITE) != WAIT_OBJECT_0) {
        return EINVAL;
    }
    (void)CloseHandle(thread.handle);
    if (value != NULL) {
        *value = (*thread.result).value;
    }
    free(thread.result);
    return 0;
}

static inline pthread_t pthread_self(void) {
    pthread_t self;
    self.handle = NULL;
    self.id = GetCurrentThreadId();
    self.result = NULL;
    return self;
}

static inline int pthread_equal(pthread_t left, pthread_t right) {
    const int equal = left.id == right.id;
    return equal;
}

static inline int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attributes) {
    if (attributes != NULL) {
        return EINVAL;
    }
    InitializeSRWLock(&(*mutex).lock);
    return 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t* mutex) {
    AcquireSRWLockExclusive(&(*mutex).lock);
    return 0;
}

static inline int pthread_mutex_unlock(pthread_mutex_t* mutex) {
    ReleaseSRWLockExclusive(&(*mutex).lock);
    return 0;
}

/* An SRW lock owns no kernel object, so there is nothing to release. */
static inline int pthread_mutex_destroy(pthread_mutex_t* mutex) {
    (void)mutex;
    return 0;
}

static inline int pthread_cond_init(pthread_cond_t* condition, const pthread_condattr_t* attributes) {
    if (attributes != NULL) {
        return EINVAL;
    }
    InitializeConditionVariable(&(*condition).variable);
    return 0;
}

static inline int pthread_cond_broadcast(pthread_cond_t* condition) {
    WakeAllConditionVariable(&(*condition).variable);
    return 0;
}

static inline int pthread_cond_signal(pthread_cond_t* condition) {
    WakeConditionVariable(&(*condition).variable);
    return 0;
}

/* A Win32 condition variable owns no kernel object either. */
static inline int pthread_cond_destroy(pthread_cond_t* condition) {
    (void)condition;
    return 0;
}

static inline int pthread_cond_wait(pthread_cond_t* condition, pthread_mutex_t* mutex) {
    const BOOL woke = SleepConditionVariableSRW(&(*condition).variable, &(*mutex).lock, INFINITE, 0);
    if (!woke) {
        return EINVAL;
    }
    return 0;
}

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
typedef int clockid_t;

/* UTC wall-clock time (the UCRT's timespec_get), the only clock the kit
 * reads. */
static inline int clock_gettime(clockid_t clock, struct timespec* now) {
    if (clock != CLOCK_REALTIME) {
        return EINVAL;
    }
    const int base = timespec_get(now, TIME_UTC);
    if (base != TIME_UTC) {
        return EINVAL;
    }
    return 0;
}
#endif

/* Waits until `deadline` (CLOCK_REALTIME) at the latest; ETIMEDOUT when it
 * passed. The remaining time is recomputed from the clock, rounded up to a
 * whole millisecond, so a wake before the deadline is never a timeout. */
static inline int pthread_cond_timedwait(pthread_cond_t* condition, pthread_mutex_t* mutex, const struct timespec* deadline) {
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        return EINVAL;
    }
    const int64_t deadline_ns = (int64_t)(*deadline).tv_sec * 1000000000LL + (int64_t)(*deadline).tv_nsec;
    const int64_t now_ns = (int64_t)now.tv_sec * 1000000000LL + (int64_t)now.tv_nsec;
    if (deadline_ns <= now_ns) {
        return ETIMEDOUT;
    }
    const int64_t remaining_ms = (deadline_ns - now_ns + 999999LL) / 1000000LL;
    DWORD wait_ms = INFINITE - 1U;
    if (remaining_ms < (int64_t)(INFINITE - 1U)) {
        wait_ms = (DWORD)remaining_ms;
    }
    const BOOL woke = SleepConditionVariableSRW(&(*condition).variable, &(*mutex).lock, wait_ms, 0);
    if (woke) {
        return 0;
    }
    if (GetLastError() == ERROR_TIMEOUT) {
        return ETIMEDOUT;
    }
    return EINVAL;
}

#else

#include "gf_conformance_pthread.h"

#endif

#endif
