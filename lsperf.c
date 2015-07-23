#define LSPERFVERSION "0.3.0"
#ifdef DEBUGLSPERF
void DebugOutput(int area, int level, char *format,...);
#define DEBUG_MAIN 0
#define DEBUG_CMD  1
#define DEBUG_OPEN 2
#define DEBUG_MEM 3
#define DEBUG_TIME 4
#define DEBUG_IO 5
#define DEBUG_DEV 6
#define DEBUG_FS 7
#define DEBUG_SCHED 8
#define DEBUG_WRITE 9
#define DEBUG_READ 10
#define DEBUG_LATENCY 11
#define DEBUG_MAX 12
int DebugVector[DEBUG_MAX]={0};
int DebugArea,DebugLevel;
#define DEBUGP(x) x
#else
#define DEBUGP(x)
#endif

#define _GNU_SOURCE

/* to avoid errors with too much/less zeros */
#define kE 1000
#define mE 1000000
#define gE 1000000000
#define AIO_MAXIO 256

#include <libaio.h>
#include <stdarg.h>
#include <ctype.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <sys/utsname.h>
#include <signal.h>
#include <malloc.h>
#include <unistd.h>

#define STRLEN 4096        /* max length for strings */

typedef struct
{
  long double avrb;
  long double avsb;
} latency;

struct thread_data
{
  int threadid;
  long long time;
  int loop;
  int cpu;
  long long MinTime, MaxTime;
  latency IOPS;
};

typedef struct
{
   int FD;
   long long bsize;
   long long dsize;
} fileinfo;

/* global variables */

static int NrThreads=1;
static long long BlockSize=4096;
static int Direct=0;
static int SynFlag=0;
static int Write=0;
static int Trunc=0;
static int Read=0;
static int Fsync=0;
static int Unique=0;
static int CCache=0;
static char FileName[STRLEN]="";
static char hostname[STRLEN]="";
static char comment[STRLEN]="";
static char *membuf;
static long long FileSize=0;
static int Random=0;
static int Timing=0;
static int Verbose=0;
static char format[5]="     ";
static long long pagesize;
pthread_mutex_t mutex;

pthread_barrier_t psync1,psync2,psync3;
static int aio_engine = 0;
static int iodepth = 1;
static int Percent = 0;
static int RandomFlag = 0;

void CmdLine(int argc, char* argv[]);
void Usage(char *argv);
fileinfo OpenFile(char *name,int threadid);
int GetMemory(long long bs);
int WriteFile(int FD,struct thread_data *thread_data_array);
int ReadFile(int FD,struct thread_data *thread_data_array);
int AIOWriteFile(int FD,struct thread_data *thread_data_array);
int AIOReadFile(int FD,struct thread_data *thread_data_array);
long long GetTime();
long double Format(long double io);
void *IOtest(void *thread_data_array);
void *AIOtest(void *thread_data_array);
int get_device(int major, int minor, char *device);
int get_fs(char *cwd, char *fs);
int get_scheduler(char *device, char* scheduler, long long *dsize);
int ClearCache();
long long unsigned get_total_cpu();
int get_task_cpu(long long unsigned *system, long long unsigned *user);
int get_nr_cpu();
void SubmitRead(int offset);
void *Reap(void *p);
static void io_error(const char *, int);

