/* This file is part of UProf.
 *
 * Copyright © 2006 OpenedHand
 * Copyright © 2008, 2009, 2010 Robert Bragg
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

#define USE_RDTSC

#if !defined(__i386__) && !defined(__x86_64__)
#undef USE_RDTSC
#endif
//#undef USE_RDTSC

#include <uprof.h>

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <glib.h>
#include <errno.h>
#include <string.h>
#include <glib/gprintf.h>
#ifndef USE_RDTSC
#include <time.h>
#endif
#include <unistd.h>

/* #define UPROF_DEBUG     1 */

#ifdef UPROF_DEBUG
#define DBG_PRINTF(fmt, args...)              \
  do                                          \
    {                                         \
      printf ("[%s] " fmt, __FILE__, ##args); \
    }                                         \
  while (0)
#else
#define DBG_PRINTF(fmt, args...) do { } while (0)
#endif
#define REPORT_COLUMN0_WIDTH 40

#define UPROF_OBJECT_STATE(X) ((UProfObjectState *)X)

typedef void (*UProfContextCallback) (UProfContext *context,
                                      gpointer user_data);

typedef struct _UProfPrintReportRecord
{
  int    height;
  GList *entries;
  void  *data; /* timer / counter */
} UProfPrintReportRecord;

#if 0
typedef enum
{
  UPROF_ALIGNMENT_LEFT,
  UPROF_ALIGNMENT_RIGHT,
  UPROF_ALIGNMENT_CENTER
} UProfAlignment;
#endif

typedef struct _UProfPrintReportEntry
{
  int            width;
  int            height;
#if 0
  UProfAlignment alignment;
#endif
  gboolean       is_percentage;
  float          percentage;
  char         **lines;
} UProfPrintReportEntry;

typedef struct _UProfTimerAttribute
{
  char *name;
  UProfReportTimersAttributeCallback callback;
  void *user_data;
} UProfTimerAttribute;

typedef struct _UProfCounterAttribute
{
  char *name;
  UProfReportCountersAttributeCallback callback;
  void *user_data;
} UProfCounterAttribute;

struct _UProfContext
{
  guint  ref;

  char	*name;

  GList *links;

  GList	*counters;
  GList	*timers;

  int disabled;

  gboolean resolved;
  GList *root_timers;

  GList *report_messages;
};

typedef struct _UProfObjectLocation
{
  char  *filename;
  long   line;
  char  *function;
} UProfObjectLocation;

struct _UProfReport
{
  int ref;
  GList *contexts;
  GList *context_callbacks;

  GList *timer_attributes;
  GList *counter_attributes;

  int max_timer_name_size;
};

typedef struct _RDTSCVal
{
  union {
      struct {
          uint32_t low;
          uint32_t hi;
      } split;
      guint64 full_value;
  } u;
} RDTSCVal;

static guint64 system_counter_hz;
static GList *_uprof_all_contexts;
#ifndef USE_RDTSC
static clockid_t clockid;
#endif

UProfContext *mainloop_context = NULL;

void
uprof_init_real (void)
{
  static gboolean initialized = FALSE;

  if (initialized)
    return;

#ifndef USE_RDTSC
  int ret;
  struct timespec ts;

  ret = clock_getcpuclockid(0, &clockid);
  if (ret == ENOENT)
    {
      g_warning ("Using the CPU clock will be unreliable on this system if "
                 "you don't assure processor affinity");
    }
  else if (ret != 0)
    {
      const char *str = strerror (errno);
      g_warning ("Failed to get CPU clock ID: %s", str);
    }

  if (clock_gettime (CLOCK_PROCESS_CPUTIME_ID, &ts) == -1)
    {
      const char *str = strerror (errno);
      g_warning ("Failed to query CLOCK_PROCESS_CPUTIME_ID clock: %s", str);
    }
#endif

  mainloop_context = uprof_context_new ("Mainloop context");
  initialized = TRUE;
}

void
uprof_init (int *argc, char ***argv)
{
  uprof_init_real ();
}

static gboolean
pre_parse_hook (GOptionContext  *context,
                GOptionGroup    *group,
                gpointer         data,
                GError         **error)
{
  return TRUE;
}

static gboolean
post_parse_hook (GOptionContext  *context,
                 GOptionGroup    *group,
                 gpointer         data,
                 GError         **error)
{
  uprof_init_real ();
  return TRUE;
}

static GOptionEntry uprof_args[] = {
  { NULL, },
};

GOptionGroup *
uprof_get_option_group (void)
{
  GOptionGroup *group;

  group = g_option_group_new ("uprof",
                              "UProf Options",
                              "Show UProf Options",
                              NULL,
                              NULL);

  g_option_group_set_parse_hooks (group, pre_parse_hook, post_parse_hook);
  g_option_group_add_entries (group, uprof_args);
#if 0
  g_option_group_set_translation_domain (group, GETTEXT_PACKAGE);
#endif

  return group;
}

static void
uprof_calibrate_system_counter (void)
{
  guint64 time0, time1 = 0;
  guint64 diff;

  if (system_counter_hz)
    return;

  /* Determine the frequency of our time source */
  DBG_PRINTF ("Determining the frequency of the system counter\n");

  while (1)
    {
      struct timespec delay;
      time0 = uprof_get_system_counter ();
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
          time1 = uprof_get_system_counter ();
          break;
        }
    }

  /* Note: by saving hz into a global variable, processes that involve
   * a number of components with individual uprof profiling contexts don't
   * all need to incur a callibration delay. */

  g_assert (time1 > time0);
  diff = time1 - time0;
  system_counter_hz = diff * 4;

  DBG_PRINTF ("time0: %" G_GUINT64_FORMAT "\n", time0);
  DBG_PRINTF (" <sleep for 1/4 second>\n");
  DBG_PRINTF ("time1: %" G_GUINT64_FORMAT "\n", time1);
  DBG_PRINTF ("Diff over 1/4 second: %" G_GUINT64_FORMAT "\n", diff);
  DBG_PRINTF ("System Counter HZ: %" G_GUINT64_FORMAT "\n", system_counter_hz);
}

