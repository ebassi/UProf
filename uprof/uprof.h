/* This file is part of UProf.
 *
 * Copyright © 2008, 2009 Robert Bragg
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301  USA
 */

#ifndef _UPROF_H_
#define _UPROF_H_

#include <uprof-private.h>

#include <glib.h>

G_BEGIN_DECLS

typedef struct _UProfCounter
{
  /** Application defined name for counter */
  const char *name;

  /** Application defined description for counter */
  const char *description;

  /** Application private data */
  unsigned long priv;

  /** private */
  UProfCounterState *state;

  const char *filename;
  unsigned long line;
  const char *function;

} UProfCounter;

typedef struct _UProfTimer
{
  /** Application defined name for timer */
  const char *name;

  /** Application defined description for timer */
  const char *description;

  /** Application defined parent */
  const char *parent_name;

  /** Application private data */
  unsigned long priv;

  /** private */
  UProfTimerState *state;

  const char *filename;
  unsigned long line;
  const char *function;

} UProfTimer;

typedef struct _UProfCounterState UProfCounterResult;
typedef struct _UProfTimerState   UProfTimerResult;

/**
 * uprof_init:
 * FIXME
 */
void
uprof_init (int *argc, char ***argv);

/**
 * uprof_get_system_counter:
 * FIXME
 */
unsigned long long
uprof_get_system_counter (void);

/**
 * uprof_get_system_counter_hz:
 * FIXME
 */
unsigned long long
uprof_get_system_counter_hz (void);

/**
 * uprof_context_new:
 * @name: The top most name to categorize your profiling results
 *
 * This creates a new profiling context with a given name. After creating
 * a context you should declare your timers and counters, and once
 * you have accumulated data you can print the results using
 * uprof_get_report()
 */
UProfContext *
uprof_context_new (const char *name);

/**
 * uprof_context_ref:
 * @context: A UProf context
 *
 * Take a reference on a uprof context.
 */
UProfContext *
uprof_context_ref (UProfContext *context);

/**
 * uprof_context_imref:
 * @context: A UProf context
 *
 * Release a reference on a uprof context. When the reference count reaches
 * zero the context is freed.
 */
void
uprof_context_unref (UProfContext *context);

/**
 * uprof_find_context:
 * @name: Find an existing uprof context by name
 *
 * This function looks for a context by name which - for example - may
 * then be passed to uprof_context_link.
 */
UProfContext *
uprof_find_context (const char *name);

/**
 * uprof_context_add_counter:
 * @context: A UProf context
 * FIXME
 */
void
uprof_context_add_counter (UProfContext *context, UProfCounter *counter);

/**
 * uprof_context_add_timer:
 * @context: A UProf context
 * FIXME
 */
void
uprof_context_add_timer (UProfContext *context, UProfTimer *timer);

/**
 * uprof_context_link:
 * @context: A UProf context
 * @other: A UProf context whos timers and counters you want to be made
 *         available to @context.
 *
 * By linking your context with another, then the timers and counters of
 * the @other context will become in a way part of the first @context - at
 * least as far as reporting is concerned. For example calling
 * uprof_context_foreach_counter() would iterate all the counters
 * of other contexts linked to the given context.
 *
 * This can be usefull if you are profiling a library that itself loads a
 * DSO, but you can't export a context symbol from the library to the DSO
 * because it happens that this DSO is also used by other libraries or
 * applications which can't provide that symbol.
 *
 * The intention is to create a context that is owned by the DSO, and when we
 * end up loading the DSO in the library we are profiling we can link that
 * context into the real context.
 *
 * An example of this is profiling a DRI driver which can be loaded by X
 * clients for direct rendering, or also by the X server for indirect
 * rendering. If you try and export a context variable from the GL driver to
 * the DRI driver there will end up being an unresolved symbol when the X
 * server tries to load the driver.
 */
void
uprof_context_link (UProfContext *context, UProfContext *other);

/**
 * @context: A UProf context
 * @other: A UProf context whos timers and counters were previously made
 *         available to @context.
 */
