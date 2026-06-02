/*******************************************************************************
* Filename : edf.c
* Author : William Ee
* Date : Due 3/14/2025
* Description : Earliest Deadline First Scheduling Algorithm
* Pledge : I pledge my honor that I have abided by the Stevens Honor System.
******************************************************************************/

#include <stdio.h>
#include <stdlib.h>

#define max_inst 1000   // Max number of allowed process instances
// I was going to use "const int" but it ended up in more verbosity

// All time is in ms, of course
// Structure to hold static parameters for the processes
typedef struct {
    int cpu_time;   // Required CPU execution time
    int period;     // Processes' recurring time interval
} ProcessPar;

// Structures representing the processes themselves
typedef struct {
    int pid;        // Process ID (starts from 1)
    int arr_time;   // Instance arrival time
    int rem_time;   // Remaining CPU time until finished
    int deadline;   // Current deadline (= arr_time + period)
    int period;     // Process's recurring time interval
    int cpu_time;   // Required CPU time
    int wait_time;  // Accumulated wait time
    int is_fin;     // Binary flag for finished or not finished (1 and 0 respectively)
} Process;

Process instances[max_inst];    // Array to store the instances
int inst_count = 0;             // Counter for created instances

// Helper to get greatest common divisor of 2 numbers
// Just used to help in LCM function
int gcd(int a, int b) {
    while (b != 0) {
        int temp = a % b;
        a = b;
        b = temp;
    }
    return a;
}

// Helper to get least common multiple of 2 numbers
// Using this to align process periods
int lcm(int a, int b) {
    return (a / gcd(a, b)) * b;
}

// Finds the LCM of array of process periods
// Using this to determine max simulation time (called max_time) later on,
// which makes sure all processes complete a full cycle (period)
int compute_lcm(ProcessPar procs[], int n) {
    int result = procs[0].period;

    for (int i = 1; i < n; i++) {
        result = lcm(result, procs[i].period);
    }

    return result;
}

// Chooses instance to run (the one with the earliest deadline that's arrived)
// Naturally the core of the EDF algorithm for this program
int choose_current(int time) {
    int current = -1;     // -1 indicating no initial choice

    // Iterates through instances to find one with earliest deadline
    for (int i = 0; i < inst_count; i++) {
        // Ignores any instances that are finished / have remaining CPU time / haven't arrived yet
        // Essentially here for better readability (don't have to stuff it into the following conditional)
        if (instances[i].is_fin || instances[i].rem_time <= 0 || instances[i].arr_time > time)
            continue;

        // Chooses the instance with earliest deadline
        if (current == -1 || 
           instances[i].deadline < instances[current].deadline || 
           (instances[i].deadline == instances[current].deadline && instances[i].arr_time < instances[current].arr_time) ||
           (instances[i].deadline == instances[current].deadline && instances[i].arr_time == instances[current].arr_time && instances[i].pid < instances[current].pid)) {
            current = i;    // Updates the new current instance
        }
    }

    return current;
}

// Prints the ready queue at current time
// Holds all instances that have arrived but aren't finished ("ready" instances)
// Using priority queue 
void show_queue(int time) {
    Process *active[max_inst];  // Array to hold active instances
    int count = 0;              // Counter for active instances

    // Gathers all ready instances
    for (int i = 0; i < inst_count; i++) {
        if (!instances[i].is_fin && instances[i].arr_time <= time && instances[i].rem_time > 0) {
            active[count++] = &instances[i];    // Stores pointer to active instance
        }
    }

    // Sorts active instances by arrival time (breaks any ties with PID)
    // Used bubble sort for this, CS 385 paid off
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (active[i] -> arr_time > active[j] -> arr_time || (active[i] -> arr_time == active[j] -> arr_time && active[i] -> pid > active[j] -> pid)) {
                Process *temp = active[i];      // Swaps instances
                active[i] = active[j];
                active[j] = temp;
            }
        }
    }

    // Prints the newly sorted queue
    printf("%d: processes (oldest first):", time);

    // Iterates through active procs and shows their details (PID and remaining time)
    for (int i = 0; i < count; i++) {
        printf(" %d (%d ms)", active[i] -> pid, active[i] -> rem_time);
    }

    printf("\n");
}

