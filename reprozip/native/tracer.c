#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/ptrace.h>
#include <sys/reg.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>

#include "config.h"
#include "database.h"
#include "log.h"
#include "ptrace_utils.h"
#include "syscalls.h"
#include "tracer.h"
#include "utils.h"
#include "vector.h"


#ifndef NT_PRSTATUS
#define NT_PRSTATUS 1
#endif


struct i386_regs {
    int32_t ebx;
    int32_t ecx;
    int32_t edx;
    int32_t esi;
    int32_t edi;
    int32_t ebp;
    int32_t eax;
    int32_t xds;
    int32_t xes;
    int32_t xfs;
    int32_t xgs;
    int32_t orig_eax;
    int32_t eip;
    int32_t xcs;
    int32_t eflags;
    int32_t esp;
    int32_t xss;
};


struct x86_64_regs {
    int64_t r15;
    int64_t r14;
    int64_t r13;
    int64_t r12;
    int64_t rbp;
    int64_t rbx;
    int64_t r11;
    int64_t r10;
    int64_t r9;
    int64_t r8;
    int64_t rax;
    int64_t rcx;
    int64_t rdx;
    int64_t rsi;
    int64_t rdi;
    int64_t orig_rax;
    int64_t rip;
    int64_t cs;
    int64_t eflags;
    int64_t rsp;
    int64_t ss;
    int64_t fs_base;
    int64_t gs_base;
    int64_t ds;
    int64_t es;
    int64_t fs;
    int64_t gs;
};


static void get_i386_reg(register_type *reg, uint32_t value)
{
    reg->i = (int32_t)value;
    reg->u = value;
    reg->p = (void*)(uint64_t)value;
}

static void get_x86_64_reg(register_type *reg, uint64_t value)
{
    reg->i = (int64_t)value;
    reg->u = value;
    reg->p = (void*)value;
}


int trace_verbosity = 0;
#define verbosity trace_verbosity


void free_execve_info(struct ExecveInfo *execi)
{
    free_strarray(execi->argv);
    free_strarray(execi->envp);
    free(execi->binary);
    free(execi);
}


struct Process **processes = NULL;
size_t processes_size;

struct Process *trace_find_process(pid_t tid)
{
    size_t i;
    for(i = 0; i < processes_size; ++i)
    {
        if(processes[i]->status != PROCSTAT_FREE && processes[i]->tid == tid)
            return processes[i];
    }
    return NULL;
}

struct Process *trace_get_empty_process(void)
{
    size_t i;
    for(i = 0; i < processes_size; ++i)
    {
        if(processes[i]->status == PROCSTAT_FREE)
            return processes[i];
    }

    /* Count unknown processes */
    if(verbosity >= 3)
    {
        size_t unknown = 0;
        for(i = 0; i < processes_size; ++i)
            if(processes[i]->status == PROCSTAT_UNKNOWN)
                ++unknown;
        log_debug(0, "there are %u/%u UNKNOWN processes",
                  (unsigned int)unknown, (unsigned int)processes_size);
    }

    /* Allocate more! */
    if(verbosity >= 3)
        log_debug(0, "process table full (%d), reallocating",
                  (int)processes_size);
    {
        struct Process *pool;
        size_t prev_size = processes_size;
        processes_size *= 2;
        pool = malloc((processes_size - prev_size) * sizeof(*pool));
        processes = realloc(processes, processes_size * sizeof(*processes));
        for(; i < processes_size; ++i)
        {
            processes[i] = pool++;
            processes[i]->status = PROCSTAT_FREE;
            processes[i]->threadgroup = NULL;
            processes[i]->execve_info = NULL;
        }
        return processes[prev_size];
    }
}

struct ThreadGroup *trace_new_threadgroup(pid_t tgid, char *wd)
{
    struct ThreadGroup *threadgroup = malloc(sizeof(struct ThreadGroup));
    threadgroup->tgid = tgid;
    threadgroup->wd = wd;
    threadgroup->refs = 1;
    if(verbosity >= 3)
        log_debug(tgid, "threadgroup (= process) created");
    return threadgroup;
}

