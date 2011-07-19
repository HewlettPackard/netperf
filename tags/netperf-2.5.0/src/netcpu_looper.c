char   netcpu_looper_id[]="\
@(#)netcpu_looper.c (c) Copyright 2005-2007. Version 2.4.3";

/* netcpu_looper.c
  
   Implement the soaker process specific portions of netperf CPU
   utilization measurements. These are broken-out into a separate file
   to make life much nicer over in netlib.c which had become a maze of
   twisty, CPU-util-related, #ifdefs, all different.  raj 2005-01-26
   */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#if HAVE_UNISTD_H
# include <unistd.h>
#endif
#if defined(HAVE_MMAP) || defined(HAVE_SYS_MMAN_H)
# include <sys/mman.h>
#else
# error netcpu_looper requires mmap
#endif

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

#if HAVE_SYS_WAIT_H
# include <sys/wait.h>
#endif

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include "netsh.h"
#include "netlib.h"

#define PAGES_PER_CHILD 2

/* the lib_start_count and lib_end_count arrays hold the starting
   and ending values of whatever is counting when the system is
   idle. The rate at which this increments during a test is compared
   with a previous calibrarion to arrive at a CPU utilization
   percentage. raj 2005-01-26 */
static uint64_t  lib_start_count[MAXCPUS];
static uint64_t  lib_end_count[MAXCPUS];

static int *cpu_mappings;

static int lib_idle_fd;
static uint64_t *lib_idle_address[MAXCPUS];
static long     *lib_base_pointer;
static pid_t     lib_idle_pids[MAXCPUS];
static int       lib_loopers_running=0;

/* we used to use this code to bind the loopers, but since we have
   decided to enable processor affinity for the actual
   netperf/netserver processes we will use that affinity routine,
   which happens to know about more systems than this */

#ifdef NOTDEF
static void
bind_to_processor(int child_num)
{
  /* This routine will bind the calling process to a particular */
  /* processor. We are not choosy as to which processor, so it will be */
  /* the process id mod the number of processors - shifted by one for */
  /* those systems which name processor starting from one instead of */
  /* zero. on those systems where I do not yet know how to bind a */
  /* process to a processor, this routine will be a no-op raj 10/95 */

  /* just as a reminder, this is *only* for the looper processes, not */
  /* the actual measurement processes. those will, should, MUST float */
  /* or not float from CPU to CPU as controlled by the operating */
  /* system defaults. raj 12/95 */

#ifdef __hpux
#include <sys/syscall.h>
#include <sys/mp.h>

  int old_cpu = -2;

  if (debug) {
    fprintf(where,
            "child %d asking for CPU %d as pid %d with %d CPUs\n",
            child_num,
            (child_num % lib_num_loc_cpus),
            getpid(),
            lib_num_loc_cpus);
    fflush(where);
  }

  SETPROCESS((child_num % lib_num_loc_cpus), getpid());
  return;

#else
#if defined(__sun) && defined(__SVR4)
 /* should only be Solaris */
#include <sys/processor.h>
#include <sys/procset.h>

  int old_binding;

  if (debug) {
    fprintf(where,
            "bind_to_processor: child %d asking for CPU %d as pid %d with %d CPUs\n",
            child_num,
            (child_num % lib_num_loc_cpus),
            getpid(),
            lib_num_loc_cpus);
    fflush(where);
  }

  if (processor_bind(P_PID,
                     getpid(),
                     (child_num % lib_num_loc_cpus), 
                      &old_binding) != 0) {
    fprintf(where,"bind_to_processor: unable to perform processor binding\n");
    fprintf(where,"                   errno %d\n",errno);
    fflush(where);
  }
  return;
#else
#ifdef WIN32

  if (!SetThreadAffinityMask(GetCurrentThread(), (ULONG_PTR)1 << (child_num % lib_num_loc_cpus))) {
    perror("SetThreadAffinityMask failed");
    fflush(stderr);
  }

  if (debug) {
    fprintf(where,
            "bind_to_processor: child %d asking for CPU %d of %d CPUs\n",
            child_num,
            (child_num % lib_num_loc_cpus),
            lib_num_loc_cpus);
    fflush(where);
  }

#endif
  return;
#endif /* __sun && _SVR4 */
#endif /* __hpux */
}
#endif

 /* sit_and_spin will just spin about incrementing a value */
 /* this value will either be in a memory mapped region on Unix shared */
 /* by each looper process, or something appropriate on Windows/NT */
 /* (malloc'd or such). This routine is reasonably ugly in that it has */
 /* priority manipulating code for lots of different operating */
 /* systems. This routine never returns. raj 1/96 */ 

