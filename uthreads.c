#include "uthreads.h"


#define FAILURE -1 // For better code understanding.
#define SUCCESS 0 // For better code understanding.
#define READY_QUEUE_CAPACITY MAX_THREAD_NUM // Max size of the Ready queque.

typedef char thread_stack_t[STACK_SIZE] __attribute__((aligned(16)));
static thread_t threads[MAX_THREAD_NUM]; // All thread slots.
static int current_tid = 0; // The thread currently running.
static thread_stack_t stacks[MAX_THREAD_NUM]; // Stacks for user threads (not used for main thread)

static int total_quantums = 0; // counts how many quantum intervals have occurred since uthread_init() was called.
static int g_quantum_usecs = 0; // Stores the quantum length, in microseconds.
static struct itimerval timer; // This struct holds the configuration for the virtual timer used by setitimer().

/* ===================================================================== */
/*             array-based circular queue implementation                 */
/* ===================================================================== */

//  for ready threads (holds thread IDs).
static int ready_queue[READY_QUEUE_CAPACITY];
static int ready_front = 0;  // index to dequeue.
static int ready_rear = 0;   // index to enqueue.
static int ready_size = 0;   // current number of items in queue.

/**
 * Enqueues a thread ID into the ready queue.
 * 
 * This function adds the given thread ID (tid) to the ready queue,
 * which is a circular queue implemented with an array.
 * 
 * To prevent race conditions and data corruption caused by
 * asynchronous timer interrupts (SIGVTALRM), this function
 * temporarily blocks SIGVTALRM signals while modifying the queue.
 * 
 * Signal masking ensures that the timer handler does not preempt
 * the current thread during the critical update of shared queue state,
 * making the enqueue operation atomic from the scheduler’s perspective.
 * 
 * @param tid The thread ID to enqueue.
 * @return 0 on success, -1 if the queue is full.
 */
static int enqueue_thread(int tid) {
    sigset_t sigset;
    // Block SIGVTALRM.
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    if (sigprocmask(SIG_BLOCK, &sigset, NULL) < 0) {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }

    if (ready_size == READY_QUEUE_CAPACITY) {
        // Queue is full - cannot enqueue.
        return -1;
    }
    ready_queue[ready_rear] = tid;
    ready_rear = (ready_rear + 1) % READY_QUEUE_CAPACITY; // Using % READY_QUEUE_CAPACITY wraps it back to 0, making the queue "circular."
    ready_size++;
    
    // Unblock signals.
    if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) < 0) {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }
    return 0;
}

/**
 * Dequeues a thread ID from the ready queue.
 * 
 * This function removes and returns the thread ID at the front of the ready queue.
 * It uses a circular buffer implementation to track the queue.
 * 
 * Similar to enqueue, it blocks SIGVTALRM signals during the critical
 * section to avoid race conditions with the timer interrupt.
 * 
 * @return The dequeued thread ID on success, or -1 if the queue is empty.
 */
static int dequeue_thread() {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    if (sigprocmask(SIG_BLOCK, &sigset, NULL) < 0) {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }
    
    if (ready_size == 0) {
        // Queue is empty
        return -1;
    }
    int tid = ready_queue[ready_front];
    ready_front = (ready_front + 1) % READY_QUEUE_CAPACITY;
    ready_size--;

    if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) < 0) {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }
    return tid;
}

// Check if ready queue is empty
static int is_ready_queue_empty() {
    return (ready_size == 0);
}

// Testing and printing purposes.
void print_ready_queue() {
    printf("Ready Queue Status:\n");
    printf("  Size: %d\n", ready_size);
    printf("  Front index: %d\n", ready_front);
    printf("  Rear index: %d\n", ready_rear);
    printf("  Contents: [ ");
    int idx = ready_front;
    for (int i = 0; i < ready_size; i++) {
        printf("%d ", ready_queue[idx]);
        idx = (idx + 1) % READY_QUEUE_CAPACITY;
    }
    printf("]\n");
}

int uthread_init(int quantum_usecs) {
    // Validation of the input duration.
    if (quantum_usecs <= 0) {
        fprintf(stderr, "thread library error: quantom must be positive.\n");
        return FAILURE;
    }
    
    // Store the quantom length and reset the global quantom counter.
    g_quantum_usecs = quantum_usecs;
    total_quantums = 1;

    // Set up the signal handler for SIGVTALRM.
    struct sigaction sa;  // zero-initializes all fields
    sa.sa_handler = timer_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGVTALRM, &sa, NULL) < 0) {
        fprintf(stderr, "system error: sigaction failed\n");
        return FAILURE;
    }

    // Configuring the timer (virtual time only).
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = g_quantum_usecs;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = g_quantum_usecs;

    if (setitimer(ITIMER_VIRTUAL, &timer, NULL) < 0) {
        fprintf(stderr, "system error: sigaction failed\n");
        return FAILURE;
    }

    // Initialize main thread (tid 0) metadata based on the struct defined and given in uthreads.h.
    threads[0].tid = 0;
    threads[0].state = THREAD_RUNNING;
    threads[0].quantums = 1;
    threads[0].sleep_until = 0;
    threads[0].entry = NULL;  // main thread has no function pointer
    current_tid = 0;

    return SUCCESS;
}

