/*
 * This file is part of UProf.
 *
 * Copyright © 2008 Robert Bragg
 *
 * UProf is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * UProf is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with UProf.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <uprof.h>

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <glib.h>
#include <errno.h>

#define DEBUG g_print

struct _UProfContext
{
  guint ref;

  UProfCounter  *counters;
  UProfTimer	*timers;
};

typedef struct _RDTSCVal
{
  union {
      struct {
	  uint32_t low;
	  uint32_t hi;
      } split;
      unsigned long long full_value;
  } u;
} RDTSCVal;

unsigned long long system_counter_hz;


unsigned long long
uprof_query_system_counter (void)
{
#if __i386__
  RDTSCVal rdtsc;

  /* XXX:
   * Consider that on some multi processor machines it may be necissary to set
   * processor affinity.
   *
   * For Core 2 Duo, or hyper threaded systems, then as I understand it the
   * rdtsc is driven by the system bus, and so the value is synchronized
   * accross logical cores so we don't need to worry about affinity.
   *
   * Also since rdtsc is driven by the system bus, unlike the "Non-Sleep Clock
   * Ticks" counter it isn't affected by the processor going into power saving
   * states, which may happen as the processor goes idle waiting for IO.
   */

  asm ("rdtsc; movl %%edx,%0; movl %%eax,%1"
       : "=r" (rdtsc.u.split.hi), "=r" (rdtsc.u.split.low)
       : /* input */
       : "%edx", "%eax");

  return rdtsc.u.full_value;
#else
  /* XXX: implement gettimeofday() based fallback */
#error "System currently not supported by uprof"
#endif
}

void
uprof_init (int *argc, char ***argv)
{
  unsigned long long time0, time1 = 0;
  struct timespec delay;
  unsigned long long diff;

  if (system_counter_hz)
    return;

  /* Determine the frequency of our time source */
  DEBUG ("Determining the frequency of the system counter\n");

  while (1)
    {
      time0 = uprof_query_system_counter ();
      delay.tv_sec = 0;
      delay.tv_nsec = 1000000000/4;
      if (nanosleep (&delay, NULL) == -1)
	{
	  if (errno == EINTR)
	    continue;
	  else
	    g_critical ("Failed to init uprof, due to nanosleep error\n");
	}
      else
	{
	  time1 = uprof_query_system_counter ();
	  break;
	}
    }

  /* Note: by saving hz into a global variable, processes that involve
   * a number of components with individual uprof profiling contexts don't
   * all need to incur a callibration delay. */

  diff = time1 - time0;
  system_counter_hz = diff * 4;

  DEBUG ("time0: %llu\n", time0);
  DEBUG (" <sleep for 1/4 second>\n");
  DEBUG ("time1: %llu\n", time1);
  DEBUG ("Diff over 1/4 second: %llu\n", diff);
  DEBUG ("System Counter HZ: %llu\n", system_counter_hz);
}

unsigned long long
uprof_get_system_counter_hz (void)
{
  return system_counter_hz;
}

UProfContext *
uprof_context_new (const char *name)
{
  UProfContext *context = g_new0 (UProfContext, 1);
  context->ref = 1;
  return context;
}

void
uprof_context_ref (UProfContext *context)
{
  context->ref++;
}

void
uprof_context_unref (UProfContext *context)
{
  context->ref--;
  if (!context->ref)
    g_free (context);
}

void
uprof_context_declare_counters (UProfContext *context, UProfCounter *counters)
{
  int i;

  context->counters = counters;
  for (i = 0; counters[i].name != NULL; i++)
    {
      counters[i].count = 0;
    }
}

void
uprof_context_declare_timers (UProfContext *context, UProfTimer *timers)
{
  int i;

  context->timers = timers;
  for (i = 0; timers[i].name != NULL; i++)
    {
      timers[i].count = 0;
      timers[i].start = 0;
      timers[i].total = 0;
    }
}

void
uprof_context_output_report (UProfContext *context)
{

}


/* Should be easy to add new probes to code, and ideally not need to
 * modify the profile reporting code in most cases.
 *
 * Should support simple counters
 * Should support summing the total time spent between start/stop delimiters
 * Should support heirachical timers; such that more fine grained timers
 * may be be easily enabled/disabled.
 *
 * Reports should support XML
 * Implement a simple clutter app for visualising the stats.
 */