static void
sit_and_spin(int child_index)

{
  uint64_t *my_counter_ptr;

 /* only use C stuff if we are not WIN32 unless and until we */
 /* switch from CreateThread to _beginthread. raj 1/96 */
#ifndef WIN32
  /* we are the child. we could decide to exec some separate */
  /* program, but that doesn't really seem worthwhile - raj 4/95 */
  if (debug > 1) {
    fprintf(where,
            "Looper child %d is born, pid %d\n",
            child_index,
            getpid());
    fflush(where);
  }
  
#endif /* WIN32 */

  /* reset our base pointer to be at the appropriate offset */
  my_counter_ptr = (uint64_t *) ((char *)lib_base_pointer + 
                             (netlib_get_page_size() * 
                              PAGES_PER_CHILD * child_index));
  
  /* in the event we are running on an MP system, it would */
  /* probably be good to bind the soaker processes to specific */
  /* processors. I *think* this is the most reasonable thing to */
  /* do, and would be closes to simulating the information we get */
  /* on HP-UX with pstat. I could put all the system-specific code */
  /* here, but will "abstract it into another routine to keep this */
  /* area more readable. I'll probably do the same thine with the */
  /* "low pri code" raj 10/95 */
  
  /* since we are "flying blind" wrt where we should bind the looper
     processes, we want to use the cpu_map that was prepared by netlib
     rather than assume that the CPU ids on the system start at zero
     and are contiguous. raj 2006-04-03 */
  bind_to_specific_processor(child_index % lib_num_loc_cpus,1);
  
  for (*my_counter_ptr = 0L;
       ;
       (*my_counter_ptr)++) {
    if (!(*lib_base_pointer % 1)) {
      /* every once and again, make sure that our process priority is */
      /* nice and low. also, by making system calls, it may be easier */
      /* for us to be pre-empted by something that needs to do useful */
      /* work - like the thread of execution actually sending and */
      /* receiving data across the network :) */
#ifdef _AIX
      int pid,prio;

      prio = PRIORITY;
      pid = getpid();
      /* if you are not root, this call will return EPERM - why one */
      /* cannot change one's own priority to  lower value is beyond */
      /* me. raj 2/26/96 */  
      setpri(pid, prio);
#else /* _AIX */
#ifdef __sgi
      int pid,prio;

      prio = PRIORITY;
      pid = getpid();
      schedctl(NDPRI, pid, prio);
      sginap(0);
#else /* __sgi */
#ifdef WIN32
      SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_IDLE);
#else /* WIN32 */
#if defined(__sun) && defined(__SVR4)
#include <sys/types.h>
#include <sys/priocntl.h>
#include <sys/rtpriocntl.h>
#include <sys/tspriocntl.h>
      /* I would *really* like to know how to use priocntl to make the */
      /* priority low for this looper process. however, either my mind */
      /* is addled, or the manpage in section two for priocntl is not */
      /* terribly helpful - for one, it has no examples :( so, if you */
      /* can help, I'd love to hear from you. in the meantime, we will */
      /* rely on nice(39). raj 2/26/96 */
      nice(39);
#else /* __sun && __SVR4 */
      nice(39);
#endif /* __sun && _SVR4 */
#endif /* WIN32 */
#endif /* __sgi */
#endif /* _AIX */
    }
  }
}



 /* this routine will start all the looper processes or threads for */
 /* measuring CPU utilization. */