int
main(int argc, char* argv[])
{
  long double io, Tmin=0, Tmax=0, duration,total,m,max=0;
  long long start, timedelay=0, MinAccess=0, MaxAccess=0;
  long long MeanAccess=0, MinTime=0, MaxTime=0;
  int i,loop=0, rc;
  latency IOPS={.avrb=0.0, .avsb=0.0};
  void *status;
  double sload,eload;
  pthread_attr_t attr;
  time_t ltime;
  struct utsname udata;
  long long unsigned start_task_system,start_cpu;
  long long unsigned start_task_user;
  long long unsigned end_task_system,end_cpu;
  long long unsigned end_task_user;
  long long unsigned TotalLoops=0;
  double cpu, cpu_s, cpu_u;
  int NrCPUs=0;

  /* Parse command line arguments */
  strcpy(format,"B/s");

  CmdLine(argc,argv);

  if ( (strlen(FileName) == 0) || (FileName[0]=='-') )
  {
    printf("\n Choose a file name first (-f)!\n\n");
    exit(-1);
  }

  struct thread_data thread_data_array[NrThreads];
  pthread_t thread[NrThreads];
  if ( pthread_barrier_init(&psync1,NULL,NrThreads) ||
       pthread_barrier_init(&psync2,NULL,NrThreads) ||
       pthread_barrier_init(&psync3,NULL,NrThreads) )
  {
     perror("\n Problem initializing barriers!");
     exit(-1);
  }
  if (NrThreads > 1)
      pthread_mutex_init(&mutex,NULL);

  if (Write==Read)
  {
    printf("\n Choose either read (-r) or write (-w)!\n\n");
    exit(-1);
  }

  if (FileSize==0)
  {
    printf("\n Hmm, no file size given and we do not want to calculate latency?\n\n");
    exit(-1);
  }

  if (Verbose)
  {
    printf("\n     ___ --- === Simple Disk Benchmark === --- ___\n");
    ltime=time(NULL);
    if (strlen(comment)) printf("\n Comment: %s\n",comment);
    printf("\n Actual date and time          : %s",ctime(&ltime));
    if (Write)
		printf("\n Running test                  : write");
    else
      printf("\n Running test                  : read");
    if (! gethostname(hostname,STRLEN))
      printf("\n Running on host               : %s\n",hostname);
    if ( uname(&udata) )
    {
       perror("Problem with uname data");
    }
    else
    {
      printf(" System name                   : %s\n",udata.sysname);
      printf(" Release                       : %s\n",udata.release);
      //printf(" Version                       : %s\n",udata.version);
      printf(" Machine                       : %s\n",udata.machine);
    }
    NrCPUs=get_nr_cpu();
    printf(" Number of available CPUs      : %i\n",NrCPUs);
    printf(" Main process is running on CPU: %i\n", sched_getcpu());
    start=GetTime();
    timedelay=GetTime()-start;
    printf(" Delay due to time measurement : %.3Lf microseconds\n",(long double)(timedelay/1000.0));
  }

  GetMemory(BlockSize);

  if (Verbose)
  {
    if (Direct) printf(" Flag O_DIRECT set\n");
    if (SynFlag) printf(" Flag O_SYNC set\n");
	if (aio_engine) printf(" Using AIO engine\n");
    if (Fsync) printf(" Using fsync\n");
	if (Random) printf(" Testing random IO\n");
    if (Unique && NrThreads > 1)
       printf("\n Using unique filename         : %s\n",FileName);
  }

  start_cpu=get_total_cpu();
  get_task_cpu(&start_task_system,&start_task_user);
  start=GetTime();
  getloadavg(&sload,1);

  /* Start all threads */
  for ( i=0; i < NrThreads; i++)
  {
    thread_data_array[i].threadid=i;
    thread_data_array[i].time=0;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	if (aio_engine)
		rc = pthread_create(&thread[i], &attr, AIOtest, (void *)&thread_data_array[i]);
	else
		rc = pthread_create(&thread[i], &attr, IOtest, (void *)&thread_data_array[i]);

    if (rc)
    {
      printf("ERROR; return code from pthread_create() is %d\n", rc);
      exit(-1);
    }
  }

  /* wait for output of opened files */
  if (Verbose)
  {
    if ( (Verbose > 1) && (NrThreads>1) ) sleep(1);
    printf(" \n Starting benchmark...\n\n");
  }

  total=0;
  /* threads created, wait for result  */
  for ( i=0; i < NrThreads; i++)
  {
    rc = pthread_join(thread[i], &status);
    if (rc)
    {
      printf("ERROR; return code from pthread_join() is %d\n", rc);
      exit(-1);
    }
  }
  getloadavg(&eload,1);
  end_cpu=get_total_cpu();
  get_task_cpu(&end_task_system,&end_task_user);
  cpu_s=100*((double)(end_task_system-start_task_system))/(end_cpu-start_cpu);
  cpu_u=100*((double)(end_task_user-start_task_user))/(end_cpu-start_cpu);
  cpu=100*((double)(end_task_system+end_task_user-start_task_system-start_task_user))/(end_cpu-start_cpu);

  /* clean up */
  pthread_barrier_destroy(&psync1);
  pthread_barrier_destroy(&psync2);
  pthread_barrier_destroy(&psync3);

  if (Verbose)
  {
     printf("\n Result:");
     printf("\n -------\n");
  }
  if (FileSize)
  {
     printf(" %Li Bytes copied per thread, block size is %Li Bytes\n\n",
             FileSize,BlockSize);
  }
  for ( i=0; i < NrThreads; i++)
  {
    duration=(long double) (thread_data_array[i].time-timedelay)/gE;
    io=(long double)FileSize/duration;
    total += io;
    if ( io > max ) max=io;
    io=Format(io);
    loop=thread_data_array[i].loop;
    TotalLoops += loop;
    IOPS.avrb+=thread_data_array[i].IOPS.avrb;
    IOPS.avsb+=thread_data_array[i].IOPS.avsb;
    if (FileSize) printf(" Thread%3i (CPU: %2i): %.6Lf seconds -> %.2Lf %s",
            i,thread_data_array[i].cpu,duration,io,format);
    if (loop > 1)
    {
      if (FileSize) printf(" in %i loops",loop);
      if ((MinTime==0) || (thread_data_array[i].MinTime < MinTime))
         MinTime=thread_data_array[i].MinTime;
      if (thread_data_array[i].MaxTime > MaxTime)
         MaxTime=thread_data_array[i].MaxTime;
    }
    if (FileSize) printf("\n");
    if ( duration > Tmax) Tmax=duration;
    if ( ( duration < Tmin) || Tmin == 0) Tmin=duration;
  }
  duration=(long double)(GetTime()-start)/gE;
  if(Verbose)
  {
    printf(" Load at beginning : %6.2f\n",sload);
    printf(" Load at end       : %6.2f\n",eload);
    printf(" CPU usage         : %6.2f%%\n",cpu*NrCPUs);
    printf(" CPU usage system  : %6.2f%%\n",cpu_s*NrCPUs);
    printf(" CPU usage user    : %6.2f%%\n",cpu_u*NrCPUs);
    if (NrThreads > 1)
    {
      printf(" Time variance     : %10.6Lf seconds\n",Tmax-Tmin);
      printf(" Time maximum      : %10.6Lf seconds\n",Tmax);
      printf(" Time minimum      : %10.6Lf seconds\n",Tmin);
    }
    if (loop>1 && FileSize)
    {
      if (NrThreads>1)
      {
        printf(" Min access time   : %10.6Lf ms\n",
               (long double)MinAccess/mE);
        printf(" Max access time   : %10.6Lf ms\n",
               (long double)MaxAccess/mE);
        printf(" Medium access time: %10.6Lf ms\n",
               (long double)MeanAccess/mE/NrThreads);
        printf(" Min time for loop : %10.6Lf ms\n",
               (long double)MinTime/mE);
        printf(" Max time for loop : %10.6Lf ms\n",
               (long double)MaxTime/mE);
      }
      else
      {
        printf(" Access time       : %10.6Lf ms\n",
               (long double)MinAccess/mE);
      }
      if (MinAccess<0 && Write)
      {
        printf(" WARNING AccessTime: controller/hdd cache effective?\n");
      }
    }
    if (NrThreads >1 && FileSize)
    {
      m=Format(total/NrThreads);
      printf(" Medium per thread : %6.2Lf %s\n",m,format);
      m=Format(max);
      printf(" Maximum per thread: %6.2Lf %s\n",m,format);
      m=Format(max*NrThreads);
      printf(" Ideal throughput  : %6.2Lf %s\n",m,format);
      m=Format(FileSize*NrThreads/duration);
      printf(" Medium throughput : %6.2Lf %s\n",m,format);
    }
    printf(" Total time        : %10.6Lf seconds\n",duration);
    if(TotalLoops >1)
      printf(" IOPS (theoretical): %6.2Lf (%10.6Lf ns)\n",
               (long double)TotalLoops/duration, mE*duration/TotalLoops);
  }
  else printf("\n");
  if (FileSize)
  {
    m=Format(total);
    printf("\n Total throughput  : %6.2Lf %s\n\n",m,format);
  }
  printf("\n");

  /* avoid  double free error with glibc(?) */
  if (BlockSize >= pagesize) free(membuf);
  if (NrThreads > 1)
     pthread_mutex_destroy(&mutex);
  exit(0);
}

void
Usage(char *argv)
{
  printf("\n Usage: %s [-CDhorRStTUvVw] [-d area,level] [-b BlockSize] [-B SeekBSize] [-c comment] [-f FileName] [-j NrThreads] [-s FileSize]\n",argv);
  printf("\n -b BlockSize   : Size of memory block used for writing");
  printf("\n -c comment     : Add a comment to include in verbose output");
  printf("\n -C             : Clear OS cache (needs root)");
//  printf("\n -d area,level  : Activate debug mode");
  printf("\n -D             : Use direct access (O_DIRECT)");
  printf("\n -f FileName    : Name of file to use");
  printf("\n -F             : Use fsync call");
  printf("\n -h             : print this help page");
  printf("\n -j NrThreads   : Use NrThreads parallel threads");
  printf("\n                  -1 disables this, 0 uses same count as random access");
  printf("\n -r             : Open file for reading");
  printf("\n -R             : Randomize buffer before it is written to file");
  printf("\n -s FileSize    : Bytes to read/write from/to the file");
  printf("\n -S             : Open file with O_SYNC");
  printf("\n -t             : trunc exiting files before writing with O_TRUNC");
//  printf("\n -T             : Calculate Timing for memcpy, bcopy and memmove");
//  printf("\n -U             : use same file for all threads");
  printf("\n -v             : Verbose mode");
  printf("\n -V             : Print version and exit");
  printf("\n -w             : Open file for writing");
  printf("\n -i             : Set iodepth for AIO engine");
  printf("\n -A             : Use AIO engine");
  printf("\n -p Percent     : write/read");
  printf("\n -E             : Random write/read");
  printf("\n\n Be aware: You have to use either '-r' or '-w'!\n\n");
  exit(0);
}

