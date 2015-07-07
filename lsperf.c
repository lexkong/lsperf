#define _GNU_SOURCE
#define LSPERFVERSION "0.1.0"

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h> //POSIX Standard: 2.6 Primitive System Data Types	<sys/types.h>
#include <sys/stat.h> //POSIX Standard: 5.6 File Characteristics
#include <time.h>
#include <errno.h>
#define STRLEN 4096
#define MAXTHREADS 64

struct thread_data
{
	int threadid;
	long long time;
	int loop;
	int cpu;
};

static int DEBUG = 0;
static int nr_threads = 1;
static long long blocksize = 4096;
static int direct = 0;
static int syncflag = 0;
static int i_write = 0;
static int i_read = 0;
static char filename[STRLEN] = "";
static char *omembuf;
static char *membuf;
static long long filesize = 1048576;
static int i_random = 0;
static int i_timing=0;
static int verbose = 0;
static char i_format[5]="     ";
struct thread_data thread_data_array[MAXTHREADS];

pthread_mutex_t msync[MAXTHREADS];

void parse_option(int argc, char *argv[]);
void usage(char *argv);
int openfile(char *name);
int getmemory(long long bs);
int writefile(int fd);
int readfile(int fd);
long long gettime();
long double format(long double io);
void *iotest(void *thread_data_array);

int main(int argc, char *argv[])
{
    pthread_t thread[MAXTHREADS];
	long double io, duration, total;
	long long start;
	int i, loop, rc;
	void *status;
	pthread_attr_t attr;
	printf("KONGCC\n");exit(0);
	strcpy(i_format, "B/s");
	parse_option(argc, argv);

	if ((strlen(filename) == 0) || filename[0] == '-') {
		fprintf(stderr, "\n Choose a file name first (-f)!\n\n");
		exit(-1);
	}

	if (i_write == i_read) {
		fprintf(stderr, "\n Choose either read(-r) or write (-w)!\n\n");
		exit(-1);
	}

	if (verbose) {
		printf("\n     ___ --- === Lenovo Storage Benchmark === --- ___\n");
		printf("\n Main process is running on CPU: %i\n", sched_getcpu());
		start=gettime();
		duration=(long double)(gettime()-start)/1000000000;
		printf(" Delay due to time measurement: %.6Lf seconds\n",duration);
	}

	getmemory(blocksize);

	if (verbose) {
		if (direct) printf("Flag O_DIRECT set\n");
		if (syncflag) printf("Flag O_SYNC set\n");
	}


	/* Start all threads*/
	for (i = 0; i < nr_threads; i++) {
		thread_data_array[i].threadid = i;
		thread_data_array[i].time = 0;
		pthread_mutex_init(&msync[i], NULL);
		pthread_mutex_lock(&msync[i]);
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

		rc = pthread_create(&thread[i], &attr, iotest, (void*)&thread_data_array[i]);
		if (rc) {
			fprintf(stderr, "ERROR; return code from pthread_create() is %d\n", rc);
			exit(-1);
		}
	}

	if(verbose) {
		sleep(1);
		printf("\n Starting benchmark...\n");
	}

	for (i = 0; i < nr_threads; i++) {
		pthread_mutex_unlock(&msync[i]);
	}

	if (verbose) printf("\n All threads unlocked\n");
	total = 0;

	/* threads created, wait for result*/
	for (i = 0; i < nr_threads; i++) {
		rc = pthread_join(thread[i], &status);

		if (rc) {
			fprintf(stderr, "ERROR; return code form pthread_join() is %d\n", rc);
			exit(-1);
		}

		if (i ==0) {
			if (verbose) {
				sleep(1);
				printf("\n Result:");
				printf("\n -------- \n");
			}
			printf("\n %Li Bytes copied per thread, block size %Li\n\n", filesize, blocksize);
		}

		duration = (long double) thread_data_array[i].time / 1000000000;
		io = (long double) filesize/duration;
		total += io;
		io = format(io);
		loop = thread_data_array[i].loop;
		printf("Thread%3i (CPU: %2i): %.6Lf seconds -> %.2Lf %s", i, thread_data_array[i].cpu, duration, io, i_format);
		if (loop > 1) printf(" in %i loops\n", loop);
		else printf("\n");
	}

	printf("\n");
	if (nr_threads > 1) {
		total = format(total);
		printf("Total: %.2Lf %s, medium: %.2Lf %s\n\n", total, i_format, total/nr_threads, i_format);
	}

	duration = (long double)(gettime() - start) / 1000000000;
	free(omembuf);
	exit(0);
}

