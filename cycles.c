/*
 * cycles.c: Count the (approximate) number of CPU cycles.
 */

#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>

#define KILO 1000
#define MEGA KILO/1000
#define GIGA MEGA/1000

/*
 * Call the `rdtsc' (op-code: 0F 31) instruction and read the outputs
 * from the `EAX' and `EDX' registers.
 *
 * Note: In x86-64 mode, the instruction clears the higher 32 bits
 * of `RAX' and `DAX' registers.
 */
static uint64_t rdtsc(void) {
    uint64_t ax, dx;
    /*
     * Use `=a', `=d' constraint instead  the `=A' constraint, which
     * would put the timestamp counter value in either `EAX' or `EDX'.
     */
    __asm__ __volatile__("rdtsc": "=a"(ax), "=d"(dx));
    /*
     * Use left-shift and OR for getting the full value.
     */
    return (((uint64_t)dx << 32) | ax);
}

/*
 * Define frequency prefixes here.
 */
enum prefixes {kilo, mega, giga};

/*
 * Helper function for exit on error.
 */
void bail(char *error) {
    perror(error);
    exit(EXIT_FAILURE);
}

/*
 * Prints the usage of the executable.
 */
void usage(char *program, int intro) {
    if (intro)
        printf("%s: Count the (approximate) number of CPU cycles.\n", program);

    printf("usage:\n    %s --time N [--proc P] [--freq [--prefix]]\n"
           "arguments (`*\' - required argument):\n"
           "    -t/--time    * sampling interval (in seconds)*\n"
           "    -c/--proc      processor number to set the thread affinity\n"
           "    -f/--freq      display the frequency for the processor\n"
           "    -p/--prefix    metric prefix for the frequency (in Hz)\n"
           "                   should be one of: {khz, mhz, ghz}\n"
           "    -h/--help      prints this help menu\n"
           "    -v/--verbose   verbose output\n",
           program);
}

double scale(enum prefixes factor, uint64_t value, double sample_time) {
    switch (factor) {
        case kilo:
            return (value / sample_time / KILO);
        case mega:
            return (value / sample_time / MEGA);
        case giga:
            return (value / sample_time / GIGA);
        default:
            return -1;
    }
}
int main(int argc, char *argv[]) {
    cpu_set_t task_set;
    uint64_t start, end, delta;
    double sample = 0.0;
    enum prefixes metric;
    int errno, option_index, cpu_id = 0;
    char arg, hertz = 0, pflag = 0, verbose = 0,
         *endptr = malloc(sizeof(char) * 1),
         *metric_pfx = malloc(sizeof(char) * 4);

    while (1) {
        /*
         * Define the argument list and parse them.
         */
        static struct option args[] = {
            {"time",    required_argument, 0, 't'},
            {"proc",    required_argument, 0, 'c'},
            {"freq",    no_argument,       0, 'f'},
            {"prefix",  required_argument, 0, 'p'},
            {"help",    no_argument,       0, 'h'},
            {"verbose", no_argument,       0, 'v'},
            {0,         0,                 0,   0}
        };
        option_index = 0;

        arg = getopt_long(argc, argv, "t:c:fp:hv", args, &option_index);

        if (arg < 0) {
            break;
        }

        switch (arg) {
            case 't':
                errno = 0;
                sample = strtod(optarg, 0);

                if (errno)
                    bail("strtol");

                if (sample <= 0 ) {
                    printf("error: sample time should be a positive value\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'c':
                errno = 0;
                cpu_id = strtol(optarg, &endptr, 10);
                if (errno)
                    bail("strtol");

                if (cpu_id < 0 || errno != 0) {
                    printf("error: processor should be a positive integer\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'f':
                hertz = 1;
                break;
            case 'p':
                if (strlen(optarg) == 3 && optarg[1] == 'h' &&
                    optarg[2] == 'z') {
                    switch (optarg[0]) {
                        case 'k':
                            metric = kilo;
                            break;
                        case 'm':
                            metric = mega;
                            break;
                        case 'g':
                            metric = giga;
                            break;
                        default:
                            printf("error: illegal metric prefix `%s\'\n",
                                   optarg);
                            exit(EXIT_FAILURE);
                    }
                } else {
                    printf("error: illegal metric `%s\'\n", optarg);
                    exit(EXIT_FAILURE);
                }
                pflag = 1;
                metric_pfx = optarg;
                break;
            case 'h':
                usage(argv[0], 1);
                exit(EXIT_SUCCESS);
            case 'v':
                verbose = 1;
                break;
            case '?':
                break;
            default:
                exit(EXIT_FAILURE);
        }
    }

    if (!sample) {
        printf("error: `--time\' is a required argument\n");
        exit(EXIT_FAILURE);
    }
    if ((!pflag && hertz) || (pflag && !hertz)) {
        printf("error: missing `--freq\' switch or "
               "`--prefix\' argument (need both)\n");
        exit(EXIT_FAILURE);
    }

    /*
     * Make sure that all of this runs on the same processor.
     * Clear the CPU set; add a CPU to the set (0 is the default); set the
     * CPU affinity mask for this thread (PID) for that CPU.
     */
    CPU_ZERO(&task_set);
    CPU_SET(cpu_id, &task_set);
    if (sched_setaffinity(getpid(), sizeof(task_set), &task_set) < 0)
        bail("sched_setaffinity");

    /*
     * Fetch the initial reading of the counter; sleep (or run any
     * arbitrary code); get the final reading of the counter.
     */
    start = rdtsc();
    sleep(sample);
    end = rdtsc();
    delta = (end - start);

    if (!hertz) {
        if (verbose) {
            printf("processor_id    | %d\n"
                   "sample_time     | %4lf\n"
                   "cycles_elapsed  | %4ld\n"
                   "start_timestamp | %4ld\n"
                   "end_timestamp   | %4ld\n",
                   cpu_id, sample, delta, start, end);
        } else {
            printf("%ld\n", delta);
        }
    } else {
        if (verbose) {
            printf("processor_id    | %d\n"
                   "sample_time     | %lf\n"
                   "frequency_%3s   | %lf\n"
                   "cycles_elapsed  | %ld\n"
                   "start_timestamp | %ld\n"
                   "end_timestamp   | %ld\n",
                   cpu_id, sample, metric_pfx, scale(metric, delta, sample),
                   delta, start, end);
        } else {
            printf("%lf\n", scale(metric, delta, sample));
        }
    }
    return 0;
}