/* Read settings from command line */
void
CmdLine(int argc, char* argv[])
{
  int opt, size;
  char c;
#ifdef DEBUGLSPERF
  char *d;
  int i;
#endif
  char options[]="b:B:c:Cd:Df:Fhj:l:L:orRs:StTUvVwi:Ap:E";

  while((opt = getopt(argc, argv, options)) != -1)
  {
    switch(opt)
    {
      case 'b':
        c=optarg[strlen(optarg)-1];
        BlockSize=atoi(optarg);
        if ((c=='k') || (c=='K')) BlockSize<<=10;
        if ((c=='m') || (c=='M')) BlockSize<<=20;
        if ((c=='g') || (c=='G')) BlockSize<<=30;
        if ((c=='t') || (c=='T')) BlockSize<<=40;
        DEBUGP(DebugOutput(DEBUG_CMD,1,"BlockSize: %Li\n",BlockSize););
        break;

      case 'c':
        strncpy(comment,optarg,STRLEN-1);
        if (Verbose==0) Verbose=1;
        break;

      case 'C':
        CCache++;
        break;

      case 'j':
        NrThreads=atoi(optarg);
        if (NrThreads < 1 ) NrThreads=1;
        break;

      case 'v':
        Verbose++;
        DEBUGP(DebugOutput(DEBUG_CMD,1,"Verbose mode is active\n"););
        break;

      case 'V':
        printf("\n Simple Disk Benmark Version %s\n",LSPERFVERSION);
        printf("\n Author: Dirk Geschke <dirk@geschke-online.de>\n\n");
        exit(0);
        break;

      case 'R':
        RandomFlag=1;
        DEBUGP(DebugOutput(DEBUG_CMD,1,"Enabled Random buffer filling\n");)
        break;

      case 'S':
        SynFlag=1;
        DEBUGP(DebugOutput(DEBUG_CMD,1,"O_SYNC enabled\n"););
        break;

      case 't':
        Trunc=1;
        DEBUGP(DebugOutput(DEBUG_CMD,1,"O_TRUNC enabled\n"););
        break;

      case 'T':
        Timing=1;
        DEBUGP(DebugOutput(DEBUG_CMD,1,"Timing enabled\n"););
        break;

      case 'U':
        Unique=1;
        DEBUGP(DebugOutput(DEBUG_CMD,1,"Unique filename enabled for all threads\n"););
        break;

      case 's':
        c=optarg[strlen(optarg)-1];
        FileSize=atoi(optarg);
        if ((c=='k') || (c=='K')) FileSize<<=10;
        if ((c=='m') || (c=='M')) FileSize<<=20;
        if ((c=='g') || (c=='G')) FileSize<<=30;
        if ((c=='t') || (c=='T')) FileSize<<=30;
        DEBUGP(DebugOutput(DEBUG_CMD,1, "FileSize: %Li\n",FileSize););
        break;

      case 'd':
#ifdef DEBUGLSPERF
        d=strchr(optarg,',');
          if (d == NULL)
            {
              DebugArea=atoi(optarg);
              DebugLevel=9;
            }
          else
            {
              *d='\0';d++;
              DebugArea=atoi(optarg);
              DebugLevel=atoi(d);
            }

          /* DebugLevel has maximum of 9 */
          if (DebugLevel > 9 ) DebugLevel=9;

          /* Set debug value for area */
          if ( (DebugArea < DEBUG_MAX) && (DebugArea > 0) )
            {
              DebugVector[DebugArea]=DebugLevel;
              DEBUGP(DebugOutput(DEBUG_CMD,1,
                                 "DebugArea: %u, DebugLevel: %u\n",
                                 DebugArea,DebugLevel););
            }

          /* if area is ascii "ALL" then set all values ( if not
           *  already set to an value) to DebugLevel
           */
          if ( DebugArea == 0 && (!strncasecmp(optarg,"ALL",3)) )
            {
              DEBUGP(DebugOutput(DEBUG_CMD,1,
                                 "DebugArea: ALL, DebugLevel: %u\n",
                                 DebugLevel););
              for (i=0;i<DEBUG_MAX;i++)
                {
                  if (DebugVector[i] == 0 ) DebugVector[i]=DebugLevel;
                }
            }

        printf(" Debug enabled\n");
#else
        printf("\n Debug disabled, recompile with -DDEBUGLSPERF\n");
#endif
        break;

      case 'r':
        Read=1;
        DEBUGP(DebugOutput(DEBUG_CMD,1,"Read enabled\n"););
        break;

      case 'D':
        Direct=1;
        DEBUGP(DebugOutput(DEBUG_CMD,1,"O_DIRECT enabled\n"););
        break;

      case 'F':
        Fsync=1;
        DEBUGP(DebugOutput(DEBUG_CMD,1,"fync() is used\n"););
        break;

      case 'f':
        size=strlen(optarg);
        /* take care for up to 999 threads */
        if (size > STRLEN-4)
        {
          printf("Size of filename %s too big, allowed is %i\n",
                  optarg,STRLEN-4);
          exit(-1);
        }
        strncpy(FileName,optarg, size);
        FileName[size]='\0';
        DEBUGP(DebugOutput(DEBUG_CMD,1,"FileName: %s\n",FileName););
        break;

      case 'h':
        Usage(argv[0]);
        break;

      case 'w':
        DEBUGP(DebugOutput(DEBUG_CMD,1,"Write enabled\n"););
        Write=1;
        break;

	case 'A':
		aio_engine = 1;
		Direct = 1;
		break;
	case 'i':
		iodepth = atoi(optarg);
		break;

	case 'p':
		Percent = atoi(optarg);
		break;

	case 'E':
		RandomFlag = 1;
		break;

	default:
        Usage(argv[0]);
    }
  }
}

/* open file with the right flags
 * NOTE: We do not remove this file, neither it exists nor if
 *       we created one. This way, we can create symbolic links
 *       so that the files reside on different mount points.
 */