void trace_free_process(struct Process *process)
{
    process->status = PROCSTAT_FREE;
    if(process->threadgroup != NULL)
    {
        process->threadgroup->refs--;
        if(verbosity >= 3)
            log_debug(process->tid,
                      "process died, threadgroup tgid=%d refs=%d",
                      process->threadgroup->tgid, process->threadgroup->refs);
        if(process->threadgroup->refs == 0)
        {
            if(verbosity >= 3)
                log_debug(process->threadgroup->tgid,
                          "deallocating threadgroup");
            if(process->threadgroup->wd != NULL)
                free(process->threadgroup->wd);
            free(process->threadgroup);
        }
        process->threadgroup = NULL;
    }
    else if(verbosity >= 3)
        log_debug(process->tid, "threadgroup==NULL");
    if(process->execve_info != NULL)
    {
        free_execve_info(process->execve_info);
        process->execve_info = NULL;
    }
}

void trace_count_processes(unsigned int *p_nproc, unsigned int *p_unknown)
{
    unsigned int nproc = 0, unknown = 0;
    size_t i;
    for(i = 0; i < processes_size; ++i)
    {
        switch(processes[i]->status)
        {
        case PROCSTAT_FREE:
            break;
        case PROCSTAT_UNKNOWN:
            /* Exists but no corresponding syscall has returned yet */
            ++unknown;
        case PROCSTAT_ALLOCATED:
            /* Not yet attached but it will show up eventually */
        case PROCSTAT_ATTACHED:
            /* Running */
            ++nproc;
            break;
        }
    }
    if(p_nproc != NULL)
        *p_nproc = nproc;
    if(p_unknown != NULL)
        *p_unknown = unknown;
}

int trace_add_files_from_proc(unsigned int process, pid_t tid,
                              const char *binary)
{
    FILE *fp;
    char dummy;
    char *line = NULL;
    size_t length = 0;
    char previous_path[4096] = "";

    const char *const fmt = "/proc/%d/maps";
    int len = snprintf(&dummy, 1, fmt, tid);
    char *procfile = malloc(len + 1);
    snprintf(procfile, len + 1, fmt, tid);

    /* Loops on lines
     * Format:
     * 08134000-0813a000 rw-p 000eb000 fe:00 868355     /bin/bash
     * 0813a000-0813f000 rw-p 00000000 00:00 0
     * b7721000-b7740000 r-xp 00000000 fe:00 901950     /lib/ld-2.18.so
     * bfe44000-bfe65000 rw-p 00000000 00:00 0          [stack]
     */

#ifdef DEBUG_PROC_PARSER
    log_info(tid, "parsing %s", procfile);
#endif
    fp = fopen(procfile, "r");
    free(procfile);

    while((line = read_line(line, &length, fp)) != NULL)
    {
        unsigned long int addr_start, addr_end;
        char perms[5];
        unsigned long int offset;
        unsigned int dev_major, dev_minor;
        unsigned long int inode;
        char pathname[4096];
        sscanf(line,
               "%lx-%lx %4s %lx %x:%x %lu %s",
               &addr_start, &addr_end,
               perms,
               &offset,
               &dev_major, &dev_minor,
               &inode,
               pathname);

#ifdef DEBUG_PROC_PARSER
        log_info(tid,
                 "proc line:\n"
                 "    addr_start: %lx\n"
                 "    addr_end: %lx\n"
                 "    perms: %s\n"
                 "    offset: %lx\n"
                 "    dev_major: %x\n"
                 "    dev_minor: %x\n"
                 "    inode: %lu\n"
                 "    pathname: %s",
                 addr_start, addr_end,
                 perms,
                 offset,
                 dev_major, dev_minor,
                 inode,
                 pathname);
#endif
        if(inode > 0)
        {
            if(strncmp(pathname, binary, 4096) != 0
             && strncmp(previous_path, pathname, 4096) != 0)
            {
#ifdef DEBUG_PROC_PARSER
                log_info(tid, "    adding to database");
#endif
                if(db_add_file_open(process, pathname,
                                    FILE_READ, path_is_dir(pathname)) != 0)
                    return -1;
                strncpy(previous_path, pathname, 4096);
            }
        }
    }
    fclose(fp);
    return 0;
}

