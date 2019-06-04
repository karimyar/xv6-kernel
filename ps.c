
#ifdef CS333_P4 
#include "types.h"
#include "user.h"
#include "uproc.h"

int main(int argc, char *argv[])
{
  uint max_entries = 72;
  uint seconds, milliseconds, cpu_seconds, cpu_milli;
  int size = 0;
  int i;
  struct uproc *table = malloc(max_entries * sizeof(struct uproc));
  
  if(table == 0)
  { 
    printf(1, "Error allocating table\n");
    exit(); 
  }
  size = getprocs(max_entries, table); 

  if(size < 0)
  {
    exit(); 
  }
  #define HEADER "\nPID\tName\tUID\tGID\tPPID\tPrio\tElapsed\tCPU\tState\tSize\n" 
  printf(1, HEADER); 
  for(i = 0; i < size; i++)
  {
    printf(1, "%d\t", table[i].pid);
    printf(1, "%s\t", table[i].name);
    printf(1, "%d\t", table[i].uid);
    printf(1, "%d\t", table[i].gid);
    printf(1, "%d\t", table[i].ppid);
    printf(1, "%d\t", table[i].priority);
    seconds = table[i].elapsed_ticks / 1000; 
    milliseconds = table[i].elapsed_ticks % 1000;
    if(milliseconds < 10)
      printf(1, "%d.00%d\t", seconds, milliseconds); 
    else if(milliseconds < 100)
      printf(1, "%d.0%d\t", seconds, milliseconds);
    else 
      printf(1, "%d.%d\t", seconds, milliseconds); 
    cpu_seconds = table[i].CPU_total_ticks / 1000; 
    cpu_milli = table[i].CPU_total_ticks % 1000; 
    if(cpu_milli < 10)
      printf(1, "%d.00%d\t", cpu_seconds, cpu_milli);
    else if(cpu_milli < 100)
      printf(1, "%d.0%d\t", cpu_seconds, cpu_milli);
    else
      printf(1, "%d.%d\t", cpu_seconds, cpu_milli);
    printf(1, "%s\t", table[i].state);
    printf(1, "%d\n", table[i].size);
  }
  free(table);
  exit();
}
#endif 