void parse_option(int argc, char *argv[])
{
	int opt, size;
	char c;
	char options[] = "b:dDf:hj:rRs;STvVw";

	while ((opt = getopt(argc, argv, options)) != -1) {
		switch(opt) {
		case 'b':
			c = optarg[strlen(optarg) - 1];
			blocksize = atoi(optarg);

			// 全部转化位byte
			if ((c == 'k') || (c == 'K')) blocksize <<= 10;
			if ((c == 'm') || (c == 'M')) blocksize <<= 20;
			if ((c == 'g') || (c == 'G')) blocksize <<= 30;

			if(DEBUG) printf("BlockSize: %Li\n", blocksize);
			break;
		case 'j':
			nr_threads = atoi(optarg);
			if (nr_threads < 1) nr_threads = 1;
			if (nr_threads > MAXTHREADS) {
				fprintf(stderr, "Maximum number of threads is reached\n");
				fprintf(stderr, "Please increase MAXTHREADS(%i) and recompile\n", MAXTHREADS);
				exit(-1);
			}
			break;
		case 'v':
			verbose = 1;
			if (DEBUG) printf("Verbose mode is active\n");
			break;
		case 'V':
			printf("\n lsperf Version %s", LSPERFVERSION);
			exit(0);
			break;
		case 'R':
			i_random = 1;
			if (DEBUG) printf("Enabled Random buffer filling\n");
			break;
		case 'S':
			syncflag = 1;
			if (DEBUG) printf("O_SYNC enabled\n");
			break;
		case 'T':
			i_timing = 1;
			if(DEBUG) printf("Timing enabled\n");
			break;
		case 's':
			c = optarg[strlen(optarg) - 1];
			filesize = atoi(optarg);

			// 全部转化位byte
			if ((c == 'k') || (c == 'K')) filesize <<= 10;
			if ((c == 'm') || (c == 'M')) filesize <<= 20;
			if ((c == 'g') || (c == 'G')) filesize <<= 30;
			if (DEBUG) printf("FileSize: %Li\n", filesize);
			break;
		case 'd':
			DEBUG = 1;
			printf("Debug enabled\n");
			break;
		case 'r':
			i_read = 1;
			if (DEBUG) printf("Read enabled\n");
			break;
		case 'D':
			direct = 1;
			if (DEBUG) printf("O_DIRECT enabled\n");
			break;
		case 'f':
			size = strlen(optarg);
			if (size > STRLEN - 4) {
				fprintf(stderr, "Size of filename %s too big, allowed is %i\n", optarg, STRLEN - 4);
				exit(-1);
			}

			strncpy(filename, optarg, size);
			filename[size] = '\0';
			if(DEBUG) printf("FileName: %s\n", filename);
			break;
		case 'h':
			usage(argv[0]);
			break;
		case 'w':
			i_write = 1;
			if (DEBUG) printf("Write enabled\n");
			break;
		default:
			usage(argv[0]);
		}
	}
}

void usage(char *argv)
{
  printf("\n Usage: %s [-dDhrRSTvVw] [-j NrThreads] [-b BlockSize] [-s FileSize] [-f FileName]\n",argv);
  printf("\n -b BlockSize   : Size of memory block used for writing");
  printf("\n -d             : Activate debug mode");
  printf("\n -D             : Use direct access (O_DIRECT)");
  printf("\n -f FileName    : Name of file to use");
  printf("\n -h             : print this help page");
  printf("\n -j NrThreads   : Use NrThreads parallel proesses");
  printf("\n -r             : Open file for reading");
  printf("\n -R             : Randomize buffer before it is written to file");
  printf("\n -s FileSize    : Bytes to read/write from/to the file");
  printf("\n -S             : Open file with O_SYNC");
  printf("\n -T             : Calculate Timing for memcpy, bcopy and memmove");
  printf("\n -v             : Verbose mode");
  printf("\n -V             : Print version and exit");
  printf("\n -w             : Open file for writing");
  printf("\n\n Be aware: You have to use either '-r' or '-w'!\n\n");
  exit(0);
}

