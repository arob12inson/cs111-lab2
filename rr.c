#include <fcntl.h>
#include <stdbool.h>
#include "stdckdint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <unistd.h>

/* A process table entry.  */
struct process
{
  long pid;
  long arrival_time;
  long burst_time;

  TAILQ_ENTRY (process) pointers;

  /* Additional fields here */
  long remaining_time; // set at initialization, decrement it at wait time
  long start_exec_time; // set when runs for first time
  long waiting_time; // Calculate at the end
  long response_time; // calculate at the end
  long finish_time; // set when it finishes
  /* End of "Additional fields here" */
};

TAILQ_HEAD (process_list, process);

/* Skip past initial nondigits in *DATA, then scan an unsigned decimal
   integer and return its value.  Do not scan past DATA_END.  Return
   the integer’s value.  Report an error and exit if no integer is
   found, or if the integer overflows.  */
static long
next_int (char const **data, char const *data_end)
{
  long current = 0;
  bool int_start = false;
  char const *d;

  for (d = *data; d < data_end; d++)
    {
      char c = *d;
      if ('0' <= c && c <= '9')
	{
	  int_start = true;
	  if (ckd_mul (&current, current, 10)
	      || ckd_add (&current, current, c - '0'))
	    {
	      fprintf (stderr, "integer overflow\n");
	      exit (1);
	    }
	}
      else if (int_start)
	break;
    }

  if (!int_start)
    {
      fprintf (stderr, "missing integer\n");
      exit (1);
    }

  *data = d;
  return current;
}

/* Return the first unsigned decimal integer scanned from DATA.
   Report an error and exit if no integer is found, or if it overflows.  */
static long
next_int_from_c_str (char const *data)
{
  return next_int (&data, strchr (data, 0));
}

/* A vector of processes of length NPROCESSES; the vector consists of
   PROCESS[0], ..., PROCESS[NPROCESSES - 1].  */
struct process_set
{
  long nprocesses;
  struct process *process;
};

/* Return a vector of processes scanned from the file named FILENAME.
   Report an error and exit on failure.  */