guint64
uprof_get_system_counter (void)
{
#ifdef USE_RDTSC
  RDTSCVal rdtsc;

  /* XXX:
   * Consider that on some multi processor machines it may be necessary to set
   * processor affinity.
   *
   * For Core 2 Duo, or hyper threaded systems, as I understand it the
   * rdtsc is driven by the system bus, and so the value is synchronized
   * across logical cores so we don't need to worry about affinity.
   *
   * Also since rdtsc is driven by the system bus, unlike the "Non-Sleep Clock
   * Ticks" counter it isn't affected by the processor going into power saving
   * states, which may happen as the processor goes idle waiting for IO.
   */
  asm ("rdtsc; movl %%edx,%0; movl %%eax,%1"
       : "=r" (rdtsc.u.split.hi), "=r" (rdtsc.u.split.low)
       : /* input */
       : "%edx", "%eax");

  /* g_print ("counter = %" G_GUINT64_FORMAT "\n",
              (guint64)rdtsc.u.full_value); */
  return rdtsc.u.full_value;
#else
  struct timespec ts;
  guint64 ret;

#if 0
  /* XXX: Seems very unreliable compared to simply using rdtsc asm () as above
   */
  clock_gettime (clockid, &ts);
#else
  clock_gettime (CLOCK_MONOTONIC, &ts);
#endif

  ret = ts.tv_sec;
  ret *= 1000000000;
  ret += ts.tv_nsec;

  /* g_print ("counter = %" G_GUINT64_FORMAT "\n", (guint64)ret); */

  return ret;
#endif
}

guint64
uprof_get_system_counter_hz (void)
{
  uprof_calibrate_system_counter ();
  return system_counter_hz;
}

UProfContext *
uprof_context_new (const char *name)
{
  UProfContext *context = g_new0 (UProfContext, 1);
  context->ref = 1;

  context->name = g_strdup (name);

  _uprof_all_contexts = g_list_prepend (_uprof_all_contexts, context);
  return context;
}

UProfContext *
uprof_context_ref (UProfContext *context)
{
  context->ref++;
  return context;
}

void
free_locations (GList *locations)
{
  GList *l;
  for (l = locations; l != NULL; l = l->next)
    {
      UProfObjectLocation *location = l->data;
      g_free (location->filename);
      g_free (location->function);
      g_slice_free (UProfObjectLocation, l->data);
    }
  g_list_free (locations);
}

void
free_object_state_members (UProfObjectState *object)
{
  g_free (object->name);
  g_free (object->description);
  free_locations (object->locations);
}

void
uprof_context_unref (UProfContext *context)
{
  context->ref--;
  if (!context->ref)
    {
      GList *l;
      g_free (context->name);

      for (l = context->counters; l != NULL; l = l->next)
        {
          free_object_state_members (l->data);
          g_slice_free (UProfCounterState, l->data);
        }
      g_list_free (context->counters);

      for (l = context->timers; l != NULL; l = l->next)
        {
          UProfTimerState *timer = l->data;
          free_object_state_members (l->data);
          if (timer->parent && timer->parent_name)
            g_free (timer->parent_name);
          g_slice_free (UProfTimerState, l->data);
        }
      g_list_free (context->timers);

      for (l = context->report_messages; l != NULL; l = l->next)
        g_free (l->data);
      g_list_free (context->report_messages);

      for (l = context->links; l != NULL; l = l->next)
        uprof_context_unref (l->data);
      g_list_free (context->links);

      _uprof_all_contexts = g_list_remove (_uprof_all_contexts, context);
      g_free (context);
    }
}

UProfContext *
uprof_get_mainloop_context (void)
{
  return mainloop_context;
}

/* A counter or timer may be accessed from multiple places in source
 * code so we support tracking a list of locations for each uprof
 * object. Note: we don't currently separate statistics for each
 * location, though that might be worth doing. */
GList *
add_location (GList         *locations,
              const char    *filename,
              unsigned long  line,
              const char    *function)
{
  GList *l;
  UProfObjectLocation *location;

  for (l = locations; l != NULL; l = l->next)
    {
      location = l->data;
      if (strcmp (location->filename, filename) == 0
          && location->line == line
          && strcmp (location->function, function) == 0)
        return locations;
    }
  location = g_slice_alloc (sizeof (UProfObjectLocation));
  location->filename = g_strdup (filename);
  location->line = line;
  location->function = g_strdup (function);
  return g_list_prepend (locations, location);
}

UProfObjectState *
find_uprof_object_state (GList *objects, const char *name)
{
  GList *l;
  for (l = objects; l != NULL; l = l->next)
    if (strcmp (((UProfObjectState *)l->data)->name, name) == 0)
      return l->data;
  return NULL;
}

UProfTimerResult *
uprof_context_get_timer_result (UProfContext *context, const char *name)
{
  return (UProfTimerResult *)find_uprof_object_state (context->timers, name);
}

UProfCounterResult *
uprof_context_get_counter_result (UProfContext *context, const char *name)
{
  return (UProfCounterResult *)find_uprof_object_state (context->counters,
                                                        name);
}

typedef struct
{
  GList *seen_contexts;
  UProfContextCallback callback;
  gpointer user_data;
} ForContextAndLinksState;

static void
_for_context_and_links_recursive (UProfContext *context,
                                  ForContextAndLinksState *state)
{
  GList *l;

  if (g_list_find (state->seen_contexts, context))
    return;

  state->callback (context, state->user_data);

  state->seen_contexts = g_list_prepend (state->seen_contexts, context);

  for (l = context->links; l; l = l->next)
    _for_context_and_links_recursive (l->data, state);
}

/* This recursively traverses all the contexts linked to the given
 * context ignoring duplicates. */
static void
for_context_and_links_recursive (UProfContext *context,
                                 UProfContextCallback callback,
                                 gpointer user_data)
{
  ForContextAndLinksState state;

  state.seen_contexts = NULL;
  state.callback = callback;
  state.user_data = user_data;

  _for_context_and_links_recursive (context, &state);

  g_list_free (state.seen_contexts);
}

