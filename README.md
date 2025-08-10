# User-Level Threads Library in C

This repository contains a static library implementing user-level threads in C, featuring cooperative multitasking with a 
Round-Robin (RR) scheduler. The library manages thread creation, scheduling, blocking, termination, and context switching without 
using dynamic memory allocation or native threading libraries.

---

## Features

- Supports up to `MAX_THREAD_NUM` static user threads, including the main thread.  
- Each thread has its own stack (except the main thread).  
- Threads managed with states: RUNNING, READY, BLOCKED, TERMINATED.  
- Preemption using virtual timer signals (`ITIMER_VIRTUAL`) and signal handling.  
- Context switching implemented via `sigsetjmp`/`siglongjmp` with stack and PC manipulation.  
- Scheduler enforces fixed quantum per thread and uses Round-Robin policy.  
- Thread IDs are assigned as the smallest unused non-negative integer.  
- Signal masking used to protect critical sections and prevent context switch during state updates.  
- No use of dynamic memory (malloc), pthreads, or modifying provided header files.  
- Error reporting to `stderr` with clear messages and proper exit codes on system call failures.

---

## Project Structure

- `uthreads.h` — API header (do not modify)  
- `uthreads.c` — Implementation of the user-level threads library  
- Additional helper files as needed (all static memory, no dynamic allocation)  
- `testuthreads.c` — (Not submitted) User-written tests validating library correctness  

---

## Usage

- Call `uthread_init(quantum_usecs)` to initialize the library and main thread.  
- Use `uthread_spawn(thread_entry_point func)` to create new threads.  
- Control threads with API functions for blocking, resuming, terminating, and yielding.  
- The library handles scheduling and preemption automatically based on quantum expiration.  
- Compile with:  
  ```bash
  gcc -std=c23 -o your_program your_program.c uthreads.c

