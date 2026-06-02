#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <pthread.h>	// For pthread_create and pthread_join
#include <sys/wait.h>	// For wait

#include "scull.h"		// For ioctl defs and struct task_info

#define CDEV_NAME "/dev/scull"

/* Quantum command line option */
static int g_quantum;

static void usage(const char *cmd)
{
	printf("Usage: %s <command>\n"
	       "Commands:\n"
	       "  R          Reset quantum\n"
	       "  S <int>    Set quantum\n"
	       "  T <int>    Tell quantum\n"
	       "  G          Get quantum\n"
	       "  Q          Query quantum\n"
	       "  X <int>    Exchange quantum\n"
	       "  H <int>    Shift quantum\n"
		   // Added commands
		   "  i          IOCTL info\n"
           "  p          Processes test\n"
           "  t          Threads test\n"
	       "  h          Print this message\n",
	       cmd);
}

typedef int cmd_t;

static cmd_t parse_arguments(int argc, const char **argv)
{
	cmd_t cmd;

	if (argc < 2) {
		fprintf(stderr, "%s: Invalid number of arguments\n", argv[0]);
		cmd = -1;
		goto ret;
	}

	/* Parse command and optional int argument */
	cmd = argv[1][0];
	switch (cmd) {
	case 'S':
	case 'T':
	case 'H':
	case 'X':
		if (argc < 3) {
			fprintf(stderr, "%s: Missing quantum\n", argv[0]);
			cmd = -1;
			break;
		}
		g_quantum = atoi(argv[2]);
		break;
	case 'R':
	case 'G':
	case 'Q':
	// Added cases
	case 'i':
    case 'p':
    case 't':
	case 'h':
		break;
	default:
		fprintf(stderr, "%s: Invalid command\n", argv[0]);
		cmd = -1;
	}

ret:
	if (cmd < 0 || cmd == 'h') {
		usage(argv[0]);
		exit((cmd == 'h')? EXIT_SUCCESS : EXIT_FAILURE);
	}
	return cmd;
}

static int do_op(int fd, cmd_t cmd)
{
	int ret, q;

	switch (cmd) {
	case 'R':
		ret = ioctl(fd, SCULL_IOCRESET);
		if (ret == 0)
			printf("Quantum reset\n");
		break;
	case 'Q':
		q = ioctl(fd, SCULL_IOCQQUANTUM);
		printf("Quantum: %d\n", q);
		ret = 0;
		break;
	case 'G':
		ret = ioctl(fd, SCULL_IOCGQUANTUM, &q);
		if (ret == 0)
			printf("Quantum: %d\n", q);
		break;
	case 'T':
		ret = ioctl(fd, SCULL_IOCTQUANTUM, g_quantum);
		if (ret == 0)
			printf("Quantum set\n");
		break;
	case 'S':
		q = g_quantum;
		ret = ioctl(fd, SCULL_IOCSQUANTUM, &q);
		if (ret == 0)
			printf("Quantum set\n");
		break;
	case 'X':
		q = g_quantum;
		ret = ioctl(fd, SCULL_IOCXQUANTUM, &q);
		if (ret == 0)
			printf("Quantum exchanged, old quantum: %d\n", q);
		break;
	case 'H':
		q = ioctl(fd, SCULL_IOCHQUANTUM, g_quantum);
		printf("Quantum shifted, old quantum: %d\n", q);
		ret = 0;
		break;
	default:
		/* Should never occur */
		abort();
		ret = -1; /* Keep the compiler happy */
	}

	if (ret != 0)
		perror("ioctl");
	return ret;
}

// Added function that performs SCULL_IOCIQUANTUM ioctl and prints task info
static int do_info(int fd) {
    struct task_info info;
    int ret;

    ret = ioctl(fd, SCULL_IOCIQUANTUM, &info);	// Issues new command

    if (ret < 0) {
        perror("ioctl");

        return ret;
    }

	// Prints returned task info
    printf("state %u, cpu %u, prio %d, pid %d, tgid %d, nv %lu, niv %lu\n", info.__state, info.cpu, info.prio, info.pid, info.tgid, info.nvcsw, info.nivcsw);
    
	return 0;
}

// Added thread function for 't' command
// Each thread performing two IOCTL calls to get task info
static void *thread_func(void *arg) {
    int fd = *(int *)arg;	// Gets file descriptor

	// Two calls here
    do_info(fd);
    do_info(fd);

    return NULL;
}

int main(int argc, const char **argv)
{
	int fd, ret = 0, i;	// Changed
	cmd_t cmd;

	cmd = parse_arguments(argc, argv);	// Parses command-line args

	fd = open(CDEV_NAME, O_RDONLY);		// Opens device file
	if (fd < 0) {
		perror("cdev open");
		return EXIT_FAILURE;
	}

	printf("Device (%s) opened\n", CDEV_NAME);	// Message to show device opening

	// Handles new commands
	if (cmd == 'i') {
        // Single call to get task_info
        ret = do_info(fd);
    } else if (cmd == 'p') {
        // Creates 4 child processes concurrently
        pid_t pid;
        for (i = 0; i < 4; i++) {
            pid = fork();
			
            if (pid < 0) {
                perror("fork");

                exit(EXIT_FAILURE);
            } else if (pid == 0) {
                // Child process to also call twice and exit
                do_info(fd);
                do_info(fd);

                exit(EXIT_SUCCESS);
            }
        }
		
		// Parent waits for all children to complete
        for (i = 0; i < 4; i++)
            wait(NULL);

    } else if (cmd == 't') {
		// Creates 4 threads concurrently
        pthread_t threads[4];

        for (i = 0; i < 4; i++) {
            if (pthread_create(&threads[i], NULL, thread_func, &fd) != 0) {
                perror("pthread_create");

                exit(EXIT_FAILURE);
            }
        }

		// Waits for all threads to complete
        for (i = 0; i < 4; i++)
            pthread_join(threads[i], NULL);

    } else {
        // Uses original do_op for existing commands
        ret = do_op(fd, cmd);
	}

	if (close(fd) != 0) {
		perror("cdev close");
		return EXIT_FAILURE;
	}

	printf("Device (%s) closed\n", CDEV_NAME);

	return (ret != 0)? EXIT_FAILURE : EXIT_SUCCESS;
}