static void
dirty_resolved_state_cb (UProfContext *context,
                         gpointer user_data)
{
  context->resolved = FALSE;
}

/* If we add timers or counters or link or unlink a
 * context, then we need to scrap any resolved hierarchies
 * between timers etc. */
static void
_uprof_context_dirty_resolved_state (UProfContext *context)
{
  for_context_and_links_recursive (context,
                                   dirty_resolved_state_cb,
                                   NULL);
}

void
uprof_context_add_counter (UProfContext *context, UProfCounter *counter)
{
  /* We check if we have actually seen this counter before; it might be that
   * it belongs to a dynamic shared object that has been reloaded */
  UProfCounterState *state =
    uprof_context_get_counter_result (context, counter->name);

  /* If we have seen this counter before see if it is being added from a
   * new location and track that location if so.
   */
  if (G_UNLIKELY (state))
    {
      UProfObjectState *object = (UProfObjectState *)state;
      object->locations = add_location (object->locations,
                                        counter->filename,
                                        counter->line,
                                        counter->function);
    }
  else
    {
      UProfObjectState *object;
      state = g_slice_alloc0 (sizeof (UProfCounterState));
      state->disabled = context->disabled;
      object = (UProfObjectState *)state;
      object->context = context;
      object->name = g_strdup (counter->name);
      object->description = g_strdup (counter->description);
      object->locations = add_location (NULL,
                                        counter->filename,
                                        counter->line,
                                        counter->function);
      context->counters = g_list_prepend (context->counters, state);
    }
  counter->state = state;
  _uprof_context_dirty_resolved_state (context);
}

void
uprof_context_add_timer (UProfContext *context, UProfTimer *timer)
{
  /* We check if we have actually seen this timer before; it might be that
   * it belongs to a dynamic shared object that has been reloaded */
  UProfTimerState *state =
    uprof_context_get_timer_result (context, timer->name);

  /* If we have seen this timer before see if it is being added from a
   * new location and track that location if so.
   */
  if (G_UNLIKELY (state))
    {
      UProfObjectState *object = (UProfObjectState *)state;
      object->locations = add_location (object->locations,
                                        timer->filename,
                                        timer->line,
                                        timer->function);
    }
  else
    {
      UProfObjectState *object;
      state = g_slice_alloc0 (sizeof (UProfTimerState));
      state->disabled = context->disabled;
      object = (UProfObjectState *)state;
      object->context = context;
      object->name = g_strdup (timer->name);
      object->description = g_strdup (timer->description);
      object->locations = add_location (NULL,
                                        timer->filename,
                                        timer->line,
                                        timer->function);
      if (timer->parent_name)
        state->parent_name = g_strdup (timer->parent_name);
      context->timers = g_list_prepend (context->timers, state);
    }
  timer->state = state;
  _uprof_context_dirty_resolved_state (context);
}

void
uprof_context_link (UProfContext *context, UProfContext *other)
{
  if (!g_list_find (context->links, other))
    {
      context->links = g_list_prepend (context->links,
                                       uprof_context_ref (other));

      _uprof_context_dirty_resolved_state (context);
    }
}

void
uprof_context_unlink (UProfContext *context, UProfContext *other)
{
  GList *l = g_list_find (context->links, other);
  if (l)
    {
      uprof_context_unref (other);
      context->links = g_list_delete_link (context->links, l);
      _uprof_context_dirty_resolved_state (context);
    }
}

typedef struct
{
#ifdef DEBUG_TIMER_HEIRARACHY
  int indent;
#endif
  UProfTimerState *parent;
  GList *children;
} FindChildrenState;

static void
find_timer_children_of_parent_cb (UProfContext *context,
                                  gpointer user_data)
{
  FindChildrenState *state = user_data;
  GList *l;
  GList *children = NULL;

  for (l = context->timers; l != NULL; l = l->next)
    {
      UProfTimerState *timer = l->data;
      if (timer->parent_name &&
          strcmp (timer->parent_name, state->parent->object.name) == 0)
        children = g_list_prepend (children, timer);
    }

#ifdef DEBUG_TIMER_HEIRARACHY
  for (l = children; l; l = l->next)
    {
      UProfTimerState *timer = l->data;
      g_print ("%*smatch: timer=\"%s\" context=\"%s\"\n",
               state->indent, "",
               timer->object.name, context->name);
    }
  state->indent += 2;
#endif

  state->children = g_list_concat (state->children, children);
}

static GList *
find_timer_children (UProfContext *context, UProfTimerState *parent)
{
  FindChildrenState state;

#ifdef DEBUG_TIMER_HEIRARACHY
  GList *l2;
  g_print (" find_timer_children (context = %s, parent = %s):\n",
           context->name, parent->object.name);

  state.indent = 0;
#endif

  state.parent = parent;
  state.children = NULL;

  /* NB: links allow users to combine the timers and counters from one
   * context into another, so when searching for the children of any
   * given timer we also need to search any linked contexts... */
  for_context_and_links_recursive (context,
                                   find_timer_children_of_parent_cb,
                                   &state);

  return state.children;
}

/*
 * Timer parents are declared using a string to name parents; this function
 * takes the names and resolves them to actual UProfTimer structures.
 */
static void
resolve_timer_heirachy (UProfContext    *context,
                        UProfTimerState *timer,
                        UProfTimerState *parent)
{
  GList *l;

#ifdef DEBUG_TIMER_HEIRACHY
  g_print ("resolve_timer_heirachy (context = %s, timer = %s, parent = %s)\n",
           context->name,
           ((UProfObjectState *)timer)->name,
           parent ? ((UProfObjectState *)parent)->name : "NULL");
#endif

  timer->parent = parent;
  timer->children = find_timer_children (context, timer);

#ifdef DEBUG_TIMER_HEIRACHY
  g_print ("resolved children of %s:\n", timer->object.name);
  for (l = timer->children; l != NULL; l = l->next)
    g_print ("  name = %s\n", ((UProfObjectState *)l->data)->name);
#endif

  for (l = timer->children; l != NULL; l = l->next)
    resolve_timer_heirachy (context, (UProfTimerState *)l->data, timer);
}