static void
start_looper_processes()
{

  unsigned int      i, file_size;
  
  /* we want at least two pages for each processor. the */
  /* child for any one processor will write to the first of his two */
  /* pages, and the second page will be a buffer in case there is page */
  /* prefetching. if your system pre-fetches more than a single page, */
  /* well, you'll have to modify this or live with it :( raj 4/95 */

  file_size = ((netlib_get_page_size() * PAGES_PER_CHILD) * 
               lib_num_loc_cpus);
  
#ifndef WIN32

  /* we we are not using WINDOWS NT (or 95 actually :), then we want */
  /* to create a memory mapped region so we can see all the counting */
  /* rates of the loopers */

  /* could we just use an anonymous memory region for this? it is */
  /* possible that using a mmap()'ed "real" file, while convenient for */
  /* debugging, could result in some filesystem activity - like */
  /* metadata updates? raj 4/96 */
  lib_idle_fd = open("/tmp/netperf_cpu",O_RDWR | O_CREAT | O_EXCL);
  
  if (lib_idle_fd == -1) {
    fprintf(where,"create_looper: file creation; errno %d\n",errno);
    fflush(where);
    exit(1);
  }
  
  if (chmod("/tmp/netperf_cpu",0644) == -1) {
    fprintf(where,"create_looper: chmod; errno %d\n",errno);
    fflush(where);
    exit(1);
  }
  
  /* with the file descriptor in place, lets be sure that the file is */
  /* large enough. */
  
  if (truncate("/tmp/netperf_cpu",file_size) == -1) {
    fprintf(where,"create_looper: truncate: errno %d\n",errno);
    fflush(where);
    exit(1);
  }
  
  /* the file should be large enough now, so we can mmap it */
  
  /* if the system does not have MAP_VARIABLE, just define it to */
  /* be zero. it is only used/needed on HP-UX (?) raj 4/95 */
#ifndef MAP_VARIABLE
#define MAP_VARIABLE 0x0000
#endif /* MAP_VARIABLE */
#ifndef MAP_FILE
#define MAP_FILE 0x0000
#endif /* MAP_FILE */
  if ((lib_base_pointer = (long *)mmap(NULL,
                                       file_size,
                                       PROT_READ | PROT_WRITE,
                                       MAP_FILE | MAP_SHARED | MAP_VARIABLE,
                                       lib_idle_fd,
                                       0)) == (long *)-1) {
    fprintf(where,"create_looper: mmap: errno %d\n",errno);
    fflush(where);
    exit(1);
  }
  

  if (debug > 1) {
    fprintf(where,"num CPUs %d, file_size %d, lib_base_pointer %p\n",
            lib_num_loc_cpus,
            file_size,
            lib_base_pointer);
    fflush(where);
  }

  /* we should have a valid base pointer. lets fork */
  
  for (i = 0; i < (unsigned int)lib_num_loc_cpus; i++) {
    switch (lib_idle_pids[i] = fork()) {
    case -1:
      perror("netperf: fork");
      exit(1);
    case 0:
      /* we are the child. we could decide to exec some separate */
      /* program, but that doesn't really seem worthwhile - raj 4/95 */

      signal(SIGTERM, SIG_DFL);
      sit_and_spin(i);

      /* we should never really get here, but if we do, just exit(0) */
      exit(0);
      break;
    default:
      /* we must be the parent */
      lib_idle_address[i] = (uint64_t *) ((char *)lib_base_pointer + 
                                      (netlib_get_page_size() * 
                                       PAGES_PER_CHILD * i));
      if (debug) {
        fprintf(where,"lib_idle_address[%d] is %p\n",
                i,
                lib_idle_address[i]);
        fflush(where);
      }
    }
  }
#else
  /* we are compiled -DWIN32 */
  if ((lib_base_pointer = malloc(file_size)) == NULL) {
    fprintf(where,
            "create_looper_process could not malloc %d bytes\n",
            file_size);
    fflush(where);
    exit(1);
  }

  /* now, create all the threads */
  for(i = 0; i < (unsigned int)lib_num_loc_cpus; i++) {
    long place_holder;
    if ((lib_idle_pids[i] = CreateThread(0,
                                         0,
                                         (LPTHREAD_START_ROUTINE)sit_and_spin,
                                         (LPVOID)(ULONG_PTR)i,
                                         0,
                                         &place_holder)) == NULL ) {
      fprintf(where,
              "create_looper_process: CreateThread failed\n");
      fflush(where);
      /* I wonder if I need to look for other threads to kill? */
      exit(1);
    }
    lib_idle_address[i] = (long *) ((char *)lib_base_pointer + 
                                    (netlib_get_page_size() * 
                                     PAGES_PER_CHILD * i));
    if (debug) {
      fprintf(where,"lib_idle_address[%d] is %p\n",
              i,
              lib_idle_address[i]);
      fflush(where);
    }
  }
#endif /* WIN32 */

  /* we need to have the looper processes settled-in before we do */
  /* anything with them, so lets sleep for say 30 seconds. raj 4/95 */

  sleep(30);
}

