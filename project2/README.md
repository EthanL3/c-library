# Thread Library:

For this project, I implemented a basic threading system with 3 functions (pthread_create, pthread_delete, pthread_self). I organized the threads via an 128 size array of TCB (thread control blocks). The thread control block stores the thread id, context (registers and stack), stack pointer, and status of each thread. After a thread is created, the scheduler schedules it and every 50 ms after that, it switches to a new thread (if any). Scheduling is done via round robin, giving each thread a fair and equal share. The scheduler is triggered via the SIGALARM signal, which is set to go off every 50 ms. The custom signal handler for this alarm is the schedule() function.
The biggest challenge I faced when completing this project was figuring out when to initialize the signal handler to the schedule() function. At first, I tried doing this at the beginning of the first thread (inside init_thread_sys). However, this led to segmentation faults, since the signal handler got initialized before any thread was created, so when schedule() first ran after 50 ms, there was no thread to call setjmp on. After that, I realized I needed to put at the end pthread_create, but only on the first time pthread_create runs. Since new_thread_id would be 1 in the first time pthread_create runs (main() has thread id 0), I just checked for that condition using an if statement and then initalized the signal handler there.