fileinfo
OpenFile(char * name,int threadid)
{
  int FD;
  int flags=O_RDONLY;
  struct stat sb;
  long long bsize=0;
  long long dsize=0;
  fileinfo ret;

  if (Write)
  {
    flags = O_CREAT | O_RDWR;
    if (Trunc) flags |= O_TRUNC;
  }
  if (SynFlag) flags |= O_SYNC;
  if (Direct)
  {
#ifdef O_DIRECT
    flags |= O_DIRECT;
#else
    printf("\n O_DIRECT not supported by OS!\n");
    Direct=0;
#endif
  }

  /* largefile should be used nowadays everytime */
#ifdef O_LARGEFILE
  flags |= O_LARGEFILE;
#endif

  if ( (FD=open(name, flags, S_IRUSR|S_IWUSR)) == -1)
  {
    perror("Error opening file");
    printf("Filename: %s\n",name);
    exit(-1);
  };

  if (Verbose)
  {
    char device[STRLEN]="";
    char scheduler[STRLEN]="";
    char fs[STRLEN]="";

    if (fstat(FD,&sb) == -1)
    {
      perror("Openfile: stat");
    }
    else
    {
      char cwd[STRLEN]="";
      DEBUGP(DebugOutput(DEBUG_OPEN,3,"Blocksize : %i\n",(int)sb.st_blksize););
      DEBUGP(DebugOutput(DEBUG_OPEN,3,"Size      : %Li\n",(long)sb.st_size););
      DEBUGP(DebugOutput(DEBUG_OPEN,3,"Blockcount: %i\n",(int)sb.st_blocks););
      DEBUGP(DebugOutput(DEBUG_OPEN,3,"Device    : %i\n",(int)sb.st_rdev););
      DEBUGP(DebugOutput(DEBUG_OPEN,1,"Major: %i (%i), Minor: %i (%i)\n",
                         major(sb.st_dev),major(sb.st_rdev),
                         minor(sb.st_dev),minor(sb.st_rdev)););

      /* get the realpath, maybe it was a link to another file */
      realpath(name,cwd);

      DEBUGP(DebugOutput(DEBUG_OPEN,1,"Working directory: %s\n",cwd););

      /* get the device we test on */
      if (get_device(major(sb.st_dev),minor(sb.st_dev),device))
      {
        DEBUGP(DebugOutput(DEBUG_OPEN,7," Raw device not found!\n"););
        strcpy(device,cwd);
      }

      if ( (bsize=get_scheduler(device,scheduler,&dsize)) < -1 )
      {
        DEBUGP(DebugOutput(DEBUG_OPEN,7," Scheduler not identified!\n"););
        strcpy(scheduler,"unknown");
      }
      DEBUGP(DebugOutput(DEBUG_OPEN,3,"dBlocksize: %Li\n",bsize););
      DEBUGP(DebugOutput(DEBUG_OPEN,3,"dDatasize : %Li\n",dsize););

      /* try to get the file system */
      if (get_fs(cwd,fs))
      {
        DEBUGP(DebugOutput(DEBUG_OPEN,7," File system not identified!\n"););
        strcpy(fs,"unknown");
      };
    }

    if ((Verbose == 1) || ((Unique > 0) && (threadid==0)))
    {

      if (Write)
        printf(" File opened for writing (%2i)  : %s (%s, %s, %s)\n",
                 threadid,name,device,fs,scheduler);
      else
        printf(" File opened for reading (%2i)  : %s (%s, %s, %s)\n",
                 threadid,name,device,fs,scheduler);
    }
  }

  DEBUGP(DebugOutput(DEBUG_OPEN,9,"File opened: %i\n",FD););

  ret.FD=FD;
  ret.bsize=bsize;
  ret.dsize=dsize;

  return ret;
}

/* Get the memory buffer and fill it up before read/write */
int

GetMemory(long long bs)
{
  long long start;
  long double io, duration;
  char * membuf2=NULL;

  if (bs < 0 )
  {
    printf("Invalid buffer size: %Li",bs);
    exit(-1);
  }

  /* get the pagesize for aligned memory allocation */
  pagesize=getpagesize();

  /* only needed for O_DIRECT? */
  if ((bs%pagesize) && Direct)
  {
    printf("\n WARNING: buffersize is not modulo pagesize %Li\n\n",pagesize);
    //exit(-1);
  }

  if(posix_memalign((void **)&membuf,pagesize,bs))
  {
    printf("Malloc failed!\n");
    exit(-1);
  }

  if (Timing)
  {
    if (posix_memalign((void **)&membuf2,pagesize,bs))
    {
      printf("Malloc for membuf2 failed!\n");
      exit(-1);
    }
  }

  DEBUGP(DebugOutput(DEBUG_MEM,5,"membuf after alignment : %p\n",membuf););
  DEBUGP(DebugOutput(DEBUG_MEM,5,"pagesize: %Li\n", pagesize););
  DEBUGP(DebugOutput(DEBUG_MEM,5,"Allocated memory: %Li at %p\n", bs,membuf););
  if (Verbose) printf(" Buffer size                   : %Li Bytes\n",bs);
  if (Verbose) printf(" Iodepth                       : %d\n",iodepth);

  start=GetTime();

  if ( memset(membuf,0,bs) != membuf)
  {
    perror("Problem with memset - membuf!");
    exit(-1);
  }

  if (Timing || (Verbose>1))
  {
    duration=(long double) (GetTime()-start)/gE;
    io=Format(bs/duration);
    printf("\n memset  took %Lf seconds -> %.2Lf %s",
           duration,io,format);
  }

  if (Timing)
  {
    start=GetTime();
    if ( memset(membuf2,0,bs) != membuf2)
    {
      perror("Problem with memset - membuf2 (1)!");
      exit(-1);
    }
    duration=(long double) (GetTime()-start)/gE;
    io=Format(bs/duration);
    printf("\n memset1 took %Lf seconds -> %.2Lf %s\n",
           duration,io,format);
    start=GetTime();
    if ( memset(membuf2,0,bs) != membuf2)
    {
      perror("Problem with memset - membuf2 (2)!");
      exit(-1);
    }
    duration=(long double) (GetTime()-start)/gE;
    io=Format(bs/duration);
    printf(" memset2 took %Lf seconds -> %.2Lf %s\n",
           duration,io,format);
    start=GetTime();
    bcopy(membuf, membuf2, bs);
    duration=(long double)(GetTime()-start)/gE;
    io=Format(bs/duration);
    printf(" bcopy   took %Lf seconds -> %.2Lf %s \n",
           duration,io,format);

    start=GetTime();
    memcpy(membuf2, membuf, bs);
    duration=(long double)(GetTime()-start)/gE;
    io=Format(bs/duration);
    printf(" memcpy  took %Lf seconds -> %.2Lf %s \n",
             duration,io,format);

    start=GetTime();
    memmove(membuf, membuf2, bs);
    duration=(long double)(GetTime()-start)/gE;
    io=Format(bs/duration);
    printf(" memmove took %Lf seconds -> %.2Lf %s \n",
             duration,io,format);

    free(membuf2);
  }
  else
    if (Verbose) printf("\n");

  if (Random && Write)
  {
    long long count,bytes;
    int RD;

    start=GetTime();

    if ( (RD=open("/dev/urandom", O_RDONLY|O_NONBLOCK)) == -1)
    {
      perror("Problem to open /dev/random");
      exit(-1);
    }

    count=0;

    printf("\n Filling buffer with randomized values...\n");

    while (count<bs)
    {
      bytes=read(RD, membuf, bs);
      count +=bytes;
    }
    duration=(long double)(GetTime()-start)/gE;
    io=Format(bs/duration);
    printf(" Randomization took %Lf seconds -> %.2Lf %s \n",
         duration,io,format);
    close(RD);
  }

  if (Verbose>1) printf("\n Initialization of memory finished.\n");

  return(0);
}