void
cpu_util_init(void) 
{
  cpu_method = LOOPER;

  /* we want to get the looper processes going */
  if (!lib_loopers_running) {
    start_looper_processes();
    lib_loopers_running = 1;
  }

  return;
}

/* clean-up any left-over CPU util resources - looper processes,
   files, whatever.  raj 2005-01-26 */
void
cpu_util_terminate() {

#ifdef WIN32
  /* it would seem that if/when the process exits, all the threads */
  /* will go away too, so I don't think I need any explicit thread */
  /* killing calls here. raj 1/96 */
#else

  int i;

  /* now go through and kill-off all the child processes */
  for (i = 0; i < lib_num_loc_cpus; i++){
    /* SIGKILL can leave core files behind - thanks to Steinar Haug */
    /* for pointing that out. */
    kill(lib_idle_pids[i],SIGTERM);
  }
  lib_loopers_running = 0;
  /* reap the children */
  while(waitpid(-1, NULL, WNOHANG) > 0) { }
  
  /* finally, unlink the mmaped file */
  munmap((caddr_t)lib_base_pointer,
         ((netlib_get_page_size() * PAGES_PER_CHILD) * 
          lib_num_loc_cpus));
  unlink("/tmp/netperf_cpu");
#endif
  return;
}

int
get_cpu_method(void)
{
  return LOOPER;
}

 /* calibrate_looper */

 /* Loop a number of iterations, sleeping interval seconds each and */
 /* count how high the idle counter gets each time. Return  the */
 /* measured cpu rate to the calling routine. raj 4/95 */

float
calibrate_idle_rate (int iterations, int interval)
{

  uint64_t
    firstcnt[MAXCPUS],
    secondcnt[MAXCPUS];

  float 
    elapsed,
    temp_rate,
    rate[MAXTIMES],
    local_maxrate;

  long  
    sec,
    usec;

  int   
    i,
    j;
  
  struct  timeval time1, time2 ;
  struct  timezone tz;
  
  if (iterations > MAXTIMES) {
    iterations = MAXTIMES;
  }

  local_maxrate = (float)-1.0;
  
  for(i = 0; i < iterations; i++) {
    rate[i] = (float)0.0;
    for (j = 0; j < lib_num_loc_cpus; j++) {
      firstcnt[j] = *(lib_idle_address[j]);
    }
    gettimeofday (&time1, &tz);
    sleep(interval);
    gettimeofday (&time2, &tz);

    if (time2.tv_usec < time1.tv_usec)
      {
        time2.tv_usec += 1000000;
        time2.tv_sec -=1;
      }
    sec = time2.tv_sec - time1.tv_sec;
    usec = time2.tv_usec - time1.tv_usec;
    elapsed = (float)sec + ((float)usec/(float)1000000.0);
    
    if(debug) {
      fprintf(where, "Calibration for counter run: %d\n",i);
      fprintf(where,"\tsec = %ld usec = %ld\n",sec,usec);
      fprintf(where,"\telapsed time = %g\n",elapsed);
    }

    for (j = 0; j < lib_num_loc_cpus; j++) {
      secondcnt[j] = *(lib_idle_address[j]);
      if(debug) {
        /* I know that there are situations where compilers know about */
        /* long long, but the library fucntions do not... raj 4/95 */
        fprintf(where,
                "\tfirstcnt[%d] = 0x%8.8lx%8.8lx secondcnt[%d] = 0x%8.8lx%8.8lx\n",
                j,
                (uint32_t)(firstcnt[j]>>32),
                (uint32_t)(firstcnt[j]&0xffffffff),
                j,
                (uint32_t)(secondcnt[j]>>32),
                (uint32_t)(secondcnt[j]&0xffffffff));
      }
      /* we assume that it would wrap no more than once. we also */
      /* assume that the result of subtracting will "fit" raj 4/95 */
      temp_rate = (secondcnt[j] >= firstcnt[j]) ?
        (float)(secondcnt[j] - firstcnt[j])/elapsed : 
          (float)(secondcnt[j]-firstcnt[j]+MAXLONG)/elapsed;
      if (temp_rate > rate[i]) rate[i] = temp_rate;
      if(debug) {
        fprintf(where,"\trate[%d] = %g\n",i,rate[i]);
        fflush(where);
      }
      if (local_maxrate < rate[i]) local_maxrate = rate[i];
    }
  }
  if(debug) {
    fprintf(where,"\tlocal maxrate = %g per sec. \n",local_maxrate);
    fflush(where);
  }
  return local_maxrate;
}