void
uprof_context_unlink (UProfContext *context, UProfContext *other);

/**
 * uprof_context_output_report:
 * @context: A UProf context
 *
 * Generates a report of the accumulated timer and counter data associated with
 * the given context.
 */
void
uprof_context_output_report (UProfContext *context);

/************
 * Counters
 ************/

#define UPROF_COUNTER(COUNTER_SYMBOL, NAME, DESCRIPTION, PRIV) \
  UProfCounter COUNTER_SYMBOL = { \
    .name = NAME, \
    .description = DESCRIPTION, \
    .priv = (unsigned long)(PRIV), \
    .state = NULL \
  }

/**
 * UPROF_STATIC_COUNTER:
 * @COUNTER_SYMBOL: The name of the C symbol to declare
 * @DESCRIPTION: A string describing what the timer represents
 * @PRIV: Optional private data (unsigned long) which you can get to if you are
 *	  generating a very customized report. For example you might put
 *	  application specific flags here that affect reporting.
 *
 * This can be used to declare a new static counter structure
 */
#define UPROF_STATIC_COUNTER(COUNTER_SYMBOL, NAME, DESCRIPTION, PRIV) \
  static UPROF_COUNTER(COUNTER_SYMBOL, NAME, DESCRIPTION, PRIV)

#define INIT_UNSEEN_COUNTER(CONTEXT, COUNTER_SYMBOL) \
  do { \
    (COUNTER_SYMBOL).filename = __FILE__; \
    (COUNTER_SYMBOL).line = __LINE__; \
    (COUNTER_SYMBOL).function = __FUNCTION__; \
    uprof_context_add_counter (CONTEXT, &(COUNTER_SYMBOL)); \
  } while (0)

#define UPROF_COUNTER_INC(CONTEXT, COUNTER_SYMBOL) \
  do { \
    if (!(COUNTER_SYMBOL).state) \
      INIT_UNSEEN_COUNTER (CONTEXT, COUNTER_SYMBOL); \
    if ((COUNTER_SYMBOL).state->disabled) \
      break; \
    (COUNTER_SYMBOL).state->count++; \
  } while (0)

#define UPROF_COUNTER_DEC(CONTEXT, COUNTER_SYMBOL) \
  do { \
    if (!(COUNTER_SYMBOL).state) \
      INIT_UNSEEN_COUNTER (CONTEXT, COUNTER_SYMBOL) \
    if ((COUNTER_SYMBOL).state->disabled) \
      break; \
    (COUNTER_SYMBOL).state->count--; \
  } while (0)

#define UPROF_COUNTER_ZERO(CONTEXT, COUNTER_SYMBOL) \
  do { \
    if (!(COUNTER_SYMBOL).state) \
      INIT_UNSEEN_COUNTER (CONTEXT, COUNTER_SYMBOL) \
    if ((COUNTER_SYMBOL).state->disabled) \
      break; \
    (COUNTER_SYMBOL).state->count = 0; \
  } while (0)




/************
 * Timers
 ************/

#define UPROF_TIMER(TIMER_SYMBOL, PARENT, NAME, DESCRIPTION, PRIV) \
  UProfTimer TIMER_SYMBOL = { \
    .name = NAME, \
    .description = DESCRIPTION, \
    .parent_name = PARENT, \
    .priv = (unsigned long)(PRIV), \
    .state = NULL \
  }

/**
 * UPROF_STATIC_TIMER:
 * @TIMER_SYMBOL: The name of the C symbol to declare
 * @PARENT: The name of a parent timer (it should really be the name given to
 *	    the parent, not the C symbol name for the parent)
 * @DESCRIPTION: A string describing what the timer represents
 * @PRIV: Optional private data (unsigned long) which you can get to if you are
 *	  generating a very customized report. For example you might put
 *	  application specific flags here that affect reporting.
 * This can be used to declare a new static timer structure
 */

#define UPROF_STATIC_TIMER(TIMER_SYMBOL, PARENT, NAME, DESCRIPTION, PRIV) \
  static UPROF_TIMER(TIMER_SYMBOL, PARENT, NAME, DESCRIPTION, PRIV)