static const char *bars[] = {
    " ",
    "▏",
    "▎",
    "▍",
    "▌",
    "▋",
    "▊",
    "▉",
    "█"
};

/* Note: This returns a bar 45 characters wide for 100% */
char *
get_percentage_bar (float percent)
{
  GString *bar = g_string_new ("");
  int bar_len = 3.6 * percent;
  int bar_char_len = bar_len / 8;
  int i;

  if (bar_len % 8)
    bar_char_len++;

  for (i = bar_len; i >= 8; i -= 8)
    g_string_append_printf (bar, "%s", bars[8]);
  if (i)
    g_string_append_printf (bar, "%s", bars[i]);

  return g_string_free (bar, FALSE);
}

void
uprof_print_percentage_bar (float percent)
{
  char *percentage_bar = get_percentage_bar (percent);

  g_print ("%s", percentage_bar);
  g_free (percentage_bar);
}

static int
utf8_width (char *utf8_string)
{
  glong n_chars;
  gunichar *ucs4_string = g_utf8_to_ucs4_fast (utf8_string, -1, &n_chars);
  int i;
  int len = 0;

  for (i = 0; i < n_chars; i++)
    {
      if (g_unichar_iswide (ucs4_string[i]))
        len += 2;
      else if (g_unichar_iszerowidth (ucs4_string[i]))
        continue;
      else
        len++;
    }

  g_free (ucs4_string);

  return len;
}

static int
get_name_size_for_timer_and_children (UProfTimerResult *timer,
                                      int indent_level)
{
  int max_name_size =
    (indent_level * 2) + utf8_width (UPROF_OBJECT_STATE (timer)->name);
  GList *l;

  for (l = timer->children; l; l = l->next)
    {
      UProfTimerResult *child = l->data;
      int child_name_size =
        get_name_size_for_timer_and_children (child, indent_level + 1);
      if (child_name_size > max_name_size)
        max_name_size = child_name_size;
    }

  return max_name_size;
}

/* XXX: Only used to support deprecated API */
static void
print_timer_and_children (UProfReport                   *report,
                          UProfTimerResult              *timer,
                          UProfTimerResultPrintCallback  callback,
                          int                            indent_level,
                          gpointer                       data)
{
  char             *extra_fields;
  guint             extra_fields_width = 0;
  UProfTimerResult *root;
  GList            *children;
  GList            *l;
  float             percent;
  int               indent;

  if (callback)
    {
      extra_fields = callback (timer, &extra_fields_width, data);
      if (!extra_fields)
        return;
    }
  else
    extra_fields = g_strdup ("");

  /* percentages are reported relative to the root timer */
  root = uprof_timer_result_get_root (timer);

  indent = indent_level * 2; /* 2 spaces per indent level */

  percent = ((float)timer->total / (float)root->total) * 100.0;
  g_print (" %*s%-*s%-10.2f  %*s %7.3f%% ",
           indent,
           "",
           report->max_timer_name_size + 1 - indent,
           timer->object.name,
           ((float)timer->total / uprof_get_system_counter_hz()) * 1000.0,
           extra_fields_width,
           extra_fields,
           percent);

  uprof_print_percentage_bar (percent);
  g_print ("\n");

  children = uprof_timer_result_get_children (timer);
  children = g_list_sort_with_data (children,
                                    UPROF_TIMER_SORT_TIME_INC,
                                    NULL);
  for (l = children; l != NULL; l = l->next)
    {
      UProfTimerState *child = l->data;
      if (child->count == 0)
        continue;

      print_timer_and_children (report,
                                child,
                                callback,
                                indent_level + 1,
                                data);
    }
  g_list_free (children);

  g_free (extra_fields);
}

/* XXX: Deprecated API */
void
uprof_timer_result_print_and_children (UProfTimerResult              *timer,
                                       UProfTimerResultPrintCallback  callback,
                                       gpointer                       data)
{
  char *extra_titles = NULL;
  guint extra_titles_width = 0;
  UProfReport *report;

  if (callback)
    extra_titles = callback (NULL, &extra_titles_width, data);
  if (!extra_titles)
    extra_titles = g_strdup ("");

  report = g_new0 (UProfReport, 1);
  report->max_timer_name_size =
    get_name_size_for_timer_and_children (timer, 0);

  g_print (" %-*s%s %*s %s\n",
           report->max_timer_name_size + 1,
           "Name",
           "Total msecs",
           extra_titles_width,
           extra_titles,
           "Percent");

  g_free (extra_titles);

  print_timer_and_children (report, timer, callback, 0, data);
  g_free (report);
}

static void
copy_timers_list_cb (UProfContext *context, gpointer user_data)
{
  GList **all_timers = user_data;
  GList *context_timers;
#ifdef DEBUG_TIMER_HEIRACHY
  GList *l;
#endif

  context_timers = g_list_copy (context->timers);
  *all_timers = g_list_concat (*all_timers, context_timers);

#ifdef DEBUG_TIMER_HEIRACHY
  g_print (" all %s timers:\n", context->name);
  for (l = *all_timers; l != NULL; l = l->next)
    g_print ("  timer->name: %s\n", ((UProfObjectState *)l->data)->name);
#endif

  return;
}