/* get actual time in nsec granularity */
long long
GetTime()
{
  struct timespec ts;
  long long nsec;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  DEBUGP(DebugOutput(DEBUG_TIME,3,"Seconds: %i, %Li\n",(int)ts.tv_sec,
         (long long)ts.tv_nsec););
  nsec=(long long)ts.tv_sec*gE+(long long)(ts.tv_nsec);
  DEBUGP(DebugOutput(DEBUG_TIME,1,"nsec: %.10Li\n",nsec););
  return(nsec);
}

/* function to write the membuf buffer to the file */
int
WriteFile(int FD,struct thread_data *data)
{
  long long count=FileSize,start,stop,totaltime=0;
  long long mintime=0,maxtime=0;
  size_t block=BlockSize;
  long long wb;
  int loop=0;
  long long offset = 0;
  off64_t  numrecs64 = (off64_t)(FileSize/BlockSize);

#ifdef DEBUGLSPERF
  long double duration;
#endif
  if (count==0)
  {
    printf("File size not set, use -s <size>!\n");
    exit(-1);
  }

  if (RandomFlag) srand48(0);

  while (count)
  {
    if (count < BlockSize)
    {
      block=count;
    }

	if (RandomFlag) {
		offset = block * (lrand48()%numrecs64);

		if (lseek(FD, offset, 0) < 0) {
			perror("lseek");
			exit(68);
		}
	}

    start=GetTime();
    wb=write(FD,membuf,block);
    stop = GetTime()-start;
    loop++;
	if ( (mintime==0) || (stop<mintime) )  mintime=stop;
	if ( stop> maxtime )  maxtime=stop;
    totaltime+=stop;

#ifdef DEBUGLSPERF
    duration=(long double)(stop)/gE;
    DEBUGP(DebugOutput(DEBUG_WRITE,4,"Time loop %i read: %Lf seconds\n",loop,duration););
#endif

    if (wb <0)
    {
      perror("Error write");
      printf("Failed to write to (%i) from %p number of bytes: %Li\n",
	     FD, membuf, (long long) block);
      exit(-1);
    }
    if (wb != block)
    {
      printf("Short write!\n");
      printf("Failed to write full block (%Li): %Li\n", (long long)block, wb);
    }
    count-=wb;
  }
  if (loop>1 && Verbose ) // && NrThreads==1)
  {
    data->MinTime=mintime;
    data->MaxTime=maxtime;
    DEBUGP(DebugOutput(DEBUG_WRITE,4,"MinTime: %Li, MaxTime: %Li\n",mintime, maxtime););
    //printf("\n Access time     : %.2Lf ms",duration);
    //duration=(long double)(midtime)/mE;
    //printf("\n Mean time       : %.2Lf ms\n",duration);
    //printf("\n IOPS            : %i \n",(int)(kE/duration));
  }
  return(loop);
}

/* function to read from the file to membuf, we do not care
* for the content at all, we rewrite membuf every time
*/
int
ReadFile(int FD,struct thread_data *data)
{
  long long count=0,start,stop,totaltime=0 ;
  long long mintime=0,maxtime=0;

  size_t block=BlockSize;
  long long wb;
  int endless=0;
  int loop=0;
  long long offset = 0;
  off64_t  numrecs64 = (off64_t)(FileSize/BlockSize);

#ifdef DEBUGLSPERF
  long double duration;
#endif

  if (FileSize==0) endless=1;
  if (RandomFlag) srand48(0);

  while (endless || count < FileSize)
  {
    if ((FileSize >0) && (count+BlockSize > FileSize)  )
    {
		block=FileSize-count;
	}

	if (RandomFlag) {
		offset = block * (lrand48()%numrecs64);

		if (lseek(FD, offset, 0) < 0) {
			perror("lseek");
			exit(68);
		}
	}

	start=GetTime();
	wb = read(FD,membuf,block);
	stop=GetTime()-start;
	loop++;
	if ( (mintime==0) || (stop<mintime) )  mintime=stop;
	if ( stop> maxtime )  maxtime=stop;
	totaltime+=stop;

#ifdef DEBUGLSPERF
    duration=(long double)(stop)/gE;
    DEBUGP(DebugOutput(DEBUG_READ,4,"Time loop %i read: %Lf seconds\n",loop,duration););
#endif

    if (wb <0)
    {
      perror("Error read");
      printf("Failed to read to (%i) from %p number of bytes: %Li\n",
	     FD, membuf, (long long) block);
      exit(-1);
    }
    if (wb != block)
    {
      printf("Short read!\n");
      printf("Failed to read full block (%Li): %Li\n", (long long) block, wb);
    }
    if (wb==0) { FileSize=count; return(loop);}
    count += wb;
  }

  if (loop>1 && Verbose ) // && NrThreads==1)
  {
    data->MinTime=mintime;
    data->MaxTime=maxtime;
    DEBUGP(DebugOutput(DEBUG_READ,4,"MinTime: %Li, MaxTime: %Li\n",mintime, maxtime););
    //printf("\n Access time     : %.2Lf ms",duration);
    //duration=(long double)(midtime)/mE;
    //printf("\n Mean time       : %.2Lf ms\n",duration);
    //printf("\n IOPS            : %i \n",(int)(kE/duration));
  }

  return(loop);
}

long double
Format(long double io)
{
  if ( io >= 1024*1024*1024*1024UL )
  {
    strcpy(format,"TB/s");
    io = io/1024/1024/1024/1024UL;
  }
  else if ( io >= 1024*1024*1024 )
  {
    strcpy(format,"GB/s");
    io = io/1024/1024/1024;
  }
  else if ( io >= 1024*1024 )
  {
    strcpy(format,"MB/s");
    io = io/1024/1024;
  }
  else if ( io >= 1024 )
  {
    strcpy(format,"kB/s");
    io = io/1024;
  }
  else
  {
    strcpy(format,"B/s");
  }
  return(io);
}

