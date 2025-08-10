#ifndef _UTHREADS_H
#define _UTHREADS_H

#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>


/* ===================================================================== */
/*                           Static Constants                            */
/* ===================================================================== */

/** Maximum number of threads (including the main thread). */
#define MAX_THREAD_NUM 100

/** Stack size per thread (in bytes). */
#define STACK_SIZE 4096

/**
 * @brief Function pointer type for a thread's entry point.
 *
 * Each thread's entry function must take no arguments and return void.
 */
typedef void (*thread_entry_point)(void);

/* ===================================================================== */
/*                        Internal Data Structures                       */
/* ===================================================================== */

/**
 * @brief Enumeration of possible thread states.
 */
typedef enum {
    THREAD_UNUSED = 0, /**< Slot is unused. */
    THREAD_READY,      /**< Thread is ready to run. */
    THREAD_RUNNING,    /**< Thread is currently executing. */
    THREAD_BLOCKED,    /**< Thread is blocked (explicitly or sleeping). */
    THREAD_TERMINATED  /**< Thread has finished execution (internal use only). */
} thread_state_t;

/**
 * @brief Thread Control Block (TCB)
 *
 * Each thread (except for the main thread) has its own allocated stack and context.
 * The TCB stores all metadata required for managing the thread.
 */
typedef struct {
    int tid;                    /**< Unique thread identifier. */
    thread_state_t state;       /**< Current thread state. */
    sigjmp_buf env;             /**< Jump buffer for context switching using sigsetjmp/siglongjmp. */
    int quantums;               /**< Count of quantums this thread has executed. */
    int sleep_until;            /**< Global quantum count until which the thread should sleep (0 if not sleeping). */
    thread_entry_point entry;   /**< Entry point function for the thread. */
} thread_t;

/* ===================================================================== */
/*                           External Interface                          */
/* ===================================================================== */

/**
 * @brief Initializes the user-level thread library.
 *
 * This function must be called before any other thread library function.
 * It sets up internal data structures, initializes the main thread (tid == 0) as running,
 * and configures the timer for quantum management. The main thread uses the process's regular stack.
 *
 * @param quantum_usecs Length of a quantum in microseconds (must be positive).
 * @return 0 on success; -1 on error (e.g., if quantum_usecs is non-positive).
 */
int uthread_init(int quantum_usecs);

/**
 * @brief Creates a new thread.
 *
 * Allocates a new TCB and separate stack for the thread (using a char array).
 * The thread is added to the end of the READY queue.
 * Calling this function with a NULL entry_point or exceeding MAX_THREAD_NUM is an error.
 *
 * @param entry_point Pointer to the thread’s entry function (must not be NULL).
 * @return On success, returns the new thread’s ID; on failure, returns -1.
 */
int uthread_spawn(thread_entry_point entry_point);

/**
 * @brief Terminates a thread.
 *
 * Terminates the thread with the specified tid and releases all resources allocated for it.
 * The thread is removed from all scheduling structures. If no thread with the given tid exists,
 * it is considered an error. Terminating the main thread (tid == 0) will terminate the entire process
 * (after releasing allocated resources).
 *
 * @param tid Thread ID to terminate.
 * @return 0 on success; -1 on error. (Note: if a thread terminates itself or if the main thread terminates,
 * the function does not return.)
 */
int uthread_terminate(int tid);

/**
 * @brief Blocks a thread.
 *
 * Moves the thread with the given tid into the BLOCKED state.
 * A blocked thread may later be resumed using uthread_resume.
 * It is an error to block a thread that does not exist or to block the main thread (tid == 0).
 * Blocking a thread that is already BLOCKED is a no-op.
 *
 * @param tid Thread ID to block.
 * @return 0 on success; -1 on error.
 */
int uthread_block(int tid);

/**
 * @brief Resumes a blocked thread.
 *
 * Moves a thread from the BLOCKED state to the READY state.
 * If the thread is already in RUNNING or READY state, this call has no effect.
 * It is an error if no thread with the given tid exists.
 *
 * @param tid Thread ID to resume.
 * @return 0 on success; -1 on error.
 */
int uthread_resume(int tid);

/**
 * @brief Puts the running thread to sleep.
 *
 * Blocks the currently running thread for a specified number of quantums.
 * The current quantum is not counted; sleeping begins with the next quantum.
 * After the sleep period expires, the thread is moved to the end of the READY queue.
 * It is an error for the main thread (tid == 0) to call this function.
 *
 * @param num_quantums Number of quantums to sleep.
 * @return 0 on success; -1 on error.
 */
int uthread_sleep(int num_quantums);

/**
 * @brief Returns the calling thread's ID.
 *
 * @return The thread ID of the calling thread.
 */
int uthread_get_tid();

/**
 * @brief Returns the total number of quantums since the library was initialized.
 *
 * The count starts at 1 immediately after uthread_init. Every new quantum, regardless of cause,
 * increments this counter.
 *
 * @return Total number of quantums.
 */
int uthread_get_total_quantums();

/**
 * @brief Returns the number of quantums the thread with the specified tid has run.
 *
 * For a thread in the RUNNING state, the current quantum is included.
 * An error is returned if no thread with the given tid exists.
 *
 * @param tid Thread ID.
 * @return Number of quantums for the specified thread; -1 on error.
 */
int uthread_get_quantums(int tid);

/* ===================================================================== */
/*              Internal Helper Functions and Structures                 */
/* ===================================================================== */
/*
 * The following declarations are intended for internal use by the thread library.
 * They provide guidance on how to structure your implementation using sigsetjmp/siglongjmp
 * and manually managed stacks.
 */

/**
 * @brief Scheduler: Selects the next thread to run.
 *
 * This function examines the READY queue and selects the next thread for execution.
 * It handles state transitions and triggers a context switch.
 */
void schedule_next(void);

/**
 * @brief Context switch helper.
 *
 * Uses sigsetjmp and siglongjmp to save the current thread's context and restore the context of the next thread.
 *
 * @param current Pointer to the current thread's TCB.
 * @param next Pointer to the next thread's TCB.
 */
void context_switch(thread_t *current, thread_t *next);

/**
 * @brief Timer signal handler.
 *
 * Registered as the handler for timer signals, this function updates global quantum counters
 * and initiates a scheduling decision when a quantum expires.
 *
 * @param signum The signal number (e.g., SIGVTALRM).
 */
void timer_handler(int signum);

/**
 * @brief Initializes a thread's jump buffer.
 *
 * Sets up the thread’s jump buffer for context switching by manually assigning the stack pointer
 * and program counter using sigsetjmp/siglongjmp techniques. You may need to perform architecture-specific
 * address translation (see provided reference implementation) when initializing the context.
 *
 * @param tid Thread ID.
 * @param stack Pointer to the thread's allocated stack (a char array).
 * @param entry_point Pointer to the thread's entry function.
 */
void setup_thread(int tid, char *stack, thread_entry_point entry_point);

/* ===================================================================== */
/*                     testing purposes section                          */
/* ===================================================================== */


void debug_print_threads_state(void);

int uthread_yield(void);

int uthread_get_state(int tid);

int uthread_any_ready(); 


#endif /* _UTHREADS_H */