static void trace_set_options(pid_t tid)
{
    ptrace(PTRACE_SETOPTIONS, tid, 0,
           PTRACE_O_TRACESYSGOOD |  /* Adds 0x80 bit to SIGTRAP signals
                                     * if paused because of syscall */
#ifdef PTRACE_O_EXITKILL
           PTRACE_O_EXITKILL |
#endif
           PTRACE_O_TRACECLONE |
           PTRACE_O_TRACEFORK |
           PTRACE_O_TRACEVFORK |
           PTRACE_O_TRACEEXEC);
}


vector *tid_worker_pipe_list;

static void add_tid_worker_pipe(pid_t tid, int worker_pipe[4]) {
    tid_worker_pipe cur_tid_worker_pipe;
    cur_tid_worker_pipe.tid = tid;
    for (int i = 0; i < 4; i++)
        cur_tid_worker_pipe.worker_pipe[i] = worker_pipe[i];
    vector_add(tid_worker_pipe_list, cur_tid_worker_pipe);
}

static int get_worker_pipe_write(pid_t tid) {
    int index = vector_find_tid(tid_worker_pipe_list, tid);
    //printf("After vector_find_tid. Index is [%d]\n", index);
    if (index == -1) {
        return -1;
    } else {
        return vector_get(tid_worker_pipe_list, index).worker_pipe[3];
    }
}

static int delete_tid_worker_pip(pid_t tid) {
    int index = vector_find_tid(tid_worker_pipe_list, tid);
    if (index == -1) {
        return -1;
    } else {
        vector_delete(tid_worker_pipe_list, index);
        return 1;
    }
}