void
uprof_context_foreach_timer (UProfContext            *context,
                             GCompareDataFunc         sort_compare_func,
                             UProfTimerResultCallback callback,
                             gpointer                 data)
{
  GList *l;
  GList *timers;
  GList *all_timers = NULL;

  g_return_if_fail (context != NULL);
  g_return_if_fail (callback != NULL);

#ifdef DEBUG_TIMER_HEIRACHY
  g_print ("uprof_context_foreach_timer (context = %s):\n",
           context->name);
#endif

  /* If the context has been linked with other contexts, then we want
   * a flat list of timers we can sort...
   *
   * XXX: If for example there are 3 contexts A, B and C where A and B
   * are linked and then C is linked to A and B then we are careful
   * not to duplicate the timers of C twice.
   */
  /* XXX: may want a dirty flag mechanism to avoid repeating this
   * too often! */
  if (context->links)
    {
      for_context_and_links_recursive (context,
                                       copy_timers_list_cb,
                                       &all_timers);
      timers = all_timers;
    }
  else
    timers = context->timers;

#ifdef DEBUG_TIMER_HEIRACHY
  g_print (" all combined timers:\n");
  for (l = timers; l != NULL; l = l->next)
    g_print ("  timer->name: %s\n", ((UProfObjectState *)l->data)->name);
#endif

  if (sort_compare_func)
    timers = g_list_sort_with_data (timers, sort_compare_func, data);
  for (l = timers; l != NULL; l = l->next)
    callback (l->data, data);

  g_list_free (all_timers);
}

static void
copy_counters_list_cb (UProfContext *context, gpointer user_data)
{
  GList **all_counters = user_data;
  GList *context_counters = g_list_copy (context->counters);

  *all_counters = g_list_concat (*all_counters, context_counters);

  return;
}

void
uprof_context_foreach_counter (UProfContext              *context,
                               GCompareDataFunc           sort_compare_func,
                               UProfCounterResultCallback callback,
                               gpointer                   data)
{
  GList *l;
  GList *counters;
  GList *all_counters = NULL;

  g_return_if_fail (context != NULL);
  g_return_if_fail (callback != NULL);

  /* If the context has been linked with other contexts, then we want
   * a flat list of counters we can sort... */
  /* XXX: may want a dirty flag mechanism to avoid repeating this
   * too often! */
  if (context->links)
    {
      for_context_and_links_recursive (context,
                                       copy_counters_list_cb,
                                       &all_counters);
      counters = all_counters;
    }
  else
    counters = context->counters;

  if (sort_compare_func)
    counters = g_list_sort_with_data (counters, sort_compare_func, data);
  for (l = counters; l != NULL; l = l->next)
    callback (l->data, data);

  g_list_free (all_counters);
}

UProfContext *
uprof_find_context (const char *name)
{
  GList *l;
  for (l = _uprof_all_contexts; l != NULL; l = l->next)
    {
      UProfContext *context = l->data;
      if (strcmp (context->name, name) == 0)
        return context;
    }
  return NULL;
}

gint
_uprof_timer_compare_total_times (UProfTimerState *a,
                                  UProfTimerState *b,
                                  gpointer data)
{
  if (a->total > b->total)
    return -1;
  else if (a->total < b->total)
    return 1;
  else
    return 0;
}

gint
_uprof_timer_compare_start_count (UProfTimerState *a,
                                  UProfTimerState *b,
                                  gpointer data)
{
  if (a->count > b->count)
    return -1;
  else if (a->count < b->count)
    return 1;
  else
    return 0;
}

gint
_uprof_counter_compare_count (UProfCounterState *a,
                              UProfCounterState *b,
                              gpointer data)
{
  if (a->count > b->count)
    return -1;
  else if (a->count < b->count)
    return 1;
  else
    return 0;
}

const char *
uprof_timer_result_get_name (UProfTimerResult *timer)
{
  return timer->object.name;
}

const char *
uprof_timer_result_get_description (UProfTimerResult *timer)
{
  return timer->object.description;
}

float
uprof_timer_result_get_total_msecs (UProfTimerResult *timer)
{
  return ((float)timer->total / uprof_get_system_counter_hz()) * 1000.0;
}

gulong
uprof_timer_result_get_start_count (UProfTimerResult *timer)
{
  return timer->count;
}

UProfTimerResult *
uprof_timer_result_get_parent (UProfTimerResult *timer)
{
  return timer->parent;
}

UProfTimerResult *
uprof_timer_result_get_root (UProfTimerResult *timer)
{
  while (timer->parent)
    timer = timer->parent;

  return timer;
}

GList *
uprof_timer_result_get_children (UProfTimerResult *timer)
{
  return g_list_copy (timer->children);
}

UProfContext *
uprof_timer_result_get_context (UProfTimerResult *timer)
{
  return timer->object.context;
}

const char *
uprof_counter_result_get_name (UProfCounterResult *counter)
{
  return counter->object.name;
}

gulong
uprof_counter_result_get_count (UProfCounterResult *counter)
{
  return counter->count;
}

UProfContext *
uprof_counter_result_get_context (UProfCounterResult *counter)
{
  return counter->object.context;
}

static void
resolve_timer_heirachy_cb (UProfTimerResult *timer, void *data)
{
  UProfContext *context = data;
  if (timer->parent_name == NULL)
    {
      resolve_timer_heirachy (context, timer, NULL);
      context->root_timers = g_list_prepend (context->root_timers, timer);
    }
}

#ifdef DEBUG_TIMER_HEIRARACHY
static void
debug_print_timer_recursive (UProfTimerResult *timer, int indent)
{
  UProfContext *context = uprof_timer_result_get_context (timer);
  GList *l;

  g_print ("%*scontext = %s, timer = %s, parent_name = %s, "
           "parent = %s\n",
           2 * indent, "",
           context->name,
           timer->object.name,
           timer->parent_name,
           timer->parent ? timer->parent->object.name : "NULL");
  for (l = timer->children; l != NULL; l = l->next)
    {
      UProfTimerResult *child = l->data;
      g_print ("%*schild = %s\n",
               2 * indent, "",
               child->object.name);
    }

  indent++;
  for (l = timer->children; l != NULL; l = l->next)
    debug_print_timer_recursive (l->data, indent);
}

static void
debug_print_timer_heirachy_cb (UProfTimerResult *timer, void *data)
{
  if (timer->parent == NULL)
    debug_print_timer_recursive (timer, 0);
}
#endif