int uthread_spawn(thread_entry_point entry_point) {
    // Entry point validation.
    if (entry_point == NULL) {
        fprintf(stderr, "thread library error: entry_point is null.\n");
        return FAILURE;
    }
    
    // Block SIGVTALRM before modifying shared data.
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    if (sigprocmask(SIG_BLOCK, &sigset, NULL) < 0) {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }

    // Find a free thread slot - tid. guaratnees that the first empty slot will be filled.
    int tid = -1;
    for (int i = 0; i < MAX_THREAD_NUM; i++) {
        if (threads[i].state == THREAD_UNUSED) {
            tid = i;
            break;
        }
    }
    // If we reached here when tid = -1 -> no free slot. exceeding MAX_THREAD_NUM is an error.
    if (tid == -1) {
        fprintf(stderr, "thread library error: no available thread slots.\n");
        // Unblock before return.
        if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) < 0) {
            fprintf(stderr, "system error: masking failed\n");
            exit(1);
        }
        return FAILURE;
    }

    // Allocate stack and setup thread context.
    char *stack = stacks[tid]; // gets the address of the first byte of that thread’s stack array.
    setup_thread(tid, stack, entry_point);

    // Initialize thread metadata.
    threads[tid].tid = tid;
    threads[tid].state = THREAD_READY;
    threads[tid].quantums = 0;
    threads[tid].sleep_until = 0;
    threads[tid].entry = entry_point;

    // Enqueue thread ID to Ready Queue.
    if (enqueue_thread(tid) < 0) {
        fprintf(stderr, "thread library error: Ready queue is full, cannot enqueue thread %d.\n", tid);
        // reset thread slot if enqueue fails.
        threads[tid].state = THREAD_UNUSED;
        // Unblock before return.
        if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) < 0) {
            fprintf(stderr, "system error: masking failed\n");
            exit(1);
        }
        return FAILURE;
    }
    
    // Unblock signals after finishing.
    if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) < 0) {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }
    return tid;
}

int uthread_terminate(int tid) {
    if (tid < 0 || tid >= MAX_THREAD_NUM || threads[tid].state == THREAD_UNUSED) {
        fprintf(stderr, "thread library error: invalid or already terminated thread %d. \n" , tid);
        return FAILURE;
    }
     
     // Case of main thread.
     if (tid == 0) {
        // Terminate main thread -> kill program.
        exit(0);
     }
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);

    // Block SIGVTALRM before modifying shared state.
    if (sigprocmask(SIG_BLOCK, &sigset, NULL) < 0) {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }
     
     // Current thread case.
     if (tid == current_tid) {
        threads[tid].state = THREAD_UNUSED; // meaning it's terminated and can be reused by a future uthread_spawn().
        threads[tid].entry = NULL;
        threads[tid].quantums = 0;
        threads[tid].sleep_until = 0;
        
        // Unblock before scheduling next to avoid deadlock.
        if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) < 0) {
            fprintf(stderr, "system error: masking failed\n");
            exit(1);
        }

        // Signals must be unblocked before calling schedule_next.
        schedule_next(); // we must immediately switch to a new thread, since a running thread can’t "terminate itself".
        return SUCCESS;
     }

     // initializes a temporary queue (new_queue) that will hold only the threads we want to keep -> not the one we're terminating.
     int new_queue[READY_QUEUE_CAPACITY]; 
     int new_rear = 0;

     // We can't remove from the middle of the circular queue easily,
     // so we build a new queue without the target tid, and copy it over.
     for ( int i = 0; i < ready_size; i++) {
        int index = (ready_front + i) % READY_QUEUE_CAPACITY; // actual index inside the circular queue.
        if (ready_queue[index] != tid) {
            new_queue[new_rear++] = ready_queue[index]; // adds the current thread ID to a new queue if it's not the one we're removing.
        }  
     }

     // Copy the filtered queue.
     memcpy(ready_queue, new_queue, sizeof(new_queue)); // Copy all contents of new_queue into ready_queue.
     ready_front = 0;
     ready_rear = new_rear;
     ready_size = new_rear;

     threads[tid].state = THREAD_UNUSED;

    // Unblock signals after modifications.
    if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) < 0) {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }

     return SUCCESS;
}