int getmemory(long long bs)
{
	long long pagesize, offset;
	long long count, bytes;
	int fd;
	long long start;
	long double io, duration;
	char *membuf2;
	char *omembuf2;

	if (bs < 0) {
		fprintf(stderr, "Invalid buffer size: %Li", bs);
		exit(-1);
	}

	pagesize = getpagesize();

	if ((bs%pagesize) && (direct)) {
		fprintf(stderr, "Please choose buffersize modulo %Li\n", pagesize);
		exit(-1);
	}

	membuf = malloc(bs + pagesize);
	omembuf = membuf;
	if (membuf == NULL) {
		fprintf(stderr, "Malloc failed!\n");
		exit(-1);
	}

	if (DEBUG) printf("membuf before alignment: %p\n", membuf);
	offset = pagesize - (long long)membuf % pagesize;
	if(DEBUG) printf("offset: %Li\n", offset);
	membuf += offset;

	if(i_timing) {
		membuf2 = malloc(bs + pagesize);
		if (membuf2 == NULL) {
			fprintf(stderr, "Malloc for membuf2 failed\n");
			exit(-1);
		}

		omembuf2 = membuf2;
		membuf2 += offset;
	}
	if (DEBUG) printf("membuf after alignment: %p\n", membuf);
	if (DEBUG) printf("pagesize: %Li\n", pagesize);
	if (DEBUG) printf("Allocated memory: %Li at %p\n", bs, membuf);
	if (verbose) printf("\n Buffer size: %Li\n", bs);
	start = gettime();

	if (memset(membuf, 0, bs) != membuf) {
		perror("Problem with memset!");
		exit(-1);
	}

	if (i_timing || verbose) {
		duration = (long double) (gettime() - start) / 100000000;
		io = format(bs/duration);
		printf("\n memset took %Lf seconds -> %.2Lf %s\n", duration, io, i_format); // the speed of memset
	}

	if (i_timing) {
		start = gettime();
		memcpy(membuf2, membuf, bs);
		duration = (long double)(gettime() - start) / 1000000000;
		io = format(bs/duration);
		printf("memcpy took %Lf seconds -> %.2Lf %s \n", duration, io, i_format);

		start = gettime();
		bcopy(membuf, membuf2, bs);
		duration = (long double)(gettime() - start) / 1000000000;
		io = format(bs/duration);
		printf(" bcopy   took %Lf seconds -> %.2Lf %s \n", duration, io, i_format);

		start = gettime();
		memmove(membuf2, membuf, bs);
		duration=(long double)(gettime()-start)/1000000000;
		io=format(bs/duration);
		printf(" memmove took %Lf seconds -> %.2Lf %s \n", duration, io, i_format);
		free(omembuf2);
	}

	if (i_random && i_write) {
		start = gettime();
		if ((fd = open("/dev/urandom", O_RDONLY | O_NONBLOCK)) == -1) {
			perror("Problem to open /dev/random");
			exit(-1);
		}
		count = 0;
		printf("Filling buffer with randomized values...\n");
		while (count < bs) {
			bytes = read(fd, membuf, bs);
			count += bytes;
		}

		duration = (long double)(gettime() - start) / 1000000000;
		io=format(bs/duration);
		printf(" Randomization took %Lf seconds -> %.2Lf %s \n", duration, io, i_format);
		close(fd);
	}

	if (verbose) printf("\n Initialization of memory finished.\n\n");
	return 0;
}

// 多研究下
long long gettime()
{
	struct timespec ts;
	long long nsec;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	if(DEBUG) printf("Seconds: %i, %Li\n",(int)ts.tv_sec, (long long)ts.tv_nsec);
	nsec=(long long)ts.tv_sec*1000*1000*1000+(long long)(ts.tv_nsec);
	if (DEBUG) printf("nsec: %.10Li\n",nsec);
	return(nsec);
}