void
get_cpu_idle (uint64_t *res)
{
  int i;

  for (i = 0; i < lib_num_loc_cpus; i++){
    res[i] = *lib_idle_address[i];
  }

}

float
calc_cpu_util_internal(float elapsed_time)
{
  int i;
  float correction_factor;
  float actual_rate;

  lib_local_cpu_util = (float)0.0;
  /* It is possible that the library measured a time other than */
  /* the one that the user want for the cpu utilization */
  /* calculations - for example, tests that were ended by */
  /* watchdog timers such as the udp stream test. We let these */
  /* tests tell up what the elapsed time should be. */
  
  if (elapsed_time != 0.0) {
    correction_factor = (float) 1.0 + 
      ((lib_elapsed - elapsed_time) / elapsed_time);
  }
  else {
    correction_factor = (float) 1.0;
  }

  for (i = 0; i < lib_num_loc_cpus; i++) {

    /* it would appear that on some systems, in loopback, nice is
     *very* effective, causing the looper process to stop dead in its
     tracks. if this happens, we need to ensure that the calculation
     does not go south. raj 6/95 and if we run completely out of idle,
     the same thing could in theory happen to the USE_KSTAT path. raj
     8/2000 */ 
    
    if (lib_end_count[i] == lib_start_count[i]) {
      lib_end_count[i]++;
    }
    
    actual_rate = (lib_end_count[i] > lib_start_count[i]) ?
      (float)(lib_end_count[i] - lib_start_count[i])/lib_elapsed :
      (float)(lib_end_count[i] - lib_start_count[i] +
	      MAXLONG)/ lib_elapsed;
    if (debug) {
      fprintf(where,
              "calc_cpu_util: actual_rate on processor %d is %f start 0x%8.8lx%8.8lx end 0x%8.8lx%8.8lx\n",
              i,
              actual_rate,
              (uint32_t)(lib_start_count[i]>>32),
              (uint32_t)(lib_start_count[i]&0xffffffff),
              (uint32_t)(lib_end_count[i]>>32),
              (uint32_t)(lib_end_count[i]&0xffffffff));
    }
    lib_local_per_cpu_util[i] = (lib_local_maxrate - actual_rate) /
      lib_local_maxrate * 100;
    lib_local_per_cpu_util[i] *= correction_factor;
    lib_local_cpu_util += lib_local_per_cpu_util[i];
  }
  /* we want the average across all n processors */
  lib_local_cpu_util /= (float)lib_num_loc_cpus;
  
  return lib_local_cpu_util;
}

void
cpu_start_internal(void)
{
  get_cpu_idle(lib_start_count);
  return;
}

void
cpu_stop_internal(void)
{
  get_cpu_idle(lib_end_count);
}