static int trace(pid_t first_proc, int *first_exit_code)
{
    //printf("Beginning of trace\n");
    int num_workers = 0;

    vpollfd pollfds;
    vpollfd_init(&pollfds);

    for(;;)
    {
        //printf("I am tracer. New iteration begins.\n");
        int status;
        pid_t tid;
        int cpu_time;
        struct Process *process;

        /* Wait for a process */
#if NO_WAIT3
        tid = waitpid(-1, &status, __WALL | WNOHANG);
        cpu_time = -1;
#else
        {
            struct rusage res;
            tid = wait3(&status, __WALL | WNOHANG, &res);
            cpu_time = (res.ru_utime.tv_sec * 1000 +
                        res.ru_utime.tv_usec / 1000);
        }
#endif
        if(tid == -1)
        {
            /* LCOV_EXCL_START : internal error: waitpid() won't fail unless we
             * mistakingly call it while there is no child to wait for */
            log_critical(0, "waitpid failed: %s", strerror(errno));
            return -1;
            /* LCOV_EXCL_END */
        }

        if (tid == 0)
            goto read;

        //printf("I am tracer. I found my child. His PID is [%d]\n", tid);

        /*int index;
        if ((index = vector_find(&workers, tid) != -1) {
            vector_delete(&workers, index);
            continue;
        }*/

        if(WIFEXITED(status) || WIFSIGNALED(status))
        {
            //printf("I am tracer. I am finalizing my child [%d]\n", tid);
            int index = vector_find_tid(tid_worker_pipe_list, tid);
            if (index == -1) {
            //    printf("Invalid TID2\n");
                exit(EXIT_FAILURE);
            }
            vpollfd_delete(&pollfds, index);
            num_workers -= 1;


            if (delete_tid_worker_pip(tid) == -1) {
            //    printf("TID [%d] is invalid.\n", tid);
                exit(EXIT_FAILURE);
            }

            unsigned int nprocs, unknown;
            int exitcode;
            if(WIFSIGNALED(status))
                /* exit codes are 8 bits */
                exitcode = 0x0100 | WTERMSIG(status);
            else
                exitcode = WEXITSTATUS(status);

            if(tid == first_proc && first_exit_code != NULL)
                *first_exit_code = exitcode;
            process = trace_find_process(tid);
            if(process != NULL)
            {
                int cpu_time_val = -1;
                if(process->tid == process->threadgroup->tgid)
                    cpu_time_val = cpu_time;
                if(db_add_exit(process->identifier, exitcode,
                               cpu_time_val) != 0)
                    return -1;
                trace_free_process(process);
            }
            trace_count_processes(&nprocs, &unknown);
            if(verbosity >= 2)
                log_info(tid, "process exited (%s %d), CPU time %.2f, "
                         "%d processes remain",
                         (exitcode & 0x0100)?"signal":"code", exitcode & 0xFF,
                         cpu_time * 0.001f, (unsigned int)nprocs);
            if(nprocs <= 0)
                break;
            if(unknown >= nprocs)
            {
                /* LCOV_EXCL_START : This can't happen because UNKNOWN
                 * processes are the forked processes whose creator has not
                 * returned yet. Therefore, if there is an UNKNOWN process, its
                 * creator has to exist as well (and it is not UNKNOWN). */
                log_critical(0, "only UNKNOWN processes remaining (%d)",
                             (unsigned int)nprocs);
                return -1;
                /* LCOV_EXCL_END */
            }
            continue;
        }

        process = trace_find_process(tid);
        if(process == NULL)
        {
            if(verbosity >= 3)
                log_debug(tid, "process appeared");
            process = trace_get_empty_process();
            process->status = PROCSTAT_UNKNOWN;
            process->flags = 0;
            process->tid = tid;
            process->threadgroup = NULL;
            process->in_syscall = 0;
            trace_set_options(tid);
            /* Don't resume, it will be set to ATTACHED and resumed when fork()
             * returns */
            continue;
        }
        else if(process->status == PROCSTAT_ALLOCATED)
        {
            process->status = PROCSTAT_ATTACHED;

            if(verbosity >= 3)
                log_debug(tid, "process attached");
            trace_set_options(tid);
            ptrace(PTRACE_SYSCALL, tid, NULL, NULL);
            if(verbosity >= 2)
            {
                unsigned int nproc, unknown;
                trace_count_processes(&nproc, &unknown);
                log_info(0, "%d processes (inc. %d unattached)",
                         nproc, unknown);
            }
            continue;
        }

        if(WIFSTOPPED(status) && WSTOPSIG(status) & 0x80)
        {
            size_t len = 0;
#ifdef I386
            struct i386_regs regs;
#else /* def X86_64 */
            struct x86_64_regs regs;
#endif
            /* Try to use GETREGSET first, since iov_len allows us to know if
             * 32bit or 64bit mode was used */
#ifdef PTRACE_GETREGSET
#ifndef NT_PRSTATUS
#define NT_PRSTATUS  1
#endif
            {
                struct iovec iov;
                iov.iov_base = &regs;
                iov.iov_len = sizeof(regs);
                if(ptrace(PTRACE_GETREGSET, tid, NT_PRSTATUS, &iov) == 0)
                    len = iov.iov_len;
            }
            if(len == 0)
#endif
            /* GETREGSET undefined or call failed, fallback on GETREGS */
            {
                /* LCOV_EXCL_START : GETREGSET was added by Linux 2.6.34 in
                 * May 2010 (2225a122) */
                ptrace(PTRACE_GETREGS, tid, NULL, &regs);
                /* LCOV_EXCL_END */
            }
#if defined(I386)
            if(!process->in_syscall)
                process->current_syscall = regs.orig_eax;
            if(process->in_syscall)
                get_i386_reg(&process->retvalue, regs.eax);
            else
            {
                get_i386_reg(&process->params[0], regs.ebx);
                get_i386_reg(&process->params[1], regs.ecx);
                get_i386_reg(&process->params[2], regs.edx);
                get_i386_reg(&process->params[3], regs.esi);
                get_i386_reg(&process->params[4], regs.edi);
                get_i386_reg(&process->params[5], regs.ebp);
            }
            process->mode = MODE_I386;
#elif defined(X86_64)
            /* On x86_64, process might be 32 or 64 bits */
            /* If len is known (not 0) and not that of x86_64 registers,
             * or if len is not known (0) and CS is 0x23 (not as reliable) */
            if( (len != 0 && len != sizeof(regs))
             || (len == 0 && regs.cs == 0x23) )
            {
                /* 32 bit mode */
                struct i386_regs *x86regs = (struct i386_regs*)&regs;
                if(!process->in_syscall)
                    process->current_syscall = x86regs->orig_eax;
                if(process->in_syscall)
                    get_i386_reg(&process->retvalue, x86regs->eax);
                else
                {
                    get_i386_reg(&process->params[0], x86regs->ebx);
                    get_i386_reg(&process->params[1], x86regs->ecx);
                    get_i386_reg(&process->params[2], x86regs->edx);
                    get_i386_reg(&process->params[3], x86regs->esi);
                    get_i386_reg(&process->params[4], x86regs->edi);
                    get_i386_reg(&process->params[5], x86regs->ebp);
                }
                process->mode = MODE_I386;
            }
            else
            {
                /* 64 bit mode */
                if(!process->in_syscall)
                    process->current_syscall = regs.orig_rax;
                if(process->in_syscall)
                    get_x86_64_reg(&process->retvalue, regs.rax);
                else
                {
                    get_x86_64_reg(&process->params[0], regs.rdi);
                    get_x86_64_reg(&process->params[1], regs.rsi);
                    get_x86_64_reg(&process->params[2], regs.rdx);
                    get_x86_64_reg(&process->params[3], regs.r10);
                    get_x86_64_reg(&process->params[4], regs.r8);
                    get_x86_64_reg(&process->params[5], regs.r9);
                }
                /* Might still be either native x64 or Linux's x32 layer */
                process->mode = MODE_X86_64;
            }
#endif
            //printf("I am tracer. I am before worker pipe write.\n");
            int worker_pipe_write = get_worker_pipe_write(tid);
            //printf("Wroker pipe write = [%d]\n", worker_pipe_write);
            if (worker_pipe_write == -1) {
                //printf("I am tracer. I am creating a worker for my child [%d]\n", tid);
                pthread_t worker;
                int worker_pipe[4];
                if (pipe(&worker_pipe[0]) == -1) {
                    perror("pipe1");
                    exit(EXIT_FAILURE);
                }
                if (pipe(&worker_pipe[2]) == -1) {
                    perror("pipe2");
                    exit(EXIT_FAILURE);
                }
                worker_arg *argument = (worker_arg *)malloc(sizeof(worker_arg));
                for (int i = 0; i < 4; i++) {
                    argument->fd[i] = worker_pipe[i];
                    //printf("worker_pipe[%d] = %d\n", i, worker_pipe[i]);
                }
                worker_pipe_write = worker_pipe[3];

                add_tid_worker_pipe(tid, worker_pipe);

                num_workers += 1;

                struct pollfd cur_pollfd;
                cur_pollfd.fd = worker_pipe[0];
                cur_pollfd.events = POLLIN | POLLPRI;
                vpollfd_add(&pollfds, cur_pollfd);

                pthread_create(&worker, NULL, syscall_handle, argument);

                //printf("I am tracer. I finished creating a worker for my child [%d]\n", tid);
                //pthread_detach(worker);
            }

            //printf("I am parent. I am writing to fd [%d]\n", worker_pipe_write);
            //ssize_t len1 = write(worker_pipe_write, process, sizeof(struct Process));
            ssize_t len1 = write(worker_pipe_write, &process, sizeof(struct Process *));
            if (len1 == -1) {
            //    printf("Parent write failed.\n");
                exit(EXIT_FAILURE);
            } else {
            //    printf("I am parent. I have written [%d] bytes. The size of Process is [%d] bytes.\n", (int)len1, (int)sizeof(struct Process));
            }
            //if(syscall_handle(process) != 0)
            //    return -1;
        }
        /* Handle signals */
        else if(WIFSTOPPED(status))
        {
            int signum = WSTOPSIG(status) & 0x7F;

            /* Synthetic signal for ptrace event: resume */
            if(signum == SIGTRAP && status & 0xFF0000)
            {
                int event = status >> 16;
                if(event == PTRACE_EVENT_EXEC)
                {
                    log_debug(tid,
                             "got EVENT_EXEC, an execve() was successful and "
                             "will return soon");
                    //printf("Process has value [%p]\n", process);
                    if(syscall_execve_event(process) != 0)
                        return -1;
                }
                else if( (event == PTRACE_EVENT_FORK)
                      || (event == PTRACE_EVENT_VFORK)
                      || (event == PTRACE_EVENT_CLONE))
                {
                    if(syscall_fork_event(process, event) != 0)
                        return -1;
                }
                ptrace(PTRACE_SYSCALL, tid, NULL, NULL);
            }
            else if(signum == SIGTRAP)
            {
                /* LCOV_EXCL_START : Processes shouldn't be getting SIGTRAPs */
                log_error(0,
                          "NOT delivering SIGTRAP to %d\n"
                          "    waitstatus=0x%X", tid, status);
                ptrace(PTRACE_SYSCALL, tid, NULL, NULL);
                /* LCOV_EXCL_END */
            }
            /* Other signal, let the process handle it */
            else
            {
                siginfo_t si;
                if(verbosity >= 2)
                    log_info(tid, "caught signal %d", signum);
                if(ptrace(PTRACE_GETSIGINFO, tid, 0, (long)&si) >= 0)
                    ptrace(PTRACE_SYSCALL, tid, NULL, signum);
                else
                {
                    /* LCOV_EXCL_START : Not sure what this is for... doesn't
                     * seem to happen in practice */
                    log_error(tid, "    NOT delivering: %s", strerror(errno));
                    if(signum != SIGSTOP)
                        ptrace(PTRACE_SYSCALL, tid, NULL, NULL);
                    /* LCOV_EXCL_END */
                }
            }
        }


read: ;
        //printf("I am tracer. I am polling.\n");
        struct pollfd *pollfds_items = vpollfd_items(&pollfds);
        int num_ready = poll(pollfds_items, num_workers, 50);
        //printf("I am tracer. I finished polling. Polling resul: [%d]\n", num_ready);
        if (num_ready == 0)
            continue;
        if (num_ready == -1) {
            perror("Poll");
            exit(1);
        }

        for (int i = 0; i < num_workers; i++) {
            if (pollfds_items[i].revents & POLLIN || pollfds_items[i].revents & POLLPRI) {
                int num_bytes_ready;
                if (ioctl(pollfds_items[i].fd, FIONREAD, &num_bytes_ready) < 0) {
                    perror("ioctl");
                    exit(1);
                }
                //printf("[%d] bytes are available.\n", num_bytes_ready);

                int request;
                pid_t tid;
                void *addr;
                void *data;

                ssize_t len1 = read(pollfds_items[i].fd, &request, sizeof(request));
                //printf("Request: [%d]\n", request);
                ssize_t len2 = read(pollfds_items[i].fd, &tid, sizeof(tid));
                //printf("TID: [%d]\n", tid);
                ssize_t len3 = read(pollfds_items[i].fd, &addr, sizeof(addr));
                //printf("ADDR: [%p]\n", addr);
                ssize_t len4 = read(pollfds_items[i].fd, &data, sizeof(data));
                //printf("DATA: [%p]\n", data);
                //printf("Read length: [%d], [%d], [%d], [%d]\n", (int)len1, (int)len2, (int)len3, (int)len4);


                //if (request == PTRACE_SYSCALL) {
                    //printf("Before letting child go.\n");
                //}

                errno = 0;
                long res = ptrace(request, tid, addr, data);

                //if (request == PTRACE_SYSCALL) {
                    //printf("After letting child go.\n");
                //}

                //printf("Ptrace result [%ld]\n", res);
                if (res == -1) {
                    perror("ptrace");
                }


                int writefd = get_worker_pipe_write(tid);
                ssize_t len5 = write(writefd, &res, sizeof(res));
                (void)len5;
            }
        }
        

    }

    return 0;
}