long double format(long double io)
{
	if (io >= 1024 * 1024 * 1024) {
		strcpy(i_format, "GB/s");
		io = io / 1024 / 1024 /1024;
	}
	else if (io >= 1024 * 1024) {
		strcpy(i_format, "MB/s");
		io = io / 1024 / 1024;
	}
	else if (io >= 1024) {
		strcpy(i_format, "kB/s");
		io = io / 1024;
	}
	else {
		strcpy(i_format, "B/s");
	}

	return(io);
}

void *iotest(void *thread_data_array)
{
	struct thread_data *data;
	char name[STRLEN];
	int cpu, fd, i;
	long long start;

	data = (struct thread_data *)thread_data_array;
	i = data->threadid;
	data->cpu = sched_getcpu();
	if (DEBUG) printf("Thread %i: Running on CPU: %i\n", i, data->cpu);
	if (nr_threads == 1) strcpy(name, filename);
	else sprintf(name, "%s%i", filename, i);

	if (DEBUG) printf("Using file name: %s\n", name);

	fd = openfile(name);
	pthread_mutex_lock(&msync[i]);
	start = gettime();

	if (i_write) data->loop = writefile(fd);
	if (i_read) data->loop = readfile(fd);

	data->time = gettime() - start;
	cpu = sched_getcpu();

	if (cpu != data->cpu) {
		if (verbose) printf("CPU of thread %i changed from %i to %i\n", i, data->cpu, cpu);
		data->cpu = cpu;
	}

	close(fd);

	return 0;
}

int writefile(int fd)
{
	long long count = filesize;
	size_t block = blocksize;
	long long wb;
	int loop = 0;

	if (count == 0) {
		fprintf(stderr, "File size not set, use -s <size>!\n");
		exit(-1);
	}

	while (count) {
		if (count < blocksize) {
			block = count;
		}
		wb = write(fd, membuf, block);
		loop++;

		if (wb < 0) {
			perror("Error write");
			printf("Failed to write to (%i) from %p number of bytes: %Li\n",
				   fd, membuf, (long long)block);
			exit(-1);
		}
		if (wb != block) {
			fprintf(stderr, "Short write!\n");
			fprintf(stderr, "Failed to write full block(%Li): %Li\n", (long long)block, wb);
		}

		count -= wb;
	}
	return loop;
}

int readfile(fd)
{
	long long count = 0;
	size_t block = blocksize;
	long long wb;
	int endless = 0;
	int loop = 0;

	if (filesize == 0) endless = 1;

	while (endless || count < filesize) {
		if ((filesize > 0) && (count + blocksize > filesize)) {
			block = filesize - count;
		}
		wb = read(fd ,membuf, block);
		loop++;

		if (wb < 0) {
			perror("Error read");
			fprintf(stderr, "Failed to read to (%i) from %p number of bytes: %Li\n",
					fd, membuf, (long long)block);
			exit(-1);
		}
		if (wb != block) {
			fprintf(stderr, "Short read!\n");
			fprintf(stderr, "Failed to read full block (%Li): %Li\n",
					(long long)block, wb);
		}

		if (wb == 0) {
			filesize = count;
			return loop;
		}
		count += wb;
	}

	return loop;
}

int openfile(char *name)
{
	int fd;
	int flags = O_RDONLY;
	if (i_write) flags = O_CREAT | O_RDWR;
	if (direct) flags |= O_DIRECT;
	flags |= O_LARGEFILE;

	if (syncflag) flags |= O_SYNC;

	if ((fd = open(name, flags, S_IRUSR | S_IWUSR)) == -1) {
		perror("Error opening file");
		fprintf(stderr, "Filename: %s\n",name);
		exit(-1);
	}

	if (DEBUG) printf("File opened: %i\n", fd);
	if (verbose) {
		if (i_write) printf("File opened for writing: %s\n", name);
		else printf("File opened for reading: %s\n", name);
	}

	return fd;
}
