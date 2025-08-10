#include "uthreads.h"

// Thread 1: Busy-loop continuously printing its status.
void thread_function1(void) {
    int counter = 1;
    while (1) {
        printf("[Thread 1] Running, iteration %d. Total quantums: %d, My quantums: %d\n",
               counter,
               uthread_get_total_quantums(),
               uthread_get_quantums(uthread_get_tid()));
        // Busy work loop to simulate computation.
        for (volatile long i = 0; i < 5000000; i++);
        counter++;
    }
}

// Thread 2: Sleeps for 3 quantums initially and then runs.
void thread_function2(void) {
    printf("[Thread 2] Started. Sleeping for 3 quantums...\n");
    uthread_sleep(3);
    printf("[Thread 2] Woke up from sleep. Continuing execution...\n");
    int counter = 1;
    while (1) {
        printf("[Thread 2] Running, iteration %d. Total quantums: %d, My quantums: %d\n",
               counter,
               uthread_get_total_quantums(),
               uthread_get_quantums(uthread_get_tid()));
        // Busy work loop.
        for (volatile long i = 0; i < 5000000; i++);
        counter++;
    }
}

// Thread 3: Runs continuously and sleeps for 2 quantums every 4 iterations.
void thread_function3(void) {
    int counter = 1;
    while (1) {
        printf("[Thread 3] Running, iteration %d. Total quantums: %d, My quantums: %d\n",
               counter,
               uthread_get_total_quantums(),
               uthread_get_quantums(uthread_get_tid()));
        // Every 4 iterations, sleep for 2 quantums.
        if (counter % 4 == 0) {
            printf("[Thread 3] Sleeping for 2 quantums...\n");
            uthread_sleep(2);
            printf("[Thread 3] Woke up from sleep.\n");
        }
        // Busy work loop.
        for (volatile long i = 0; i < 5000000; i++);
        counter++;
    }
}

// Thread 4: Continuously runs and prints its status.
void thread_function4(void) {
    int counter = 1;
    while (1) {
        printf("[Thread 4] Running, iteration %d. Total quantums: %d, My quantums: %d\n",
               counter,
               uthread_get_total_quantums(),
               uthread_get_quantums(uthread_get_tid()));
        // Busy work loop.
        for (volatile long i = 0; i < 5000000; i++);
        counter++;
    }
}

int main(void) {
    // Initialize the thread library with a quantum time of 100,000 microseconds (0.1 seconds).
    if (uthread_init(100000) == -1) {
        return 1;
    }

    // Spawn four threads.
    int tid1 = uthread_spawn(thread_function1);
    if (tid1 == -1) {
        fprintf(stderr, "Failed to spawn thread 1\n");
    }
    
    int tid2 = uthread_spawn(thread_function2);
    if (tid2 == -1) {
        fprintf(stderr, "Failed to spawn thread 2\n");
    }
    
    int tid3 = uthread_spawn(thread_function3);
    if (tid3 == -1) {
        fprintf(stderr, "Failed to spawn thread 3\n");
    }
    
    int tid4 = uthread_spawn(thread_function4);
    if (tid4 == -1) {
        fprintf(stderr, "Failed to spawn thread 4\n");
    }

    // Main thread: perform busy work so that it actively consumes CPU cycles.
    int counter = 1;
    while (1) {
        for (volatile long i = 0; i < 10000000; i++);  // Busy loop to consume CPU time.
        printf("[Main Thread] Running, iteration %d. Total quantums: %d, My quantums: %d\n",
               counter,
               uthread_get_total_quantums(),
               uthread_get_quantums(uthread_get_tid()));
        counter++;

        if (counter == 500) {
            printf("[Main Thread] Shutting down user-level threads\n");
            uthread_terminate(tid1);
            uthread_terminate(tid2);
            uthread_terminate(tid3);
            uthread_terminate(tid4);
            printf("[Main Thread] Shutdown succeeded\n");
            printf("[Main Thread] Terminting Main Thread\n");
            uthread_terminate(0);
        }
    }

    return 0;
}