int main(void) {
    int n;      // Holds number of processes

    // User prompt for number of procs
    printf("Enter the number of processes to schedule: ");
    if (scanf("%d", &n) != 1) {     // Exits if input is invalid
        return 1;
    }
    
    // Using malloc for proc params and instances
    ProcessPar *procs = malloc(n * sizeof(ProcessPar));
    
    // User prompt for CPU time and period of each proc
    for (int i = 0; i < n; i++) {
        printf("Enter the CPU time of process %d: ", i + 1);
        scanf("%d", &procs[i].cpu_time);
        printf("Enter the period of process %d: ", i + 1);
        scanf("%d", &procs[i].period);
    }
    
    // Finds max simulation time (LCM of all proc periods)
    int max_time = compute_lcm(procs, n);
    
    // Makes initial instances (arrives at time 0)
    for (int i = 0; i < n; i++) {
        instances[inst_count].pid = i + 1;
        instances[inst_count].arr_time = 0;
        instances[inst_count].rem_time = procs[i].cpu_time;
        instances[inst_count].deadline = procs[i].period;
        instances[inst_count].period = procs[i].period;
        instances[inst_count].cpu_time = procs[i].cpu_time;
        instances[inst_count].wait_time = 0;
        instances[inst_count].is_fin = 0;
        inst_count++;
    }
    
    // Initializing scheduling variables
    int run_ind = -1;           // Index of currently running instance (-1 if none)
    int finish_time = -1;       // Time when current instance will finish
    int finish_ind = -1;        // Index of instance that will finish
    int total_wait_time = 0;    // Accumulated wait time for all procs
    
    show_queue(0);                  // Shows ready queue at time 0
    run_ind = choose_current(0);    // Finds which proc should start running

    if (run_ind != -1) {
        printf("0: process %d starts\n", instances[run_ind].pid);   // Prints process that starts at 0
    }
    
    // Program loop from time = 0 up to max_time, exclusive.
    for (int t = 0; t < max_time; t++) {
        // Handles process completion
        // Essentially a base case
        if (t == finish_time && finish_ind != -1) {
            printf("%d: process %d ends\n", t, instances[finish_ind].pid);  // Show process completion
            instances[finish_ind].is_fin = 1;   // Updates finished value

            // Resets state if finished process was the current one
            if (run_ind == finish_ind) {
                run_ind = -1;
                finish_ind = -1;
                finish_time = -1;
            }
        }
        
        // Checks for any missed deadlines
        int missed_ind[100], missed_count = 0;      // Array to track procs that missed deadlines

        for (int i = 0; i < inst_count; i++) {
            // Finds the procs that missed their deadlines
            if (!instances[i].is_fin && instances[i].arr_time <= t && instances[i].rem_time > 0 && instances[i].deadline == t) {
                missed_ind[missed_count++] = i;     // Stores index of process that missed deadline
            }
        }

        // Sorts missed deadlines by increasing PID and then by arrival time
        for (int i = 0; i < missed_count - 1; i++) {
            for (int j = i + 1; j < missed_count; j++) {
                int ind_i = missed_ind[i], ind_j = missed_ind[j];

                // Swaps to maintain sorting order (if necessary)
                if (instances[ind_i].pid > instances[ind_j].pid || (instances[ind_i].pid == instances[ind_j].pid && instances[ind_i].arr_time > instances[ind_j].arr_time)) {
                    int temp = missed_ind[i];
                    missed_ind[i] = missed_ind[j];
                    missed_ind[j] = temp;
                }
            }
        }

        // Handles the missed deadlines
        for (int i = 0; i < missed_count; i++) {
            int ind = missed_ind[i];
            int left = instances[ind].rem_time;     // Remaining execution time
            int new_deadline = t + instances[ind].period; // Computes the new deadline
            
            printf("%d: process %d missed deadline (%d ms left), new deadline is %d\n", t, instances[ind].pid, left, new_deadline);
            instances[ind].deadline = new_deadline;     // Updates the deadline into the new one
        }
        
        // Handles new arrivals
        int new_arr_time = 0;

        for (int i = 0; i < n; i++) {
            // Checks if process should arrive at current time
            if (t > 0 && t < max_time && (t % procs[i].period == 0)) {
                // Creates a new instance
                // Is there a less verbose way to do this?
                instances[inst_count].pid = i + 1;
                instances[inst_count].arr_time = t;
                instances[inst_count].rem_time = procs[i].cpu_time;
                instances[inst_count].deadline = t + procs[i].period;
                instances[inst_count].period = procs[i].period;
                instances[inst_count].cpu_time = procs[i].cpu_time;
                instances[inst_count].wait_time = 0;
                instances[inst_count].is_fin = 0;

                inst_count++;       // Increments process count
                new_arr_time = 1;   // Indicates new process arrival
            }
        }

        // Shows ready queue if new procs arrived
        if (new_arr_time) {
            show_queue(t);
        }
        
        // Makes the scheduling decision
        int current = choose_current(t);    // Chooses next proc to run
        if (current != -1) {
            // Starts new proc if no proc was running
            if (run_ind == -1) {
                run_ind = current;
                printf("%d: process %d starts\n", t, instances[run_ind].pid);       // Shows starting proc
            // Preempts current proc if a different proc needs to run
            } else if (run_ind != current) {
                printf("%d: process %d preempted!\n", t, instances[run_ind].pid);   // Shows preempted proc

                run_ind = current;

                printf("%d: process %d starts\n", t, instances[run_ind].pid);
            }
        }
        
        // Executes current running proc for 1 ms
        if (run_ind != -1) {
            instances[run_ind].rem_time--;  // Decrements remaining time

            // Marks completed process for next cycle
            if (instances[run_ind].rem_time == 0) {
                finish_time = t + 1;
                finish_ind = run_ind;
            }
        }
        
        // Increments wait time for all active procs (besides the running one)
        for (int i = 0; i < inst_count; i++) {
            if (!instances[i].is_fin && instances[i].arr_time <= t && i != run_ind) {
                instances[i].wait_time++;   // Increments wait time
                total_wait_time++;          // Increments summed wait time
            }
        }
    }
    
    // Handles any process completion at max_time
    if (max_time == finish_time && finish_ind != -1) {
        printf("%d: process %d ends\n", max_time, instances[finish_ind].pid);   // Shows final process completion
        instances[finish_ind].is_fin = 1;   // Updates finished value

        // A final reset!
        run_ind = -1;
        finish_ind = -1;
        finish_time = -1;
    }
    
    // Handles end-of-simulation outputs
    printf("%d: Max Time reached\n", max_time);                 // Shows that max_time was reached
    printf("Sum of all waiting times: %d\n", total_wait_time);  // Shows total wait time
    printf("Number of processes created: %d\n", inst_count);    // Shows the final process count

    double avg = 0.0;
    avg = (double) total_wait_time / inst_count;
    printf("Average Waiting Time: %.2lf\n", avg);               // Prints the average wait time to 2 digits of decimal precision
    
    // Frees the allocated memory to avoid points off...
    free(procs);
}
