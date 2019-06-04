#ifdef CS333_P2 
#include "types.h"
#include "user.h"
#include "date.h" 

int 
main(int argc, char *argv[])
{
  int milli, seconds;  
  int start_time = uptime();
  int proc_id = fork();
  
  if(proc_id < 0)
  {
    exit(); //fork function returned a -1 
  }
  if(proc_id == 0)
  {
    exec(argv[1], argv+1); 
    exit();
  }
  else if(proc_id > 0)
  { 
    wait();
    int end_time = uptime();
    int total_time = end_time - start_time; 
    seconds = total_time / 1000;
    milli = total_time % 1000; 
    if(milli < 10)
    {
      printf(1, "%s ran in %d.00%d seconds\n", argv[1], seconds, milli);
    }
    else if(milli < 100)
    {
      printf(1, "%s ran in %d.0%d seconds\n", argv[1], seconds, milli);
    }
    else
    {
      printf(1, "%s ran in %d.%d seconds\n", argv[1], seconds, milli); 
    }
    exit();
  }
}
#endif 