/* Do the real i/o test */
void *
IOtest(void *thread_data_array)
{
  struct thread_data *data;
  char name[STRLEN];
  int cpu,FD,threadid;
  fileinfo fi;
  long long start;

  data=(struct thread_data *) thread_data_array;
  threadid=data->threadid;
  data->cpu=sched_getcpu();
  DEBUGP(DebugOutput(DEBUG_IO,1," Thread %i: Running on CPU: %i\n",data->threadid, data->cpu););

  /* if we have one thread, use the real given filename
   * otherwise add the thread number to the file
   */
  if ((NrThreads==1) || Unique)
    strcpy(name,FileName);
  else
    sprintf(name,"%s%i",FileName,threadid);

  DEBUGP(DebugOutput(DEBUG_IO,2," Using file name: %s\n",name););

  fi=OpenFile(name,threadid);
  FD=fi.FD;

  /* wait for synchronized start */
  if (pthread_barrier_wait(&psync1)==PTHREAD_BARRIER_SERIAL_THREAD)
  {
     if (Verbose>1) printf("\n Starting threads!\n");
  }

  if (CCache) ClearCache();
  start=GetTime();

  if (Write && FileSize) data->loop=WriteFile(FD,thread_data_array);
  if (Read && FileSize) data->loop=ReadFile(FD,thread_data_array);
  if (Fsync)
  {
    if(fsync(FD)==-1)
    {
       perror("fsync failed");
    }
  }
  data->time=GetTime()-start;
  if (CCache>1) ClearCache();

  /* get the cpu we are running on, maybe it changed */
  cpu=sched_getcpu();
  if (cpu != data->cpu)
  {
    if (Verbose>1) printf(" CPU of thread %i changed from %i to %i\n",
		         threadid, data->cpu, cpu);
    data->cpu=cpu;
  }

  close(FD);

  return(0);
}

/* Do the real AIO test */
void *
AIOtest(void *thread_data_array)
{
	struct thread_data *data;
	char name[STRLEN];
	int cpu,FD,threadid;
	fileinfo fi;

//	long long count = FileSize, start, totaltime = 0;
//	size_t block = BlockSize;
//	int loop = 0;
//	long long offset = 0;
//	long iter = 0;
//	int i = 0;
//	io_context_t myctx;

	data=(struct thread_data *) thread_data_array;
	threadid=data->threadid;
	data->cpu=sched_getcpu();
	DEBUGP(DebugOutput(DEBUG_IO,1," Thread %i: Running on CPU: %i\n",data->threadid, data->cpu););

	/* if we have one thread, use the real given filename
	 * otherwise add the thread number to the file
	 */
	if ((NrThreads==1) || Unique)
		strcpy(name,FileName);
	else
		sprintf(name,"%s%i",FileName,threadid);

	DEBUGP(DebugOutput(DEBUG_IO,2," Using file name: %s\n",name););

	fi=OpenFile(name,threadid);
	FD=fi.FD;

	/* wait for synchronized start */
	if (pthread_barrier_wait(&psync1)==PTHREAD_BARRIER_SERIAL_THREAD)
	{
		if (Verbose>1) printf("\n Starting threads!\n");
	}

	if (Write && FileSize) data->loop=AIOWriteFile(FD, thread_data_array);
	if (Read && FileSize) data->loop=AIOReadFile(FD, thread_data_array);

	cpu=sched_getcpu();
	if (cpu != data->cpu)
	{
		if (Verbose>1) printf(" CPU of thread %i changed from %i to %i\n",
							  threadid, data->cpu, cpu);
		data->cpu=cpu;
	}

	close(FD);
	return(0);
}

/* find the device the file is located on */
int
get_device(int major, int minor, char *device)
{
  FILE *fp;
  char *line = NULL;
  size_t len = 0;
  int i,j;
  struct stat sb;

  //fp = fopen("/proc/mounts", "r");
  fp = fopen("/etc/mtab", "r");
  if (fp ==NULL) return(-1);
  while (getline(&line,&len,fp) !=-1)
  {
    j=strlen(line);
    for (i=0;  i<j; i++)
    {
      if (line[i] == ' ')
      {
        line[i]='\0';
        break;
      }
    }
    DEBUGP(DebugOutput(DEBUG_DEV,7,"line: %s\n",line););
    if (stat(line,&sb) == -1)
    {
      continue;
    }
    if ((major==major(sb.st_rdev)) && (minor==minor(sb.st_rdev)))
    {
      strncpy(device,line,STRLEN);
      free(line);
      fclose(fp);
      return(0);
    }
  }
  fclose(fp);
  free(line);
  return(1);
}

/* find the file system where the file is written on */
int
get_fs(char *cwd, char *fs)
{
  FILE *fp;
  char * line;
  size_t len = 0;
  char pwd[STRLEN]="";
  int i,j,p=0,l1,l2=0,fsi,ind=0;
  size_t length;
  int ret=1;

  length=strlen(cwd);
  //fp = fopen("/proc/mounts", "r");
  fp = fopen("/etc/mtab", "r");
  if (fp ==NULL) return(-1);
  while (getline(&line,&len,fp) !=-1)
  {
    j=strlen(line);
    fsi=0;
    p=0;
    for (i=0;  i<j; i++)
    {
      if (line[i] == ' ')
      {
        line[i]='\0';
        if (p==0) ind=i+1;
        if (p==1)
        {
          fsi=i+1;
          strcpy(pwd,&line[ind]);
        }
        p++;
      }
    }
    DEBUGP(DebugOutput(DEBUG_FS,7,"line: %s\n",line););
    l1=0;
    for (i=0; i<length;i++)
    {
      if (cwd[i] == pwd [i]) l1++;
      else break;
    }
    DEBUGP(DebugOutput(DEBUG_FS,4,"l1: %i,l2: %i, len(pwd): %i %s\n",
               l1,l2,strlen(pwd),pwd););
    if (strlen(pwd) > l1) l1=0;
    if ((i>1) && (pwd[i-1]=='/') && (l1 >0)) l1--;
    DEBUGP(DebugOutput(DEBUG_FS,4,"l1: %i,l2: %i, pwd: %s, cwd: %s fs: %s\n",
               l1,l2,pwd,cwd,&line[fsi]););
    if ( (l2>1) && (l1 == l2) )
      {
        printf(" WARNIG, found more than one entry!\n");
        printf(" old entry: %s\n",fs);
        printf(" new entry: %s\n",&line[fsi]);
      }
    if ( l1 >= l2 )
    {
      strcpy(fs,&line[fsi]);
      l2=l1;
      ret=0;
    }
  }
  fclose(fp);

  DEBUGP(DebugOutput(DEBUG_FS,1,"FS: %s, l2: %i\n",fs,l2););

  free(line);
  return(ret);
}

#ifdef DEBUGLSPERF
/*
 * Function: DebugOutput(format,args,...)
 *
 * Purpose: prints debug output to stderr
 *
 */

void
DebugOutput(int area, int level, char *format,...)
{
  char line[STRLEN]="";
  va_list va_args;

  if (DebugVector[area] == 0) return;
  if (level > DebugVector[area]) return;
  bzero(line,STRLEN);
  snprintf(line,13,"DEBUG(%2i:%1i) ",area,level);
  va_start(va_args,format);
  vsnprintf(line+12,STRLEN-13,format,va_args);
  va_end(va_args);
  fprintf(stderr,line);
}