static void
uprof_context_resolve_timer_heirachy (UProfContext *context)
{
#ifdef DEBUG_TIMER_HEIRARACHY
  GList *l;
#endif

  if (context->resolved)
    return;

  g_assert (context->root_timers == NULL);

  /* Use the parent names of timers to resolve the actual parent
   * child hierarchy (fill in timer .children, and .parent members) */
  uprof_context_foreach_timer (context,
                               NULL, /* no need to sort */
                               resolve_timer_heirachy_cb,
                               context);

#ifdef DEBUG_TIMER_HEIRARACHY
  g_print ("resolved root_timers:\n");
  for (l = context->root_timers; l != NULL; l = l->next)
    g_print (" name = %s\n", ((UProfObjectState *)l->data)->name);

  uprof_context_foreach_timer (context,
                               NULL, /* no need to sort */
                               debug_print_timer_heirachy_cb,
                               context);
#endif

  context->resolved = TRUE;
}

GList *
uprof_context_get_root_timer_results (UProfContext *context)
{
  return g_list_copy (context->root_timers);
}

static void
_uprof_suspend_single_context (UProfContext *context)
{
  GList *l;

  context->disabled++;

  for (l = context->timers; l != NULL; l = l->next)
    {
      UProfTimerState *timer = l->data;

      timer->disabled++;
      if (timer->start && timer->disabled == 1)
        {
          timer->partial_duration +=
            uprof_get_system_counter () - timer->start;
        }
    }

  for (l = context->counters; l != NULL; l = l->next)
    ((UProfCounterState *)l->data)->disabled++;
}

void
uprof_context_suspend (UProfContext *context)
{
  for_context_and_links_recursive (
                           context,
                           (UProfContextCallback)_uprof_suspend_single_context,
                           NULL);
}

static void
_uprof_resume_single_context (UProfContext *context)
{
  GList *l;

  context->disabled--;

  for (l = context->timers; l != NULL; l = l->next)
    {
      UProfTimerState *timer = l->data;
      timer->disabled--;
      /* Handle resuming of timers... */
      /* NB: any accumulated ->partial_duration will be added to the total
       * when the timer is stopped. */
      if (timer->start && timer->disabled == 0)
        timer->start = uprof_get_system_counter (); \
    }

  for (l = context->counters; l != NULL; l = l->next)
    ((UProfCounterState *)l->data)->disabled--;
}

void
uprof_context_resume (UProfContext *context)
{
  for_context_and_links_recursive (
                           context,
                           (UProfContextCallback)_uprof_resume_single_context,
                           NULL);
}

void
uprof_context_add_report_message (UProfContext *context,
                                  const char *format, ...)
{
  va_list ap;
  char *report_message;

  va_start (ap, format);
  report_message = g_strdup_vprintf (format, ap);
  va_end (ap);

  context->report_messages = g_list_prepend (context->report_messages,
                                             report_message);
}

GList *
uprof_context_get_messages (UProfContext *context)
{
  GList *l = g_list_copy (context->report_messages);
  for (; l != NULL; l = l->next)
    l->data = g_strdup (l->data);
  return l;
}

UProfReport *
uprof_report_new (const char *name)
{
  UProfReport *report = g_slice_new0 (UProfReport);
  report->ref = 1;
  return report;
}

UProfReport *
uprof_report_ref (UProfReport *report)
{
  report->ref++;
  return report;
}

void
uprof_report_unref (UProfReport *report)
{
  report->ref--;
  if (!report->ref)
    {
      g_slice_free (UProfReport, report);
    }
}

void
uprof_report_add_context (UProfReport *report,
                          UProfContext *context)
{
  report->contexts = g_list_prepend (report->contexts, context);
}

void
uprof_report_add_context_callback (UProfReport *report,
                                   UProfReportContextCallback callback)
{
  report->context_callbacks =
    g_list_prepend (report->context_callbacks, callback);
}

void
uprof_report_remove_context_callback (UProfReport *report,
                                      UProfReportContextCallback callback)
{
  report->context_callbacks =
    g_list_remove (report->context_callbacks, callback);
}

void
uprof_report_add_counters_attribute (UProfReport *report,
                                     const char *attribute_name,
                                     UProfReportCountersAttributeCallback callback,
                                     void *user_data)
{
  UProfCounterAttribute *attribute = g_slice_new (UProfCounterAttribute);
  attribute->name = g_strdup (attribute_name);
  attribute->callback = callback;
  attribute->user_data = user_data;
  report->counter_attributes =
    g_list_append (report->counter_attributes, attribute);
}

void
uprof_report_remove_counters_attribute (UProfReport *report,
                                        const char *attribute_name)
{
  GList *l;

  for (l = report->counter_attributes; l; l = l->next)
    {
      UProfCounterAttribute *attribute = l->data;
      if (strcmp (attribute->name, attribute_name) == 0)
        {
          g_free (attribute->name);
          g_slice_free (UProfCounterAttribute, attribute);
          report->counter_attributes =
            g_list_delete_link (report->counter_attributes, l);
          return;
        }
    }
}

void
uprof_report_add_timers_attribute (UProfReport *report,
                                   const char *attribute_name,
                                   UProfReportTimersAttributeCallback callback,
                                   void *user_data)
{
  UProfTimerAttribute *attribute = g_slice_new (UProfTimerAttribute);
  attribute->name = g_strdup (attribute_name);
  attribute->callback = callback;
  attribute->user_data = user_data;
  report->timer_attributes =
    g_list_append (report->timer_attributes, attribute);
}

void
uprof_report_remove_timers_attribute (UProfReport *report,
                                      const char *attribute_name)
{
  GList *l;

  for (l = report->timer_attributes; l; l = l->next)
    {
      UProfTimerAttribute *attribute = l->data;
      if (strcmp (attribute->name, attribute_name) == 0)
        {
          g_free (attribute->name);
          g_slice_free (UProfTimerAttribute, attribute);
          report->timer_attributes =
            g_list_delete_link (report->timer_attributes, l);
          return;
        }
    }
}

typedef struct
{
  UProfReport *report;
  GList **records;
} AddCounterState;