static void (*python_sigchld_handler)(int) = NULL;
static void (*python_sigint_handler)(int) = NULL;

static void restore_signals(void)
{
    if(python_sigchld_handler != NULL)
    {
        signal(SIGCHLD, python_sigchld_handler);
        python_sigchld_handler = NULL;
    }
    if(python_sigint_handler != NULL)
    {
        signal(SIGINT, python_sigint_handler);
        python_sigint_handler = NULL;
    }
}

static void cleanup(void)
{
    size_t i;
    {
        size_t nb = 0;
        for(i = 0; i < processes_size; ++i)
            if(processes[i]->status != PROCSTAT_FREE)
                ++nb;
        /* size_t size is implementation dependent; %u for size_t can trigger
         * a warning */
        log_error(0, "cleaning up, %u processes to kill...", (unsigned int)nb);
    }
    for(i = 0; i < processes_size; ++i)
    {
        if(processes[i]->status != PROCSTAT_FREE)
        {
            kill(processes[i]->tid, SIGKILL);
            trace_free_process(processes[i]);
        }
    }
}

static time_t last_int = 0;

static void sigint_handler(int signo)
{
    time_t now = time(NULL);
    (void)signo;
    if(now - last_int < 2)
    {
        if(verbosity >= 1)
            log_error(0, "cleaning up on SIGINT");
        cleanup();
        restore_signals();
        exit(1);
    }
    else if(verbosity >= 1)
        log_error(0, "Got SIGINT, press twice to abort...");
    last_int = now;
}