#endif

int get_scheduler(char *device, char* scheduler, long long *dsize)
{

  int i,s=0;
  FILE *fp;
  char * line=NULL;
  char * pdev=NULL;
  size_t len = 0;
  long long bsize=0;

  char *bdev;
  char filename[STRLEN]="";
  char sfilename[STRLEN]="";

  if (strncmp(device,"/dev/",5))
  {
     // not found
     return(-1);
  }
  bdev=strrchr(device,'/');
  bdev++;
  DEBUGP(DebugOutput(DEBUG_SCHED,9,"full dev: %s, dev: %s\n",device,bdev););
  //bdev=strdup(bdev);
  DEBUGP(DebugOutput(DEBUG_SCHED,9,"len bdev: %i\n",(int)strlen(bdev)););
  pdev=strdup(bdev);
  if ( strstr(device,"cciss") && ( !strstr(bdev,"p") ) )
  {
    /* do nothing, it is already a raw device */
    DEBUGP(DebugOutput(DEBUG_SCHED,9,"bdev still: %s\n",bdev););
  }
  else
  {
    for (i=strlen(bdev)-1; i>0; i--)
    {
      if (isdigit((int)bdev[i])) bdev[i]='\0';
      else break;
    }
  }
  DEBUGP(DebugOutput(DEBUG_SCHED,9,"dev: %s\n",bdev););
  if (((bdev[0] == 's') || (bdev[0] == 'h')) && (bdev[1] == 'd'))
   {
      snprintf(filename,STRLEN-1,"/sys/block/%s/queue/scheduler",bdev);
      snprintf(sfilename,STRLEN-1,"/sys/block/%s",bdev);
   }
  else if (bdev[0] == 'c' && strstr(device,"cciss"))
  {
      /* we have a cciss device */
      if (strstr(bdev,"p") ) bdev[strlen(bdev)-1]='\0';
      snprintf(filename,STRLEN-1,"/sys/block/cciss!%s/queue/scheduler",bdev);
      snprintf(sfilename,STRLEN-1,"/sys/block/cciss!%s",bdev);
  }

  DEBUGP(DebugOutput(DEBUG_SCHED,4,"filename: %s\n",filename););

  fp = fopen(filename, "r");
  if (fp==NULL)
  {
      free(pdev);
      return(-1);
  }
  if (getline(&line,&len,fp) <1)
  {
      free(pdev);
      fclose(fp);
      return(-1);
  }
  fclose(fp);
  DEBUGP(DebugOutput(DEBUG_SCHED,3,"scheduler line: %s\n",line););
  for (i=0; i<strlen(line);i++)
  {
    if (line[i]=='[') s=i+1;
    DEBUGP(DebugOutput(DEBUG_SCHED,9,"char: %c, s: %i, i: %i\n",line[i],s,i););
    if ( (line[i]==']') && (i>s) ) line[i]='\0';
  }
  if (s==0)
  {
     free(pdev);
     return(-2);
  }
  snprintf(scheduler,STRLEN-1,"%s",line+s);
  DEBUGP(DebugOutput(DEBUG_SCHED,1,"scheduler: %s\n",scheduler););

  /* get size of block device */
  snprintf(filename,STRLEN-1,"%s/size",sfilename);
  DEBUGP(DebugOutput(DEBUG_SCHED,8,"filename for size: %s\n",filename););

  fp = fopen(filename, "r");
  if (fp==NULL)
  {
     free(pdev);
      return(-2);
  }
  if (getline(&line,&len,fp) <1)
  {
     free(pdev);
      fclose(fp);
      return(-2);
  }
  *dsize=atoll(line);
  DEBUGP(DebugOutput(DEBUG_SCHED,4," Blocks on full Device: %Li\n",*dsize););
  fclose(fp);
  /* Do we have a partition? Should end in a number... */
  DEBUGP(DebugOutput(DEBUG_SCHED,9," pdev: %s (%c)\n",pdev,pdev[strlen(pdev-1)]););
  if (isdigit(pdev[strlen(bdev)]) || (strstr(device,"cciss") && strstr(pdev,"p")))
  {
    int found=1;

    if ( strstr(device,"cciss") && strstr(pdev,"p"))
       snprintf(filename,STRLEN-1,"/%s/cciss!%s/size",sfilename,pdev);
    else
       snprintf(filename,STRLEN-1,"/%s/%s/size",sfilename,pdev);
    DEBUGP(DebugOutput(DEBUG_SCHED,8,"filename for size: %s\n",filename););
    fp = fopen(filename, "r");
    if (fp==NULL)
    {
      found=0;
    }
    if (getline(&line,&len,fp) <1)
    {
      found=0;
      fclose(fp);
    }
    if (found)
    {
      *dsize=atoll(line);
      DEBUGP(DebugOutput(DEBUG_SCHED,4," Blocks on full Device: %Li\n",*dsize););
    }
  }
  snprintf(filename,STRLEN-1,"%s/queue/physical_block_size",sfilename);
  fp = fopen(filename, "r");
  if (fp==NULL)
  {
      perror("physical block size");
      free(pdev);
      return(-3);
  }
  if (getline(&line,&len,fp) <1)
  {
      perror("physical block size: getline");
      free(pdev);
      fclose(fp);
      return(-3);
  }
  bsize=atoll(line);
  if (bsize==0)
  {
     DEBUGP(DebugOutput(DEBUG_SCHED,1,"\n WARNING -> no phycial block size found, using 512 Bytes instead\n"););
     bsize=512;
  }
  fclose(fp);
  *dsize*=bsize;
  DEBUGP(DebugOutput(DEBUG_SCHED,4," Physical Blocksize : %Li\n",bsize););
  DEBUGP(DebugOutput(DEBUG_SCHED,4," Total size in Bytes: %Li\n",(*dsize)););

  free(line);
  free(pdev);
  return(bsize);
}

int ClearCache()
{
  FILE *fp;

  fp=fopen("/proc/sys/vm/drop_caches","w");
  if (fp==NULL)
  {
    perror("\n Can not clear the cache\n");
    return(-1);
  }
  fprintf(fp,"3");
  fclose(fp);
  return(0);
}

long long unsigned get_total_cpu()
{
  FILE *fp;
  char cpu[5];

  long long unsigned user, nice, system, idle, iowait, irq, softirq;
  long long unsigned steal, guest, guest_nice, sum;

  fp=fopen("/proc/stat","r");
  if (fp==NULL)
  {
    perror("\n Can not open /proc/stat\n");
    return(0);
  }
  fscanf(fp,"%4s %20llu %20llu %20llu %20llu %20llu %20llu %20llu %20llu %20llu %20llu",
         cpu,&user,&nice,&system,&idle,&iowait,&irq,
         &softirq,&steal,&guest,&guest_nice);
  sum=user+nice+system+idle+iowait+irq+softirq+steal+guest+guest_nice;

//  printf("sum cpu: %llu\n",sum);
  fclose(fp);
  return(sum);
}