int uthread_block(int tid) {
    // Validation.   
    if (tid < 0 || tid >= MAX_THREAD_NUM || threads[tid].state == THREAD_UNUSED) {
        fprintf(stderr, "thread library error: Cannot block thread %d (invalid or terminated). \n" , tid);
        return FAILURE;
    }
    
    // Main thread blocking tryout case.
    if (tid == 0) {
        fprintf(stderr, "thread library error: cannot block main thread (tid 0).\n");
        return FAILURE;
    }
    
    // Block SIGVTALRM before modifying shared state.
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    if (sigprocmask(SIG_BLOCK, &sigset, NULL) < 0) {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }

    // Allready blocked thread case, alligining with that blocking a thread that is already BLOCKED is a no-op.
    if (threads[tid].state == THREAD_BLOCKED) {
        if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) < 0) {
            fprintf(stderr, "system error: masking failed\n");
            exit(1);
        }
        return SUCCESS;
    }

    // Current thread case.
    if (tid == current_tid) {
        // Blocking the running thread — switch immediately.
        threads[tid].state = THREAD_BLOCKED;
        if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) < 0) {
            fprintf(stderr, "system error: masking failed\n");
            exit(1);
        }
        // unblock before calling schedule_next() on the current thread case, to avoid deadlock.
        schedule_next();
        return SUCCESS;
    }

    if (threads[tid].state == THREAD_READY) {
        // Removing from the ready queue with similar implementation to uthread_terminate.
        int new_queue[READY_QUEUE_CAPACITY]; 
        int new_rear = 0;
        for ( int i = 0; i < ready_size; i++) {
            int index = (ready_front + i) % READY_QUEUE_CAPACITY; // actual index inside the circular queue.
            if (ready_queue[index] != tid) {
                new_queue[new_rear++] = ready_queue[index]; // adds the current thread ID to a new queue if it's not the one we're removing.
            }  
        }

        // Copy the filtered queue.
        memcpy(ready_queue, new_queue, sizeof(new_queue)); // Copy all contents of new_queue into ready_queue.
        ready_front = 0;
        ready_rear = new_rear;
        ready_size = new_rear; // new_rear exactly counts how many threads we preserved.

        threads[tid].state = THREAD_BLOCKED;

        if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) < 0) {
            fprintf(stderr, "system error: masking failed\n");
            exit(1);
        }
        return SUCCESS;
    }
    // Unblock before return for other states.
    if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) < 0) {
            fprintf(stderr, "system error: masking failed\n");
            exit(1);
        }

    return SUCCESS;  
}

int uthread_resume(int tid) {
    // Validation.   
    if (tid < 0 || tid >= MAX_THREAD_NUM || threads[tid].state == THREAD_UNUSED) {
        fprintf(stderr, "thread library error: Cannot resume thread %d (invalid or does not exist). \n" , tid);
        return FAILURE;
    }
    
    // Case of running thread or already ready thread - no effect.
    if (threads[tid].state == THREAD_RUNNING || threads[tid].state == THREAD_READY) {
        return SUCCESS;
    } 

    // Moving from BLOCKED to ready.
    if (threads[tid].state == THREAD_BLOCKED) {
        threads[tid].state = THREAD_READY;
        enqueue_thread(tid);
    }

    return SUCCESS; 
}  

int uthread_sleep(int num_quantums) {
    // Validate input.
    if (num_quantums <= 0) {
        fprintf(stderr, "thread library error: sleep duration must be positive.\n");
        return FAILURE;
    }

    // Main thread is not allowed to sleep.
    if (current_tid == 0) {
        fprintf(stderr, "thread library error: main thread (tid 0) cannot call sleep().\n");
        return FAILURE;
    }

    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    if (sigprocmask(SIG_BLOCK, &sigset, NULL) < 0) {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }

    // Mark the currently running thread as sleeping.
    threads[current_tid].state = THREAD_BLOCKED;
    threads[current_tid].sleep_until = total_quantums + num_quantums;

    if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) < 0) {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }

    // Context switch to next ready thread.
    schedule_next();

    return SUCCESS;
}


int uthread_get_tid() {
    return current_tid;
}

int uthread_get_total_quantums() {
    return total_quantums;
}

int uthread_get_quantums(int tid) {
    if (tid < 0 || tid >= MAX_THREAD_NUM || threads[tid].state == THREAD_UNUSED) {
        fprintf(stderr, "thread library error: Invalid TID %d in uthread_get_quantums().\n", tid);
        return FAILURE;
    }
    return threads[tid].quantums;
}