static void
add_counter_record (UProfCounterResult *counter,
                    gpointer            data)
{
  AddCounterState        *state = data;
  UProfReport            *report = state->report;
  GList                 **records = state->records;
  UProfPrintReportRecord *record;
  UProfPrintReportEntry  *entry;
  char                   *lines;
  GList                  *l;

  record = g_slice_new0 (UProfPrintReportRecord);

  entry = g_slice_new0 (UProfPrintReportEntry);
  lines = g_strdup_printf ("%s", uprof_counter_result_get_name (counter));
  entry->lines = g_strsplit (lines, "\n", 0);
  g_free (lines);
  record->entries = g_list_prepend (record->entries, entry);

  entry = g_slice_new0 (UProfPrintReportEntry);
  lines = g_strdup_printf ("%lu", uprof_counter_result_get_count (counter));
  entry->lines = g_strsplit (lines, "\n", 0);
  g_free (lines);
  record->entries = g_list_prepend (record->entries, entry);

  for (l = report->counter_attributes; l; l = l->next)
    {
      UProfCounterAttribute *attribute = l->data;
      entry = g_slice_new0 (UProfPrintReportEntry);
      lines = attribute->callback (counter, attribute->user_data);
      entry->lines = g_strsplit (lines, "\n", 0);
      g_free (lines);
      record->entries = g_list_prepend (record->entries, entry);
    }

  record->entries = g_list_reverse (record->entries);
  record->data = counter;

  *records = g_list_prepend (*records, record);
}

static void
prepare_print_records_for_counters (UProfReport *report,
                                    UProfContext *context,
                                    GList **records)
{
  AddCounterState state;
  state.report = report;
  state.records = records;
  uprof_context_foreach_counter (context,
                                 UPROF_COUNTER_SORT_COUNT_INC,
                                 add_counter_record,
                                 &state);
}

static void
print_record_entries (UProfPrintReportRecord *record)
{
  int line;
  GList *l;

  for (line = 0; line < record->height; line++)
    {
      for (l = record->entries; l; l = l->next)
        {
          UProfPrintReportEntry *entry = l->data;
          if (entry->height < (line + 1))
            g_print ("%-*s ", entry->width, "");
          else
            {
              /* XXX: printf field widths are byte not character oriented
               * so we have to consider multi-byte utf8 characters. */
              int field_width =
                strlen (entry->lines[line]) +
                (entry->width - utf8_width (entry->lines[line]));
              g_printf ("%-*s ", field_width, entry->lines[line]);
            }
        }
      g_print ("\n");
    }
}

static void
print_counter_record (UProfPrintReportRecord *record)
{
  UProfCounterResult *counter = record->data;

  if (counter && counter->count == 0)
    return;

  print_record_entries (record);

  /* XXX: We currently pass a NULL timer for the first record which contains
   * the column titles... */
  if (counter == NULL)
    g_print ("\n");
}

static void
default_print_counter_records (GList *records)
{
  GList *l;

  for (l = records; l; l = l->next)
    print_counter_record (l->data);
}

static void
prepare_print_records_for_timer_and_children (UProfReport *report,
                                              UProfTimerResult *timer,
                                              int indent_level,
                                              GList **records)
{
  UProfPrintReportRecord *record;
  UProfPrintReportEntry  *entry;
  int                     indent;
  char                   *lines;
  UProfTimerResult       *root;
  float                   percent;
  GList                  *l;
  GList                  *children;

  record = g_slice_new0 (UProfPrintReportRecord);

  indent = indent_level * 2; /* 2 spaces per indent level */
  entry = g_slice_new0 (UProfPrintReportEntry);
  lines = g_strdup_printf ("%*s%-*s",
                           indent, "",
                           report->max_timer_name_size + 1 - indent,
                           timer->object.name);
  entry->lines = g_strsplit (lines, "\n", 0);
  g_free (lines);
  record->entries = g_list_prepend (record->entries, entry);

  entry = g_slice_new0 (UProfPrintReportEntry);
  lines = g_strdup_printf ("%-.2f",
                           ((float)timer->total /
                            uprof_get_system_counter_hz()) * 1000.0);
  entry->lines = g_strsplit (lines, "\n", 0);
  g_free (lines);
  record->entries = g_list_prepend (record->entries, entry);

  for (l = report->timer_attributes; l; l = l->next)
    {
      UProfTimerAttribute *attribute = l->data;
      entry = g_slice_new0 (UProfPrintReportEntry);
      lines = attribute->callback (timer, attribute->user_data);
      entry->lines = g_strsplit (lines, "\n", 0);
      g_free (lines);
      record->entries = g_list_prepend (record->entries, entry);
    }

  /* percentages are reported relative to the root timer */
  root = uprof_timer_result_get_root (timer);
  percent = ((float)timer->total / (float)root->total) * 100.0;

  entry = g_slice_new0 (UProfPrintReportEntry);
  lines = g_strdup_printf ("%7.3f%%", percent);
  entry->lines = g_strsplit (lines, "\n", 0);
  g_free (lines);
  entry->is_percentage = TRUE;
  entry->percentage = percent;
  record->entries = g_list_prepend (record->entries, entry);

  entry = g_slice_new0 (UProfPrintReportEntry);
  lines = get_percentage_bar (percent);
  entry->lines = g_strsplit (lines, "\n", 0);
  g_free (lines);
  record->entries = g_list_prepend (record->entries, entry);

  record->entries = g_list_reverse (record->entries);
  record->data = timer;

  *records = g_list_prepend (*records, record);

  children = uprof_timer_result_get_children (timer);
  children = g_list_sort_with_data (children,
                                    UPROF_TIMER_SORT_TIME_INC,
                                    NULL);
  for (l = children; l; l = l->next)
    {
      UProfTimerState *child = l->data;

      prepare_print_records_for_timer_and_children (report,
                                                    child,
                                                    indent_level + 1,
                                                    records);
    }
  g_list_free (children);
}