#define INIT_UNSEEN_TIMER(CONTEXT, TIMER_SYMBOL) \
  do { \
    (TIMER_SYMBOL).filename = __FILE__; \
    (TIMER_SYMBOL).line = __LINE__; \
    (TIMER_SYMBOL).function = __FUNCTION__; \
    uprof_context_add_timer (CONTEXT, &(TIMER_SYMBOL)); \
  } while (0)

#ifdef UPROF_DEBUG
#define DEBUG_CHECK_FOR_RECURSION(CONTEXT, TIMER_SYMBOL) \
  do { \
    if ((TIMER_SYMBOL).state->start) \
      { \
        g_warning ("Recursive starting of timer (%s) unsuported!", \
                   (TIMER_SYMBOL).name); \
      } \
  } while (0)
#else
#define DEBUG_CHECK_FOR_RECURSION(CONTEXT, TIMER_SYMBOL)
#endif

#define UPROF_TIMER_START(CONTEXT, TIMER_SYMBOL) \
  do { \
    if (!(TIMER_SYMBOL).state) \
      INIT_UNSEEN_TIMER (CONTEXT, TIMER_SYMBOL); \
    if (!(TIMER_SYMBOL).state->disabled) \
      { \
        DEBUG_CHECK_FOR_RECURSION (CONTEXT, TIMER_SYMBOL); \
        (TIMER_SYMBOL).state->start = uprof_get_system_counter (); \
      } \
  } while (0)

#define UPROF_RECURSIVE_TIMER_START(CONTEXT, TIMER_SYMBOL) \
  do { \
    if (!(TIMER_SYMBOL).state) \
      INIT_UNSEEN_TIMER (CONTEXT, TIMER_SYMBOL); \
    if (!(TIMER_SYMBOL).state->disabled) \
      { \
        if ((TIMER_SYMBOL).state->recursion++ == 0) \
          { \
            (TIMER_SYMBOL).state->start = uprof_get_system_counter (); \
          } \
      } \
  } while (0)

/* XXX: We should add debug profiling to also verify that the timer isn't already
 * running. */

/* XXX: We should consider system counter wrap around issues */

#ifdef UPROF_DEBUG
#define DEBUG_CHECK_TIMER_WAS_STARTED(CONTEXT, TIMER_SYMBOL) \
  do { \
    if (!(TIMER_SYMBOL).state->start) \
      { \
        g_warning ("Stopping an un-started timer! (%s)", \
                   (TIMER_SYMBOL).name); \
      } \
  } while (0)
#define DEBUG_ZERO_TIMER_START(CONTEXT, TIMER_SYMBOL) \
  do { \
    (TIMER_SYMBOL).state->start = 0; \
  } while (0)
#else
#define DEBUG_CHECK_TIMER_WAS_STARTED(CONTEXT, TIMER_SYMBOL)
#define DEBUG_ZERO_TIMER_START(CONTEXT, TIMER_SYMBOL)
#endif

#define UPROF_TIMER_STOP(CONTEXT, TIMER_SYMBOL) \
  do { \
    if (!(TIMER_SYMBOL).state->disabled) \
      { \
        unsigned long long duration; \
        (TIMER_SYMBOL).state->count++; \
        DEBUG_CHECK_TIMER_WAS_STARTED (CONTEXT, TIMER_SYMBOL); \
        duration = uprof_get_system_counter() - (TIMER_SYMBOL).state->start; \
        if ((duration < (TIMER_SYMBOL).state->fastest)) \
          (TIMER_SYMBOL).state->fastest = duration; \
        else if ((duration > (TIMER_SYMBOL).state->slowest)) \
          (TIMER_SYMBOL).state->slowest = duration; \
        (TIMER_SYMBOL).state->total += duration; \
        DEBUG_ZERO_TIMER_START (CONTEXT, TIMER_SYMBOL); \
      } \
  } while (0)