static void trace_init(void)
{
    /* Store Python's handlers for restore_signals() */
    python_sigchld_handler = signal(SIGCHLD, SIG_DFL);
    python_sigint_handler = signal(SIGINT, sigint_handler);

    if(processes == NULL)
    {
        size_t i;
        struct Process *pool;
        processes_size = 16;
        processes = malloc(processes_size * sizeof(*processes));
        pool = malloc(processes_size * sizeof(*pool));
        for(i = 0; i < processes_size; ++i)
        {
            processes[i] = pool++;
            processes[i]->status = PROCSTAT_FREE;
            processes[i]->threadgroup = NULL;
            processes[i]->execve_info = NULL;
        }
    }

    syscall_build_table();
    tid_worker_pipe_list = (vector *)malloc(sizeof(vector));
    vector_init(tid_worker_pipe_list);
}

int fork_and_trace(const char *binary, int argc, char **argv,
                   const char *database_path, int *exit_status)
{
    pid_t child;

    //printf("Before trace init\n");
    trace_init();
    //printf("After trace init\n");

    child = fork();

    if(child != 0 && verbosity >= 2)
        log_info(0, "child created, pid=%d", child);

    if(child == 0)
    {
        //printf("Hey! I am tracee. My pid is [%d]\n", getpid());
        char **args = malloc((argc + 1) * sizeof(char*));
        memcpy(args, argv, argc * sizeof(char*));
        args[argc] = NULL;
        /* Trace this process */
        if(ptrace(PTRACE_TRACEME, 0, NULL, NULL) != 0)
        {
            log_critical(
                0,
                "couldn't use ptrace: %s\n"
                "This could be caused by a security policy or isolation "
                "mechanism (such as\n Docker), see http://bit.ly/2bZd8Fa",
                strerror(errno));
            exit(1);
        }
        /* Stop this once so tracer can set options */
        //printf("Hey! I am tracee. I am right before kill.\n");
        kill(getpid(), SIGSTOP);
        /* Execute the target */
        //printf("Hey! I am tracee. I am right before exec.\n");
        execvp(binary, args);
        log_critical(0, "couldn't execute the target command (execvp "
                     "returned): %s", strerror(errno));
        exit(1);
    }

    /* Open log file */
    {
        char logfilename[1024];
        strcpy(logfilename, getenv("HOME"));
        strcat(logfilename, "/.reprozip/log");
        if(log_open_file(logfilename) != 0)
        {
            restore_signals();
            return 1;
        }
    }

    if(db_init(database_path) != 0)
    {
        kill(child, SIGKILL);
        log_close_file();
        restore_signals();
        return 1;
    }

    /* Creates entry for first process */
    {
        struct Process *process = trace_get_empty_process();
        process->status = PROCSTAT_ALLOCATED; /* Not yet attached... */
        process->flags = 0;
        /* We sent a SIGSTOP, but we resume on attach */
        process->tid = child;
        process->threadgroup = trace_new_threadgroup(child, get_wd());
        process->in_syscall = 0;

        if(verbosity >= 2)
            log_info(0, "process %d created by initial fork()", child);
        if( (db_add_first_process(&process->identifier,
                                  process->threadgroup->wd) != 0)
         || (db_add_file_open(process->identifier, process->threadgroup->wd,
                              FILE_WDIR, 1) != 0) )
        {
            /* LCOV_EXCL_START : Database insertion shouldn't fail */
            db_close(1);
            cleanup();
            log_close_file();
            restore_signals();
            return 1;
            /* LCOV_EXCL_END */
        }
    }

    if(trace(child, exit_status) != 0)
    {
        cleanup();
        db_close(1);
        log_close_file();
        restore_signals();
        return 1;
    }

    if(db_close(0) != 0)
    {
        log_close_file();
        restore_signals();
        return 1;
    }

    log_close_file();
    restore_signals();
    return 0;
}