void schedule_next(void) {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);

    // Block SIGVTALRM to prevent race conditions during scheduling.
    if (sigprocmask(SIG_BLOCK, &sigset, NULL) < 0) {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }

    int ret = sigsetjmp(threads[current_tid].env, 1);
    if (ret == 1) {
        // If we entered here, we came back through siglongjmp.
        if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) < 0) {
            fprintf(stderr, "system error: masking failed\n");
            exit(1);
        }
        return;
    }
    
    // If the current thread is still in RUNNING state, ant not blocked or sleeping -> mark as Ready and re-enqueue.
    if (threads[current_tid].state == THREAD_RUNNING &&  threads[current_tid].sleep_until == 0) {
        threads[current_tid].state = THREAD_READY;
        enqueue_thread(current_tid);
    }

    // Next thread dequeue for running.
    int next_tid = dequeue_thread();
    if (next_tid == -1) {
        fprintf(stderr, "thread library error: no threads to schedule\n");
        exit(1); // There is no one to run.
    }

    threads[next_tid].state = THREAD_RUNNING; // After dequeue - thread went from READY to RUNNING.
    threads[next_tid].quantums++;
    int prev_tid = current_tid;
    current_tid = next_tid;

    // Unblock signals before context switch.
    if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) < 0) {
            fprintf(stderr, "system error: masking failed\n");
            exit(1);
        }
    context_switch(&threads[prev_tid], &threads[next_tid]);
}

void context_switch(thread_t *current, thread_t *next) {
    siglongjmp(next->env, 1); // This way we switch to the next thread.
}

void timer_handler(int signum) {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);

    // Block SIGVTALRM to protect critical section.
    if (sigprocmask(SIG_BLOCK, &sigset, NULL) < 0) {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }
    total_quantums++;
    // Waking up sleeping threads.
    for (int i = 0; i < MAX_THREAD_NUM; i++) {
        if (threads[i].state == THREAD_BLOCKED && threads[i].sleep_until > 0 && threads[i].sleep_until <= total_quantums) {
            threads[i].sleep_until = 0;
            threads[i].state = THREAD_READY;
            enqueue_thread(threads[i].tid);
        }
    }
    // Unblock SIGVTALRM after finishing critical section.
    if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) < 0) {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }
    schedule_next();
}

/* ===================================================================== */
/*        setup_thread from provided reference implementation            */
/* ===================================================================== */

typedef unsigned long address_t;

#define JB_SP 6  // Index of Stack Pointer in jmp_buf.
#define JB_PC 7  // Index of Program Counter in jmp_buf.

/**
 * @brief Translates an address for use in a jump buffer.
 * This translation is needed due to how addresses are handled by setjmp/longjmp on x86_64.
 * The xor + rol operations handle address randomization/segmentation.
 */
address_t translate_address(address_t addr) {
    address_t ret;
    asm volatile("xor %%fs:0x30, %0\n"
                 "rol $0x11, %0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

 /** 
 * @brief Initializes the context of a new thread.
 *
 * This function prepares the jump buffer (`env`) of a thread so that when `siglongjmp`
 * is later called on it, execution begins at the thread's entry point and uses its assigned stack.
 *
 * @param tid The thread ID being initialized.
 * @param stack Pointer to the start of the thread's allocated stack memory.
 * @param entry_point Pointer to the function the thread should run when scheduled.
 */
void setup_thread(int tid, char *stack, thread_entry_point entry_point) {
    address_t sp = (address_t)stack + STACK_SIZE - sizeof(address_t); // stack top.
    sp = sp & ~0xF; // Align down to nearest 16-byte boundary.
    address_t pc = (address_t)entry_point;
    if (sigsetjmp(threads[tid].env, 1) == 0) {
        threads[tid].env->__jmpbuf[JB_SP] = translate_address(sp);
        threads[tid].env->__jmpbuf[JB_PC] = translate_address(pc);
        sigemptyset(&threads[tid].env->__saved_mask);
    }
}

/* ===================================================================== */
/*                     testing purposes section                          */
/* ===================================================================== */


void debug_print_threads_state() {
    for (int i = 0; i < MAX_THREAD_NUM; i++) {
        printf("Thread %d: state=%d, tid=%d, quantums=%d\n",
            i, threads[i].state, threads[i].tid, threads[i].quantums);
    }
}

int uthread_yield(void) {
    // printf("[DEBUG] uthread_yield called by tid=%d\n", uthread_get_tid());
    if (is_ready_queue_empty()) {
        // printf("[DEBUG] yield skipped — no ready threads\n");
        return 0;
    }
    schedule_next();
    return 0;
}

int uthread_get_state(int tid) {
    if (tid < 0 || tid >= MAX_THREAD_NUM) {
        return -1; // invalid tid
    }
    return threads[tid].state;
}

int uthread_any_ready() {
    return !is_ready_queue_empty();
}