#define UPROF_RECURSIVE_TIMER_STOP(CONTEXT, TIMER_SYMBOL) \
  do { \
    g_assert ((TIMER_SYMBOL).state->recursion > 0); \
    if (!(TIMER_SYMBOL).state->disabled && \
        (TIMER_SYMBOL).state->recursion-- == 1) \
      { \
        unsigned long long duration; \
        (TIMER_SYMBOL).state->count++; \
        DEBUG_CHECK_TIMER_WAS_STARTED (CONTEXT, TIMER_SYMBOL); \
        duration = uprof_get_system_counter() - (TIMER_SYMBOL).state->start; \
        if ((duration < (TIMER_SYMBOL).state->fastest)) \
          (TIMER_SYMBOL).state->fastest = duration; \
        else if ((duration > (TIMER_SYMBOL).state->slowest)) \
          (TIMER_SYMBOL).state->slowest = duration; \
        (TIMER_SYMBOL).state->total += duration; \
        DEBUG_ZERO_TIMER_START (CONTEXT, TIMER_SYMBOL); \
      } \
  } while (0)

/* TODO: */
#if 0
#define UPROF_TIMER_PAUSE()
#define UPROF_TIMER_CONTINUE()
#endif



/**
 * uprof_context_add_report_warning:
 * @context: A uprof context
 * @format: A printf style format string
 *
 * This function queues a message to be output when uprof generates its
 * final report.
 */
void
uprof_context_add_report_message (UProfContext *context,
                                  const char *format, ...);

void
uprof_suspend_context (UProfContext *context);

void
uprof_resume_context (UProfContext *context);

/*
 * Support for reporting results:
 */

UProfReport *
uprof_report_new (const char *name);

UProfReport *
uprof_report_ref (UProfReport *report);

void
uprof_report_unref (UProfReport *report);

void
uprof_report_add_context (UProfReport *report,
                          UProfContext *context);

typedef void (*UProfReportContextCallback) (UProfReport *report,
                                            UProfContext *context);

void
uprof_report_add_context_callback (UProfReport *report,
                                   UProfReportContextCallback callback);

void
uprof_report_remove_context_callback (UProfReport *report,
                                      UProfReportContextCallback callback);

typedef char *(*UProfReportCountersAttributeCallback) (UProfCounterResult *result);
typedef char *(*UProfReportTimersAttributeCallback) (UProfTimerResult *result);

/**
 * uprof_context_add_counters_attribute:
 * @context: A UProf context
 * @attribute_name: The name of the attribute
 * @callback: A function called for each counter being reported
 *
 * Adds a custom attribute to reports of counter statistics. The attribute name
 * can be wrapped with newline characters and the callback functions will be
 * called once for each timer being reported via a call to
 * uprof_context_output_report()
 */
void
uprof_report_add_counters_attribute (UProfReport *report,
                                     const char *attribute_name,
                                     UProfReportCountersAttributeCallback callback,
                                     void *user_data);

/**
 * uprof_context_remove_counters_attribute:
 * @context: A UProf context
 * @id: The custom report column you want to remove
 *
 * Removes the custom counters @attribute from future reports generated
 * with uprof_context_output_report()
 */
void
uprof_report_remove_counters_attribute (UProfReport *report,
                                        const char *attribute_name);

/**
 * uprof_report_add_timers_attribute:
 * @context: A UProf context
 * @attribute_name: The name of the attribute
 * @callback: A function called for each timer being reported
 *
 * Adds a custom attribute to reports of timer statistics. The attribute name
 * can be wrapped with newline characters and the callback functions will be
 * called once for each timer being reported via a call to
 * uprof_context_output_report()
 */
void
uprof_report_add_timers_attribute (UProfReport *report,
                                   const char *attribute_name,
                                   UProfReportTimersAttributeCallback callback,
                                   void *user_data);

/**
 * uprof_report_remove_timers_attribute:
 * @context: A UProf context
 * @id: The custom report column you want to remove
 *
 * Removes the custom timers @attribute from future reports generated with
 * uprof_context_output_report()
 */