static void
size_record_entries (GList *records)
{
  UProfPrintReportRecord *record;
  int entries_per_record;
  int *column_width;
  GList *l;

  if (!records)
    return;

  /* All records should have the same number of entries */
  record = records->data;
  entries_per_record = g_list_length (record->entries);
  column_width = g_new0 (int, entries_per_record);

  for (l = records; l; l = l->next)
    {
      GList *l2;
      int i;

      record = l->data;

      for (l2 = record->entries, i = 0; l2; l2 = l2->next, i++)
        {
          UProfPrintReportEntry *entry = l2->data;
          int line;

          g_assert (i < entries_per_record);

          for (line = 0; entry->lines[line]; line++)
            entry->width = MAX (entry->width, utf8_width (entry->lines[line]));

          column_width[i] = MAX (column_width[i], entry->width);

          entry->height = line;
          record->height = MAX (record->height, entry->height);
        }
    }

  for (l = records; l; l = l->next)
    {
      GList *l2;
      int i;

      record = l->data;

      for (l2 = record->entries, i = 0; l2; l2 = l2->next, i++)
        {
          UProfPrintReportEntry *entry = l2->data;
          entry->width = column_width[i];
        }
    }

  g_free (column_width);
}

static void
print_timer_record (UProfPrintReportRecord *record)
{
  UProfTimerResult  *timer = record->data;

  if (timer && timer->count == 0)
    return;

  print_record_entries (record);

  /* XXX: We currently pass a NULL timer for the first record which contains
   * the column titles... */
  if (timer == NULL)
    g_print ("\n");
}

static void
default_print_timer_records (GList *records)
{
  GList *l;

  for (l = records; l; l = l->next)
    print_timer_record (l->data);
}

static void
free_print_records (GList *records)
{
  GList *l;
  for (l = records; l; l = l->next)
    {
      GList *l2;
      UProfPrintReportRecord *record = l->data;
      for (l2 = record->entries; l2; l2 = l2->next)
        {
          UProfPrintReportEntry *entry = l2->data;
          g_strfreev (entry->lines);
          g_slice_free (UProfPrintReportEntry, entry);
        }
      g_slice_free (UProfPrintReportRecord, l->data);
    }
  g_list_free (records);
}

static void
default_report_context (UProfReport *report, UProfContext *context)
{
  GList *records = NULL;
  UProfPrintReportRecord *record;
  UProfPrintReportEntry *entry;
  GList *root_timers;
  GList *l;

  g_print ("context: %s\n", context->name);
  g_print ("\n");
  g_print ("counters:\n");

  record = g_slice_new0 (UProfPrintReportRecord);

  entry = g_slice_new0 (UProfPrintReportEntry);
  entry->lines = g_strsplit ("Name", "\n", 0);
  record->entries = g_list_prepend (record->entries, entry);

  entry = g_slice_new0 (UProfPrintReportEntry);
  entry->lines = g_strsplit ("Total", "\n", 0);
  record->entries = g_list_prepend (record->entries, entry);

  for (l = report->counter_attributes; l; l = l->next)
    {
      UProfCounterAttribute *attribute = l->data;

      entry = g_slice_new0 (UProfPrintReportEntry);
      entry->lines = g_strsplit (attribute->name, "\n", 0);
      record->entries = g_list_prepend (record->entries, entry);
    }

  record->entries = g_list_reverse (record->entries);
  record->data = NULL;

  records = g_list_prepend (records, record);

  prepare_print_records_for_counters (report, context, &records);

  records = g_list_reverse (records);

  size_record_entries (records);

  default_print_counter_records (records);

  free_print_records (records);

  g_print ("\n");
  g_print ("timers:\n");
  g_assert (context->resolved);

  root_timers = uprof_context_get_root_timer_results (context);
  for (l = root_timers; l != NULL; l = l->next)
    {
      UProfTimerResult *timer = l->data;
      GList *l2;

      records = NULL;

      record = g_slice_new0 (UProfPrintReportRecord);

      entry = g_slice_new0 (UProfPrintReportEntry);
      entry->lines = g_strsplit ("Name", "\n", 0);
      record->entries = g_list_prepend (record->entries, entry);

      entry = g_slice_new0 (UProfPrintReportEntry);
      entry->lines = g_strsplit ("Total\nmsecs", "\n", 0);
      record->entries = g_list_prepend (record->entries, entry);

      for (l2 = report->timer_attributes; l2; l2 = l2->next)
        {
          UProfTimerAttribute *attribute = l2->data;

          entry = g_slice_new0 (UProfPrintReportEntry);
          entry->lines = g_strsplit (attribute->name, "\n", 0);
          record->entries = g_list_prepend (record->entries, entry);
        }

      entry = g_slice_new0 (UProfPrintReportEntry);
      entry->lines = g_strsplit ("Percent", "\n", 0);
      record->entries = g_list_prepend (record->entries, entry);

      /* We need a dummy entry for the last percentage bar column because
       * every record is expected to have the same number of entries. */
      entry = g_slice_new0 (UProfPrintReportEntry);
      entry->lines = g_strsplit ("", "\n", 0);
      record->entries = g_list_prepend (record->entries, entry);

      record->entries = g_list_reverse (record->entries);
      record->data = NULL;

      records = g_list_prepend (records, record);

      prepare_print_records_for_timer_and_children (report, timer,
                                                    0, &records);

      records = g_list_reverse (records);

      size_record_entries (records);

      default_print_timer_records (records);
      free_print_records (records);
    }
  g_list_free (root_timers);
}

static void
report_context (UProfReport *report, UProfContext *context)
{
  GList *l;

  uprof_context_resolve_timer_heirachy (context);

  if (!report->context_callbacks)
    {
      default_report_context (report, context);
      return;
    }

  for (l = report->context_callbacks; l != NULL; l = l->next)
    {
      UProfReportContextCallback callback = l->data;
      callback (report, context);
    }
}

void
uprof_report_print (UProfReport *report)
{
  GList *l;

  for (l = report->contexts; l; l = l->next)
    report_context (report, l->data);
}

/* Should be easy to add new probes to code, and ideally not need to
 * modify the profile reporting code in most cases.
 *
 * Should support simple counters
 * Should support summing the total time spent between start/stop delimiters
 * Should support hierarchical timers; such that more fine grained timers
 * may be be easily enabled/disabled.
 *
 * Reports should support XML
 * Implement a simple clutter app for visualising the stats.
 */