int
get_task_cpu(long long unsigned *system,long long unsigned *user)
{
  FILE *fp;
  char name[1025];
  char c='\0';

  int pid,ppid,pgrp,sid,tty1,tty2;
  long long unsigned flags, min_flt, cmin_flt, maj_flt, cmaj_flt;
  long long unsigned utime, stime, cutime, cstime;

  fp=fopen("/proc/self/stat","r");
  if (fp==NULL)
  {
    perror("\n Can not open /proc/self/stat\n");
    return(-1);
  }
  fscanf(fp,"%10i %1024s %c %10i %10i %10i %10i %10i %20llu %20llu %20llu %20llu %20llu %20llu %20llu %20llu %20llu",
         &pid,name,&c,&ppid,&pgrp,&sid,&tty1,&tty2,&flags,&min_flt,&cmin_flt,
         &maj_flt,&cmaj_flt, &utime ,&stime ,&cutime,&cstime);
//  printf("Task: %s, Status: %c\n",name,(int)c);
//  printf("sum task: %llu %llu\n",utime+stime, cutime+cstime );
  *system=stime;
  *user=utime;
  fclose(fp);
  return(0);
}

int
get_nr_cpu()
{
  FILE *fp;
  char *line=NULL;
  int cpu=0;
  size_t len=0;

  fp=fopen("/proc/cpuinfo","r");
  if (fp==NULL)
  {
    return(1);
  }

  while (getline(&line,&len,fp) !=-1)
  {
     char *c;
     if ( (c=strstr(line,"processor")) )
     {
         sscanf(line,"processor       : %20i",&cpu);
     }
  }
  free(line);
  fclose(fp);
  return(cpu+1);
}

static void io_error(const char *func, int rc)
{
	if (rc == -ENOSYS)
		printf("AIO not in this kernel");
	else if (rc < 0)
		printf("%s: %s\n", func, strerror(-rc));
	else
		printf("%s: error %d\n", func, rc);

	exit(-1);
}

int AIOWriteFile(int FD, struct thread_data *data)
{
	long long totaltime = 0;
	int loop = 0;
	long long offset = 0;
	long iter = 0;
	int i = 0;
	io_context_t myctx;
	long long count = FileSize;
	size_t block = BlockSize;
	long long start;
	off64_t  numrecs64 = (off64_t)(FileSize/BlockSize);

#ifdef DEBUGLSPERF
	long double duration;
#endif

	memset(&myctx, 0, sizeof(myctx));
	if (0 != io_setup(AIO_MAXIO, &myctx)) {
		perror("Could not initialize io queue");
		exit(-1);
	}

	if (RandomFlag) srand48(0);

	while (count) {
		if (count < BlockSize) {
			block = count;
			iodepth = 1;
		}

		iter = count / block;

		if (iter < iodepth) {
			printf("Warning: iodepth < iter\n");
			iodepth = iter;
		}

		struct iocb *ioq[iodepth];
		struct io_event events[iodepth];
		struct io_event *ep;
		int n;

		if (RandomFlag) {
			offset = block * (lrand48()%numrecs64);

			if (lseek(FD, offset, 0) < 0) {
				perror("lseek");
				exit(68);
			}
		}

		start = GetTime();
		for (i = 0; i < iodepth; i++) {
			struct iocb *io = (struct iocb*)malloc(sizeof(struct iocb));
			io_prep_pwrite(io, FD, membuf, block, offset);

			ioq[i] = io;
			offset += block;
		}

		//if (Verbose) printf("AIO prepare done, now submitting...\n");

		if (iodepth != io_submit(myctx, iodepth, ioq)) {
			perror("Failure on submit.");
			exit(-1);
		}

		//if (Verbose) printf("Now awaiting completion..\n");

		n = io_getevents(myctx, iodepth, iodepth, events, NULL);

		for (ep = events; n-- > 0; ep++) {
			struct iocb *iocb = ep->obj;
			if (ep->res2 != 0) io_error("aio write", ep->res2);
			if (ep->res != iocb->u.c.nbytes) {
				printf("write missed bytes expect %lu got %ld\n", iocb->u.c.nbytes, ep->res);
			}

			count -= iocb->u.c.nbytes;
			loop++;
		}

		totaltime += GetTime() - start;
	}

	data->time = totaltime;
	io_destroy(myctx);

	return (loop);
}

/* libaio read function */
int AIOReadFile(int FD,struct thread_data *data)
{
	long long totaltime = 0, start;
	int loop = 0;
	long long offset = 0;
	long iter = 0;
	int i = 0;
	io_context_t myctx;
	long long count = 0;
	size_t block = BlockSize;
	int endless=0;
	off64_t  numrecs64 = (off64_t)(FileSize/BlockSize);

#ifdef DEBUGLSPERF
	long double duration;
#endif

	memset(&myctx, 0, sizeof(myctx));
	if (0 != io_setup(AIO_MAXIO, &myctx)) {
		perror("Could not initialize io queue");
		exit(-1);
	}

	if (FileSize == 0) endless = 1;
	if (RandomFlag) srand48(0);
	while (endless || count < FileSize) {
		if ((FileSize > 0) && (count + BlockSize > FileSize)) {
			block = FileSize - count;
		}

		if ((FileSize - count) < BlockSize) {
			block = FileSize - count;
			iodepth = 1;
		}

		iter = (FileSize - count) / block;
		if (iter < iodepth) {
			printf("Warning: iodepth < iter\n");
			iodepth = iter;
		}

		struct iocb *ioq[iodepth];
		struct io_event events[iodepth];
		struct io_event *ep;
		int n;

		if (RandomFlag) {
			offset = block * (lrand48()%numrecs64);

			if (lseek(FD, offset, 0) < 0) {
				perror("lseek");
				exit(68);
			}
		}

		start = GetTime();
		for (i = 0; i < iodepth; i++) {
			struct iocb *io = (struct iocb*)malloc(sizeof(struct iocb));
			io_prep_pread(io, FD, membuf, block, offset);

			ioq[i] = io;
			offset += block;
		}

		//if (Verbose) printf("AIO prepare done, now submitting...\n");

		if (iodepth != io_submit(myctx, iodepth, ioq)) {
			perror("Failure on submit.");
			exit(-1);
		}

		//if (Verbose) printf("Now awaiting completion..\n");

		n = io_getevents(myctx, iodepth, iodepth, events, NULL);

		for (ep = events; n-- > 0; ep++) {
			struct iocb *iocb = ep->obj;
			if (ep->res2 != 0) io_error("aio write", ep->res2);
			if (ep->res != iocb->u.c.nbytes) {
				printf("write missed bytes expect %lu got %ld\n", iocb->u.c.nbytes, ep->res);
			}

			count += iocb->u.c.nbytes;
			loop++;
		}

		totaltime += GetTime() - start;
	}

	data->time = totaltime;
	io_destroy(myctx);
	return (loop);
}