static struct process_set
init_processes (char const *filename)
{
  int fd = open (filename, O_RDONLY);
  if (fd < 0)
    {
      perror ("open");
      exit (1);
    }

  struct stat st;
  if (fstat (fd, &st) < 0)
    {
      perror ("stat");
      exit (1);
    }

  size_t size;
  if (ckd_add (&size, st.st_size, 0))
    {
      fprintf (stderr, "%s: file size out of range\n", filename);
      exit (1);
    }

  char *data_start = mmap (NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (data_start == MAP_FAILED)
    {
      perror ("mmap");
      exit (1);
    }

  char const *data_end = data_start + size;
  char const *data = data_start;

  long nprocesses = next_int (&data, data_end);
  if (nprocesses <= 0)
    {
      fprintf (stderr, "no processes\n");
      exit (1);
    }

  struct process *process = calloc (sizeof *process, nprocesses);
  if (!process)
    {
      perror ("calloc");
      exit (1);
    }

  for (long i = 0; i < nprocesses; i++)
    {
      process[i].pid = next_int (&data, data_end);
      process[i].arrival_time = next_int (&data, data_end);
      process[i].burst_time = next_int (&data, data_end);
      if (process[i].burst_time == 0)
	{
	  fprintf (stderr, "process %ld has zero burst time\n",
		   process[i].pid);
	  exit (1);
	}
    }

  if (munmap (data_start, size) < 0)
    {
      perror ("munmap");
      exit (1);
    }
  if (close (fd) < 0)
    {
      perror ("close");
      exit (1);
    }
  return (struct process_set) {nprocesses, process};
}

int compareLongs(const void *a, const void *b) {
    long longA = *((long *)a);
    long longB = *((long *)b);

    if (longA < longB) return -1;
    if (longA > longB) return 1;
    return 0;
}

long calculateMedian(long* array, int size){
  qsort(array, size, sizeof(long), compareLongs);
  if (size % 2 == 1) {
      // Odd number of elements, median is the middle element
      return array[size / 2];
  } else {
      // Even number of elements, calculate the average of the two middle elements
      long middle1 = array[(size - 1) / 2];
      long middle2 = array[size / 2];

      // Check if rounding to the nearest even number is needed
      if ((middle1 + middle2) % 2 == 0) {
          return (middle1 + middle2) / 2;
      } else {
          return (middle1 + middle2 + 1) / 2;
      }
  }
}

int calculateQuantum(bool medianMode,
                     struct process_list* l,
                     struct process* working_process,
                     long num_processes_arrived,
                     bool arrived_processes_added,
                     int qSize){
  if (medianMode == false) {
    return qSize;
  }

  long num_queued = !(!working_process);
  struct process* p = NULL;

  TAILQ_FOREACH(p, l, pointers){
    num_queued ++;
  }

  if (num_processes_arrived && !arrived_processes_added) { //processes that arrived should be include in quantum calculation
    num_queued += num_processes_arrived;
  }

  long* x = (long*) malloc(num_queued*sizeof(long));
  if (!x){
    perror("calloc");
    // note: if you're seeing this, you caught me...
    exit(1);
  }
  int counter = 0;
  while(arrived_processes_added == false && counter < num_processes_arrived){
    x[counter] = 0;
    counter++;
  }
  if (!(!working_process) == true){
    p = working_process;
    x[counter] = p->burst_time - p->remaining_time;
    counter++;
  }
  TAILQ_FOREACH(p, l, pointers){
    x[counter] = p->burst_time - p->remaining_time;
    counter++;
  }
  long m = calculateMedian(x, counter);

  if (!m){
    m = 1;
  }
  printf("m: %ld\n", m);

  free(x);
  return m;
}

int
main (int argc, char *argv[])
{
  if (argc != 3)
    {
      fprintf (stderr, "%s: usage: %s file quantum\n", argv[0], argv[0]);
      return 1;
    }

  struct process_set ps = init_processes (argv[1]);
  long quantum_length = (strcmp (argv[2], "median") == 0 ? -1
			 : next_int_from_c_str (argv[2]));
  if (quantum_length == 0)
    {
      fprintf (stderr, "%s: zero quantum length\n", argv[0]);
      return 1;
    }

  struct process_list list;
  TAILQ_INIT (&list);

  long total_wait_time = 0;
  long total_response_time = 0;

  /* Your code here */
  struct process* p = NULL;
  for (int i = 0; i < ps.nprocesses; i++){
    struct process* p = &ps.process[i];
    p->start_exec_time = -1;
    p->remaining_time = p->burst_time;
  }

  int t = 0;
  p = NULL; // p represents current process running on CPU,
  struct process* arriving_process = NULL;
  int num_arrived = 0;
  int quantum_left = -1;
  int median_mode = quantum_length == -1 ? true : false;
  if (median_mode == true){
    quantum_length = 1;
  }

  while (TAILQ_EMPTY(&list) == false || p != NULL || num_arrived < ps.nprocesses){ // while theres still something left to process (in queue or CPU)
    int num_processes_arrived = 0;
    bool arrived_processes_added = false;
    for (int i = 0; i < ps.nprocesses; i++) {
      arriving_process = &ps.process[i];
      if (arriving_process->arrival_time == t) {
        num_processes_arrived++;
      }
    }

    if (TAILQ_EMPTY(&list) == true && p == NULL){ //handling if the processor is doing nothing
      if (num_processes_arrived == 0){
        t++;
        continue;
      }
      for (int i = 0; i < ps.nprocesses; i++) {
        arriving_process = &ps.process[i];
        if (arriving_process->arrival_time == t) {
          TAILQ_INSERT_TAIL(&list, arriving_process, pointers);
          num_arrived++;
        }
        arrived_processes_added = true;
      }
    }

    if (p == NULL){
      quantum_length = calculateQuantum(median_mode, &list, p, num_processes_arrived, arrived_processes_added, quantum_length);
      p = TAILQ_FIRST(&list); // does this pop off?
      TAILQ_REMOVE(&list, p, pointers);
      if (p->start_exec_time == -1){
        p->start_exec_time = t;
      }
      quantum_left = quantum_length;
    }

    if (p->remaining_time == 0) { // if the process finishes
      p->finish_time = t;
      p = NULL;
    } else if (quantum_left == 0) { // need a quantum switch
      // need to handle if queue is is empty
      if (TAILQ_EMPTY(&list) == false){ // quantum switch only if need to
        // seperate the last num_processes_arrived, then put them back in
        TAILQ_INSERT_TAIL(&list, p, pointers);
        p = NULL;
      }
      else { // schedules a quantum
        quantum_length = calculateQuantum(median_mode, &list, p, num_processes_arrived, arrived_processes_added, quantum_length);
        quantum_left = quantum_length;
        p->remaining_time--;
        quantum_left--;
      }
    } else {
      p->remaining_time--;
      quantum_left--;
    }

    if (arrived_processes_added == false){
      for (int i = 0; i < ps.nprocesses; i++) {
        arriving_process = &ps.process[i];
        if (arriving_process->arrival_time == t) {
          TAILQ_INSERT_TAIL(&list, arriving_process, pointers);
          num_arrived++;
        }
      }
    }
    t++;

  }

  // calculate individual wait & response time
  for (int i = 0; i < ps.nprocesses; i++){
    p = &ps.process[i];
    p->waiting_time = p->finish_time - p->arrival_time - p->burst_time;
    p->response_time = p->start_exec_time - p->arrival_time;
    printf("%ld\t%ld\n", p->waiting_time, p->response_time);
  }
  // calculate average wait & response time
  for (int i = 0; i < ps.nprocesses; i++){
    p = &ps.process[i];
    total_wait_time += p->waiting_time;
    total_response_time += p->response_time;
  }
  /* End of "Your code here" */

  printf ("Average wait time: %.2f\n",
	  total_wait_time / (double) ps.nprocesses);
  printf ("Average response time: %.2f\n",
	  total_response_time / (double) ps.nprocesses);

  if (fflush (stdout) < 0 || ferror (stdout))
    {
      perror ("stdout");
      return 1;
    }

  free (ps.process);
  return 0;
}