void
uprof_report_remove_timers_attribute (UProfReport *report,
                                      const char *attribute_name);

void
uprof_report_print (UProfReport *report);

GList *
uprof_context_get_root_timer_results (UProfContext *context);

gint
_uprof_timer_compare_total_times (UProfTimerState *a,
                                  UProfTimerState *b,
                                  gpointer data);
gint
_uprof_timer_compare_start_count (UProfTimerState *a,
                                  UProfTimerState *b,
                                  gpointer data);
#define UPROF_TIMER_SORT_TIME_INC \
  ((GCompareDataFunc)_uprof_timer_compare_total_times)
#define UPROF_TIMER_SORT_COUNT_INC \
  ((GCompareDataFunc)_uprof_timer_compare_start_count)

gint
_uprof_counter_compare_count (UProfCounterState *a,
                              UProfCounterState *b,
                              gpointer data);
#define UPROF_COUNTER_SORT_COUNT_INC \
  ((GCompareDataFunc)_uprof_counter_compare_count)


typedef void (*UProfTimerResultCallback) (UProfTimerResult *timer,
                                          gpointer          data);

void
uprof_context_foreach_timer (UProfContext            *context,
                             GCompareDataFunc         sort_compare_func,
                             UProfTimerResultCallback callback,
                             gpointer                 data);


typedef void (*UProfCounterResultCallback) (UProfCounterResult *counter,
                                            gpointer            data);

void
uprof_context_foreach_counter (UProfContext              *context,
                               GCompareDataFunc           sort_compare_func,
                               UProfCounterResultCallback callback,
                               gpointer                   data);

UProfTimerResult *
uprof_context_get_timer_result (UProfContext *context, const char *name);

const char *
uprof_timer_result_get_description (UProfTimerResult *timer);

float
uprof_timer_result_get_total_msecs (UProfTimerResult *timer);

gulong
uprof_timer_result_get_start_count (UProfTimerResult *timer);

UProfTimerResult *
uprof_timer_result_get_parent (UProfTimerResult *timer);

UProfTimerResult *
uprof_timer_result_get_root (UProfTimerResult *timer);

GList *
uprof_timer_result_get_children (UProfTimerResult *timer);

UProfContext *
uprof_timer_result_get_context (UProfTimerResult *timer);

const char *
uprof_counter_result_get_name (UProfCounterResult *counter);

gulong
uprof_counter_result_get_count (UProfCounterResult *counter);

UProfContext *
uprof_counter_result_get_context (UProfCounterResult *counter);

void
uprof_print_percentage_bar (float percent);

typedef char * (*UProfTimerResultPrintCallback) (UProfTimerState *timer,
                                                 guint           *fields_width,
                                                 gpointer         data);

void
uprof_timer_result_print_and_children (UProfTimerResult              *timer,
                                       UProfTimerResultPrintCallback  callback,
                                       gpointer                       data);

GList *
uprof_context_get_messages (UProfContext *context);

/* XXX: We should keep in mind the slightly thorny issues of handling shared
 * libraries, where various constants can dissapear as well as the timers
 * themselves. Since the UProf context is always on the heap, we can always
 * add some kind of uprof_context_internalize() mechanism later to ensure that
 * timer/counter statistics are never lost even though the underlying
 * timer structures may come and go. */

/* XXX: We might want the ability to declare "alias" timers so that we
 * can track multiple (filename, line, function) constants for essentially
 * the same timer. */

/* XXX: It would be nice if we could find a way to generalize how
 * relationships between timers and counters are made so we don't need
 * specialized reporting code to handle these use cases:
 *
 * - Having a frame_counter, and then other counters reported relative
 *   to the frame counter; such as averages of minor counters per frame.
 *
 * - Being able to report timer stats in relation to a counter (such as
 *   a frame counter). For example reporting the average time spent
 *   painting, per frame.
 */

/* XXX: static globals shouldn't be the only way to declare timers and
 * counters; exported globals or malloc'd variables might be handy too.
 */

G_END_DECLS

#endif /* _UPROF_H_ */

