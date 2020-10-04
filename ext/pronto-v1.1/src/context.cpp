#include <signal.h>
#include <pthread.h>
#include "savitar.hpp"
#include "thread.hpp"
#include "nvm_manager.hpp"
#include "snapshot.hpp"
#include <execinfo.h>

static pthread_t snapshot_thread;
static pthread_mutex_t snapshot_lock;

static void *snapshot_worker(void *arg) {
    Snapshot *snap = (Snapshot *)arg;
    snap->create();
    delete snap;
    return NULL;
}

static void signal_handler(int sig, siginfo_t *si, void *unused) {
    assert(sig == SIGSEGV || sig == SIGUSR1);
    if (sig == SIGSEGV) {
        void *addr = si->si_addr;
        if (!Snapshot::anyActiveSnapshot()) {
            void *array[10];
            size_t size;

            size = backtrace(array, 10);
            fprintf(stderr, "Segmentation fault!\n");
            backtrace_symbols_fd(array, size, STDERR_FILENO);
            exit(1);
        }
        Snapshot::getInstance()->pageFaultHandler(addr);
    }
    else { // SIGUSR1
        pthread_mutex_lock(&snapshot_lock);
        if (!Snapshot::anyActiveSnapshot()) {
            Snapshot *snap = new Snapshot(PMEM_PATH);
            pthread_create(&snapshot_thread, NULL, snapshot_worker, snap);
        }
        pthread_mutex_unlock(&snapshot_lock);
    }
}

typedef struct main_arguments {
    MainFunction main;
    int argc;
    char **argv;
} MainArguments;

static void *main_wrapper(void *arg) {
    MainArguments *args = (MainArguments *)arg;
    int *status = (int *)malloc(sizeof(int));
    *status = args->main(args->argc, args->argv);
    return status;
}

int Savitar_main(MainFunction main_function, int argc, char **argv) {

// #ifndef SYNC_SL
    Savitar_core_init();
// #endif // SYNC_SL
    NVManager::getInstance(); // recover persistent objects (blocking)

    // Register signal handler for snapshots
    pthread_mutex_init(&snapshot_lock, NULL);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = signal_handler;
    assert(sigaction(SIGSEGV, &sa, NULL) == 0);
    assert(sigaction(SIGUSR1, &sa, NULL) == 0);

    int *status;
    pthread_t main_thread;
    MainArguments args = {
        .main = main_function,
        .argc = argc,
        .argv = argv
    };
    Savitar_thread_create(&main_thread, NULL, main_wrapper, &args);
    pthread_join(main_thread, (void **)&status);

    // Wait for active snapshots to complete
    pthread_mutex_lock(&snapshot_lock);
    if (Snapshot::anyActiveSnapshot()) {
        pthread_join(snapshot_thread, NULL);
    }
    pthread_mutex_unlock(&snapshot_lock);

// #ifndef SYNC_SL
    Savitar_core_finalize();
// #endif // SYNC_SL
    pthread_mutex_destroy(&snapshot_lock);

    int ret_val = *status;
    free(status);

    return ret_val;
}
