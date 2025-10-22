/*
 * calendar-client-vdir.c: vdir-based calendar client implementation
 *
 * Copyright (C) 2025
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * This implementation reads calendars from vdir directories (directories
 * containing .ics files) as documented at:
 * https://vdirsyncer.pimutils.org/en/stable/vdir.html
 */

#include <config.h>

#include "calendar-client.h"
#include "calendar-client-vdir.h"

#include <libintl.h>
#include <string.h>
#include <gio/gio.h>
#define LIBICAL_GLIB_UNSTABLE_API
#include <libical-glib/libical-glib.h>
#include "system-timezone.h"

#undef CALENDAR_ENABLE_DEBUG
#include "calendar-debug.h"

#ifndef _
#define _(x) gettext(x)
#endif

#ifndef N_
#define N_(x) x
#endif

typedef struct _VdirSource VdirSource;

struct _VdirSource
{
  CalendarClientVdir *client;
  gchar          *path;
  gchar          *display_name;
  gchar          *color;
  GFileMonitor   *monitor;
  GHashTable     *events;  /* uid+rid -> CalendarEvent */
  guint           changed_signal_id;
};

struct _CalendarClientVdirPrivate
{
  GSettings  *settings;
  GSList     *sources;      /* List of VdirSource */
  
  ICalTimezone *zone;
  
  guint       day;
  guint       month;
  guint       year;
};

static void calendar_client_vdir_finalize     (GObject             *object);
static void calendar_client_vdir_set_property (GObject             *object,
					  guint                prop_id,
					  const GValue        *value,
					  GParamSpec          *pspec);
static void calendar_client_vdir_get_property (GObject             *object,
					  guint                prop_id,
					  GValue              *value,
					  GParamSpec          *pspec);

static void vdir_source_free (VdirSource *source);
static void vdir_source_load_events (VdirSource *source, CalendarClientVdir *client);
static void reload_sources (CalendarClientVdir *client);

enum
{
  PROP_O,
  PROP_DAY,
  PROP_MONTH,
  PROP_YEAR
};

enum
{
  APPOINTMENTS_CHANGED,
  TASKS_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE_WITH_PRIVATE (CalendarClientVdir, calendar_client_vdir, G_TYPE_OBJECT)

static void
calendar_client_vdir_class_init (CalendarClientVdirClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize     = calendar_client_vdir_finalize;
  gobject_class->set_property = calendar_client_vdir_set_property;
  gobject_class->get_property = calendar_client_vdir_get_property;

  g_object_class_install_property (gobject_class,
				   PROP_DAY,
				   g_param_spec_uint ("day",
						      "Day",
						      "The currently monitored day between 1 and 31 (0 denotes unset)",
						      0, G_MAXUINT, 0,
						      G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
				   PROP_MONTH,
				   g_param_spec_uint ("month",
						      "Month",
						      "The currently monitored month between 0 and 11",
						      0, G_MAXUINT, 0,
						      G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
				   PROP_YEAR,
				   g_param_spec_uint ("year",
						      "Year",
						      "The currently monitored year",
						      0, G_MAXUINT, 0,
						      G_PARAM_READWRITE));

  signals[APPOINTMENTS_CHANGED] =
    g_signal_new ("appointments-changed",
		  G_TYPE_FROM_CLASS (gobject_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (CalendarClientVdirClass, appointments_changed),
		  NULL,
		  NULL,
		  NULL,
		  G_TYPE_NONE,
		  0);

  signals[TASKS_CHANGED] =
    g_signal_new ("tasks-changed",
		  G_TYPE_FROM_CLASS (gobject_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (CalendarClientVdirClass, tasks_changed),
		  NULL,
		  NULL,
		  NULL,
		  G_TYPE_NONE,
		  0);
}

static ICalTimezone *
get_system_timezone (void)
{
  SystemTimezone *systz;
  const gchar *tz_name;
  ICalTimezone *zone;
  
  systz = system_timezone_new ();
  tz_name = system_timezone_get (systz);
  
  if (tz_name && *tz_name)
    zone = i_cal_timezone_get_builtin_timezone (tz_name);
  else
    zone = i_cal_timezone_get_utc_timezone ();
  
  g_object_unref (systz);
  return zone;
}

static void
on_directory_changed (GFileMonitor     *monitor,
                      GFile            *file,
                      GFile            *other_file,
                      GFileMonitorEvent event_type,
                      VdirSource       *source)
{
  /* Reload events when directory changes */
  if (event_type == G_FILE_MONITOR_EVENT_CREATED ||
      event_type == G_FILE_MONITOR_EVENT_DELETED ||
      event_type == G_FILE_MONITOR_EVENT_CHANGED)
    {
      vdir_source_load_events (source, source->client);
      g_signal_emit (source->client, source->changed_signal_id, 0);
    }
}

static void
calendar_client_vdir_init (CalendarClientVdir *client)
{
  client->priv = calendar_client_vdir_get_instance_private (client);
  
  client->priv->sources = NULL;
  client->priv->zone = get_system_timezone ();
  client->priv->day = G_MAXUINT;
  client->priv->month = G_MAXUINT;
  client->priv->year = G_MAXUINT;
}

static void
calendar_client_vdir_finalize (GObject *object)
{
  CalendarClientVdir *client = CALENDAR_CLIENT_VDIR (object);
  
  if (client->priv->settings)
    g_object_unref (client->priv->settings);
  client->priv->settings = NULL;
  
  g_slist_free_full (client->priv->sources, (GDestroyNotify) vdir_source_free);
  client->priv->sources = NULL;
  
  G_OBJECT_CLASS (calendar_client_vdir_parent_class)->finalize (object);
}

static void
calendar_client_vdir_set_property (GObject      *object,
			      guint         prop_id,
			      const GValue *value,
			      GParamSpec   *pspec)
{
  CalendarClientVdir *client = CALENDAR_CLIENT_VDIR (object);

  switch (prop_id)
    {
    case PROP_DAY:
      calendar_client_vdir_select_day (client, g_value_get_uint (value));
      break;
    case PROP_MONTH:
      calendar_client_vdir_select_month (client,
				    g_value_get_uint (value),
				    client->priv->year);
      break;
    case PROP_YEAR:
      calendar_client_vdir_select_month (client,
				    client->priv->month,
				    g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
calendar_client_vdir_get_property (GObject    *object,
			      guint       prop_id,
			      GValue     *value,
			      GParamSpec *pspec)
{
  CalendarClientVdir *client = CALENDAR_CLIENT_VDIR (object);

  switch (prop_id)
    {
    case PROP_DAY:
      g_value_set_uint (value, client->priv->day);
      break;
    case PROP_MONTH:
      g_value_set_uint (value, client->priv->month);
      break;
    case PROP_YEAR:
      g_value_set_uint (value, client->priv->year);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

CalendarClientVdir *
calendar_client_vdir_new (GSettings *settings)
{
  CalendarClientVdir *client = g_object_new (CALENDAR_TYPE_CLIENT, NULL);
  
  if (settings)
    client->priv->settings = g_object_ref (settings);
  
  reload_sources (client);
  
  return client;
}

static void
vdir_source_free (VdirSource *source)
{
  if (source->monitor)
    {
      g_file_monitor_cancel (source->monitor);
      g_object_unref (source->monitor);
    }
  
  if (source->events)
    g_hash_table_destroy (source->events);
  
  g_free (source->path);
  g_free (source->display_name);
  g_free (source->color);
  g_free (source);
}

static time_t
make_time_for_day_begin (int day, int month, int year)
{
  struct tm localtime_tm = { 0, };

  localtime_tm.tm_mday  = day;
  localtime_tm.tm_mon   = month;
  localtime_tm.tm_year  = year - 1900;
  localtime_tm.tm_isdst = -1;

  return mktime (&localtime_tm);
}

static gchar *
get_component_uid_rid (ICalComponent *comp)
{
  const gchar *uid = i_cal_component_get_uid (comp);
  ICalProperty *prop;
  ICalTime *recur_id;
  gchar *rid = NULL;
  gchar *result;
  
  prop = i_cal_component_get_first_property (comp, I_CAL_RECURRENCEID_PROPERTY);
  if (prop)
    {
      recur_id = i_cal_property_get_recurrenceid (prop);
      if (recur_id && i_cal_time_is_valid_time (recur_id) && !i_cal_time_is_null_time (recur_id))
        rid = i_cal_time_as_ical_string (recur_id);
      
      g_clear_object (&recur_id);
      g_clear_object (&prop);
    }
  
  if (rid)
    {
      result = g_strdup_printf ("%s:%s", uid ? uid : "", rid);
      g_free (rid);
    }
  else
    result = g_strdup (uid ? uid : "");
  
  return result;
}

static time_t
icaltime_to_timet (ICalTime *itt, ICalTimezone *default_zone)
{
  ICalTimezone *zone = NULL;
  time_t result;
  
  if (!itt || !i_cal_time_is_valid_time (itt))
    return 0;
  
  if (i_cal_time_is_utc (itt))
    zone = i_cal_timezone_get_utc_timezone ();
  else if (i_cal_time_get_timezone (itt))
    zone = (ICalTimezone *) i_cal_time_get_timezone (itt);
  else
    zone = default_zone;
  
  result = i_cal_time_as_timet_with_zone (itt, zone);
  
  return result;
}

static CalendarEvent *
create_event_from_component (ICalComponent *comp, VdirSource *source, ICalTimezone *default_zone)
{
  ICalComponentKind kind = i_cal_component_isa (comp);
  CalendarEvent *event;
  ICalProperty *prop;
  ICalTime *dtstart, *dtend, *due, *completed;
  
  if (kind != I_CAL_VEVENT_COMPONENT && kind != I_CAL_VTODO_COMPONENT)
    return NULL;
  
  event = g_new0 (CalendarEvent, 1);
  
  if (kind == I_CAL_VEVENT_COMPONENT)
    {
      CalendarAppointment *appt = CALENDAR_APPOINTMENT (event);
      event->type = CALENDAR_EVENT_APPOINTMENT;
      
      appt->uid = g_strdup (i_cal_component_get_uid (comp));
      
      prop = i_cal_component_get_first_property (comp, I_CAL_RECURRENCEID_PROPERTY);
      if (prop)
        {
          ICalTime *rid = i_cal_property_get_recurrenceid (prop);
          if (rid && i_cal_time_is_valid_time (rid) && !i_cal_time_is_null_time (rid))
            appt->rid = i_cal_time_as_ical_string (rid);
          g_clear_object (&rid);
          g_clear_object (&prop);
        }
      
      appt->backend_name = g_strdup ("vdir");
      
      prop = i_cal_component_get_first_property (comp, I_CAL_SUMMARY_PROPERTY);
      if (prop)
        {
          appt->summary = g_strdup (i_cal_property_get_summary (prop));
          g_object_unref (prop);
        }
      
      prop = i_cal_component_get_first_property (comp, I_CAL_DESCRIPTION_PROPERTY);
      if (prop)
        {
          appt->description = g_strdup (i_cal_property_get_description (prop));
          g_object_unref (prop);
        }
      
      appt->color_string = g_strdup (source->color ? source->color : "#3465A4");
      
      dtstart = i_cal_component_get_dtstart (comp);
      appt->start_time = icaltime_to_timet (dtstart, default_zone);
      appt->is_all_day = dtstart ? i_cal_time_is_date (dtstart) : FALSE;
      g_clear_object (&dtstart);
      
      dtend = i_cal_component_get_dtend (comp);
      appt->end_time = icaltime_to_timet (dtend, default_zone);
      g_clear_object (&dtend);
      
      /* Create a single occurrence */
      CalendarOccurrence *occurrence = g_new0 (CalendarOccurrence, 1);
      occurrence->start_time = appt->start_time;
      occurrence->end_time = appt->end_time;
      appt->occurrences = g_slist_prepend (NULL, occurrence);
    }
  else /* VTODO */
    {
      CalendarTask *task = CALENDAR_TASK (event);
      event->type = CALENDAR_EVENT_TASK;
      
      task->uid = g_strdup (i_cal_component_get_uid (comp));
      
      prop = i_cal_component_get_first_property (comp, I_CAL_SUMMARY_PROPERTY);
      if (prop)
        {
          task->summary = g_strdup (i_cal_property_get_summary (prop));
          g_object_unref (prop);
        }
      
      prop = i_cal_component_get_first_property (comp, I_CAL_DESCRIPTION_PROPERTY);
      if (prop)
        {
          task->description = g_strdup (i_cal_property_get_description (prop));
          g_object_unref (prop);
        }
      
      task->color_string = g_strdup (source->color ? source->color : "#3465A4");
      
      dtstart = i_cal_component_get_dtstart (comp);
      task->start_time = icaltime_to_timet (dtstart, default_zone);
      g_clear_object (&dtstart);
      
      due = i_cal_component_get_due (comp);
      task->due_time = icaltime_to_timet (due, default_zone);
      g_clear_object (&due);
      
      prop = i_cal_component_get_first_property (comp, I_CAL_PERCENTCOMPLETE_PROPERTY);
      if (prop)
        {
          task->percent_complete = i_cal_property_get_percentcomplete (prop);
          g_object_unref (prop);
        }
      
      prop = i_cal_component_get_first_property (comp, I_CAL_COMPLETED_PROPERTY);
      if (prop)
        {
          completed = i_cal_property_get_completed (prop);
          task->completed_time = icaltime_to_timet (completed, default_zone);
          g_clear_object (&completed);
          g_object_unref (prop);
        }
      
      prop = i_cal_component_get_first_property (comp, I_CAL_PRIORITY_PROPERTY);
      if (prop)
        {
          task->priority = i_cal_property_get_priority (prop);
          g_object_unref (prop);
        }
      else
        task->priority = -1;
    }
  
  return event;
}

static void
expand_recurrence (ICalComponent *comp, VdirSource *source, CalendarClientVdir *client,
                   time_t range_start, time_t range_end)
{
  ICalProperty *rrule_prop;
  ICalRecurrence *rrule;
  ICalRecurIterator *iter;
  ICalTime *dtstart;
  ICalTime *next;
  gchar *uid_rid;
  time_t instance_time;
  int count = 0;
  const int MAX_INSTANCES = 1000;
  
  rrule_prop = i_cal_component_get_first_property (comp, I_CAL_RRULE_PROPERTY);
  if (!rrule_prop)
    return;
  
  rrule = i_cal_property_get_rrule (rrule_prop);
  dtstart = i_cal_component_get_dtstart (comp);
  
  if (!dtstart || !i_cal_time_is_valid_time (dtstart))
    {
      g_clear_object (&dtstart);
      g_clear_object (&rrule);
      g_object_unref (rrule_prop);
      return;
    }
  
  iter = i_cal_recur_iterator_new (rrule, dtstart);
  
  while ((next = i_cal_recur_iterator_next (iter)) != NULL && count < MAX_INSTANCES)
    {
      if (i_cal_time_is_null_time (next))
        {
          g_object_unref (next);
          break;
        }
      
      instance_time = icaltime_to_timet (next, client->priv->zone);
      
      if (instance_time >= range_end)
        {
          g_object_unref (next);
          break;
        }
      
      if (instance_time >= range_start)
        {
          /* Create an instance */
          CalendarEvent *event = create_event_from_component (comp, source, client->priv->zone);
          if (event && event->type == CALENDAR_EVENT_APPOINTMENT)
            {
              CalendarAppointment *appt = CALENDAR_APPOINTMENT (event);
              
              /* Update the times for this instance */
              time_t duration = appt->end_time - appt->start_time;
              appt->start_time = instance_time;
              appt->end_time = instance_time + duration;
              
              if (appt->occurrences)
                {
                  CalendarOccurrence *occ = appt->occurrences->data;
                  occ->start_time = appt->start_time;
                  occ->end_time = appt->end_time;
                }
              
              /* Store with unique key */
              uid_rid = g_strdup_printf ("%s:%ld", appt->uid, (long)instance_time);
              g_hash_table_replace (source->events, uid_rid, event);
            }
        }
      
      g_object_unref (next);
      count++;
    }
  
  g_object_unref (iter);
  g_clear_object (&dtstart);
  g_object_unref (rrule);
  g_object_unref (rrule_prop);
}

static void
vdir_source_load_events (VdirSource *source, CalendarClientVdir *client)
{
  GDir *dir;
  const gchar *filename;
  GError *error = NULL;
  time_t range_start, range_end;
  
  /* Clear existing events */
  if (source->events)
    g_hash_table_remove_all (source->events);
  else
    source->events = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           g_free, (GDestroyNotify) calendar_event_free);
  
  /* Calculate range */
  if (client->priv->month != G_MAXUINT && client->priv->year != G_MAXUINT)
    {
      range_start = make_time_for_day_begin (1, client->priv->month, client->priv->year);
      
      if (client->priv->month == 11)
        range_end = make_time_for_day_begin (1, 0, client->priv->year + 1);
      else
        range_end = make_time_for_day_begin (1, client->priv->month + 1, client->priv->year);
    }
  else
    {
      /* No range set, load nothing */
      return;
    }
  
  dir = g_dir_open (source->path, 0, &error);
  if (!dir)
    {
      if (error)
        {
          g_warning ("Failed to open vdir directory %s: %s", source->path, error->message);
          g_error_free (error);
        }
      return;
    }
  
  while ((filename = g_dir_read_name (dir)) != NULL)
    {
      gchar *filepath;
      ICalComponent *icalcomp, *subcomp;
      gchar *contents;
      gsize length;
      
      /* Only process .ics files */
      if (!g_str_has_suffix (filename, ".ics"))
        continue;
      
      filepath = g_build_filename (source->path, filename, NULL);
      
      if (!g_file_get_contents (filepath, &contents, &length, &error))
        {
          if (error)
            {
              g_warning ("Failed to read %s: %s", filepath, error->message);
              g_clear_error (&error);
            }
          g_free (filepath);
          continue;
        }
      
      icalcomp = i_cal_parser_parse_string (contents);
      g_free (contents);
      
      if (!icalcomp)
        {
          g_warning ("Failed to parse %s", filepath);
          g_free (filepath);
          continue;
        }
      
      /* Process all VEVENT and VTODO components */
      for (subcomp = i_cal_component_get_first_component (icalcomp, I_CAL_VEVENT_COMPONENT);
           subcomp;
           g_object_unref (subcomp), subcomp = i_cal_component_get_next_component (icalcomp, I_CAL_VEVENT_COMPONENT))
        {
          ICalProperty *rrule_prop;
          CalendarEvent *event;
          gchar *uid_rid;
          time_t start_time;
          ICalTime *dtstart;
          
          /* Check if this event is in range */
          dtstart = i_cal_component_get_dtstart (subcomp);
          start_time = icaltime_to_timet (dtstart, client->priv->zone);
          g_clear_object (&dtstart);
          
          /* Check for recurrence */
          rrule_prop = i_cal_component_get_first_property (subcomp, I_CAL_RRULE_PROPERTY);
          
          if (rrule_prop)
            {
              /* Expand recurring event */
              expand_recurrence (subcomp, source, client, range_start, range_end);
              g_object_unref (rrule_prop);
            }
          else if (start_time >= range_start && start_time < range_end)
            {
              /* Single event in range */
              event = create_event_from_component (subcomp, source, client->priv->zone);
              if (event)
                {
                  uid_rid = get_component_uid_rid (subcomp);
                  g_hash_table_replace (source->events, uid_rid, event);
                }
            }
        }
      
      /* Process tasks */
      for (subcomp = i_cal_component_get_first_component (icalcomp, I_CAL_VTODO_COMPONENT);
           subcomp;
           g_object_unref (subcomp), subcomp = i_cal_component_get_next_component (icalcomp, I_CAL_VTODO_COMPONENT))
        {
          CalendarEvent *event;
          gchar *uid_rid;
          
          event = create_event_from_component (subcomp, source, client->priv->zone);
          if (event)
            {
              uid_rid = get_component_uid_rid (subcomp);
              g_hash_table_replace (source->events, uid_rid, event);
            }
        }
      
      g_object_unref (icalcomp);
      g_free (filepath);
    }
  
  g_dir_close (dir);
}

static void
reload_sources (CalendarClientVdir *client)
{
  gchar **dirs;
  gint i;
  
  /* Clear existing sources */
  g_slist_free_full (client->priv->sources, (GDestroyNotify) vdir_source_free);
  client->priv->sources = NULL;
  
  if (!client->priv->settings)
    return;
  
  dirs = g_settings_get_strv (client->priv->settings, "vdir-calendar-dirs");
  
  for (i = 0; dirs[i] != NULL; i++)
    {
      VdirSource *source;
      GFile *file;
      GError *error = NULL;
      gchar *basename;
      
      if (!g_file_test (dirs[i], G_FILE_TEST_IS_DIR))
        {
          g_warning ("vdir path is not a directory: %s", dirs[i]);
          continue;
        }
      
      source = g_new0 (VdirSource, 1);
      source->client = client;
      source->path = g_strdup (dirs[i]);
      
      /* Derive display name from directory name */
      basename = g_path_get_basename (dirs[i]);
      source->display_name = g_strdup (basename);
      g_free (basename);
      
      /* Assign a default color (could be configurable) */
      source->color = g_strdup ("#3465A4");
      
      /* Set up file monitor */
      file = g_file_new_for_path (source->path);
      source->monitor = g_file_monitor_directory (file, G_FILE_MONITOR_NONE, NULL, &error);
      g_object_unref (file);
      
      if (source->monitor)
        {
          g_signal_connect (source->monitor, "changed",
                          G_CALLBACK (on_directory_changed), source);
        }
      else if (error)
        {
          g_warning ("Failed to monitor directory %s: %s", source->path, error->message);
          g_error_free (error);
        }
      
      /* Determine which signal to use based on content */
      /* For now, we'll emit both signals; could be refined */
      source->changed_signal_id = signals[APPOINTMENTS_CHANGED];
      
      client->priv->sources = g_slist_prepend (client->priv->sources, source);
      
      /* Load initial events */
      vdir_source_load_events (source, client);
    }
  
  g_strfreev (dirs);
}

void
calendar_client_vdir_get_date (CalendarClientVdir *client,
                          guint          *year,
                          guint          *month,
                          guint          *day)
{
  g_return_if_fail (CALENDAR_IS_CLIENT (client));

  if (year)
    *year = client->priv->year;

  if (month)
    *month = client->priv->month;

  if (day)
    *day = client->priv->day;
}

void
calendar_client_vdir_select_month (CalendarClientVdir *client,
			      guint           month,
			      guint           year)
{
  GSList *l;
  
  g_return_if_fail (CALENDAR_IS_CLIENT (client));
  g_return_if_fail (month <= 11);

  if (client->priv->year != year || client->priv->month != month)
    {
      client->priv->month = month;
      client->priv->year  = year;

      /* Reload all sources */
      for (l = client->priv->sources; l != NULL; l = l->next)
        {
          VdirSource *source = l->data;
          vdir_source_load_events (source, client);
        }

      g_signal_emit (client, signals[APPOINTMENTS_CHANGED], 0);
      g_signal_emit (client, signals[TASKS_CHANGED], 0);

      g_object_freeze_notify (G_OBJECT (client));
      g_object_notify (G_OBJECT (client), "month");
      g_object_notify (G_OBJECT (client), "year");
      g_object_thaw_notify (G_OBJECT (client));
    }
}

void
calendar_client_vdir_select_day (CalendarClientVdir *client,
			    guint           day)
{
  g_return_if_fail (CALENDAR_IS_CLIENT (client));
  g_return_if_fail (day <= 31);

  if (client->priv->day != day)
    {
      client->priv->day = day;
      g_object_notify (G_OBJECT (client), "day");
    }
}

/* Event copy helpers */
static void
calendar_appointment_copy (CalendarAppointment *appointment,
			   CalendarAppointment *appointment_copy)
{
  GSList *l;

  g_assert (appointment != NULL);
  g_assert (appointment_copy != NULL);

  appointment_copy->occurrences = g_slist_copy (appointment->occurrences);
  for (l = appointment_copy->occurrences; l; l = l->next)
    {
      CalendarOccurrence *occurrence = l->data;
      CalendarOccurrence *occurrence_copy;

      occurrence_copy             = g_new0 (CalendarOccurrence, 1);
      occurrence_copy->start_time = occurrence->start_time;
      occurrence_copy->end_time   = occurrence->end_time;

      l->data = occurrence_copy;
    }

  appointment_copy->uid          = g_strdup (appointment->uid);
  appointment_copy->backend_name = g_strdup (appointment->backend_name);
  appointment_copy->summary      = g_strdup (appointment->summary);
  appointment_copy->description  = g_strdup (appointment->description);
  appointment_copy->color_string = g_strdup (appointment->color_string);
  appointment_copy->start_time   = appointment->start_time;
  appointment_copy->end_time     = appointment->end_time;
  appointment_copy->is_all_day   = appointment->is_all_day;
}

static void
calendar_task_copy (CalendarTask *task,
		    CalendarTask *task_copy)
{
  g_assert (task != NULL);
  g_assert (task_copy != NULL);

  task_copy->uid              = g_strdup (task->uid);
  task_copy->summary          = g_strdup (task->summary);
  task_copy->description      = g_strdup (task->description);
  task_copy->color_string     = g_strdup (task->color_string);
  task_copy->start_time       = task->start_time;
  task_copy->due_time         = task->due_time;
  task_copy->percent_complete = task->percent_complete;
  task_copy->completed_time   = task->completed_time;
  task_copy->priority         = task->priority;
}

static CalendarEvent *
calendar_event_copy (CalendarEvent *event)
{
  CalendarEvent *retval;

  if (!event)
    return NULL;

  retval = g_new0 (CalendarEvent, 1);

  retval->type = event->type;

  switch (event->type)
    {
    case CALENDAR_EVENT_APPOINTMENT:
      calendar_appointment_copy (CALENDAR_APPOINTMENT (event),
				 CALENDAR_APPOINTMENT (retval));
      break;
    case CALENDAR_EVENT_TASK:
      calendar_task_copy (CALENDAR_TASK (event),
			  CALENDAR_TASK (retval));
      break;
    case CALENDAR_EVENT_ALL:
    default:
      g_assert_not_reached ();
      break;
    }

  return retval;
}

GSList *
calendar_client_vdir_get_events (CalendarClientVdir    *client,
			    CalendarEventType  event_mask)
{
  GSList *result = NULL;
  GSList *l;
  time_t day_begin, day_end;
  
  g_return_val_if_fail (CALENDAR_IS_CLIENT (client), NULL);
  g_return_val_if_fail (client->priv->day != G_MAXUINT, NULL);
  g_return_val_if_fail (client->priv->month != G_MAXUINT, NULL);
  g_return_val_if_fail (client->priv->year != G_MAXUINT, NULL);

  day_begin = make_time_for_day_begin (client->priv->day,
				       client->priv->month,
				       client->priv->year);
  day_end   = make_time_for_day_begin (client->priv->day + 1,
				       client->priv->month,
				       client->priv->year);

  for (l = client->priv->sources; l != NULL; l = l->next)
    {
      VdirSource *source = l->data;
      GHashTableIter iter;
      gpointer key, value;
      
      g_hash_table_iter_init (&iter, source->events);
      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          CalendarEvent *event = value;
          
          if (event->type == CALENDAR_EVENT_APPOINTMENT && (event_mask & CALENDAR_EVENT_APPOINTMENT))
            {
              CalendarAppointment *appt = CALENDAR_APPOINTMENT (event);
              
              /* Check if appointment is on this day */
              if ((appt->start_time >= day_begin && appt->start_time < day_end) ||
                  (appt->start_time <= day_begin && appt->end_time > day_begin))
                {
                  result = g_slist_prepend (result, calendar_event_copy (event));
                }
            }
          else if (event->type == CALENDAR_EVENT_TASK && (event_mask & CALENDAR_EVENT_TASK))
            {
              /* Include all tasks */
              result = g_slist_prepend (result, calendar_event_copy (event));
            }
        }
    }
  
  return result;
}

void
calendar_client_vdir_foreach_appointment_day (CalendarClientVdir  *client,
					 CalendarDayIter  iter_func,
					 gpointer         user_data)
{
  GSList *l;
  gboolean marked_days[32] = { FALSE, };
  time_t month_begin, month_end;
  int i;
  
  g_return_if_fail (CALENDAR_IS_CLIENT (client));
  g_return_if_fail (iter_func != NULL);
  g_return_if_fail (client->priv->month != G_MAXUINT);
  g_return_if_fail (client->priv->year != G_MAXUINT);

  month_begin = make_time_for_day_begin (1,
					 client->priv->month,
					 client->priv->year);
  month_end   = make_time_for_day_begin (1,
					 client->priv->month + 1,
					 client->priv->year);

  for (l = client->priv->sources; l != NULL; l = l->next)
    {
      VdirSource *source = l->data;
      GHashTableIter iter;
      gpointer key, value;
      
      g_hash_table_iter_init (&iter, source->events);
      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          CalendarEvent *event = value;
          
          if (event->type == CALENDAR_EVENT_APPOINTMENT)
            {
              CalendarAppointment *appt = CALENDAR_APPOINTMENT (event);
              
              if (appt->start_time >= month_begin && appt->start_time < month_end)
                {
                  struct tm *tm = localtime (&appt->start_time);
                  if (tm && tm->tm_mday >= 1 && tm->tm_mday <= 31)
                    marked_days[tm->tm_mday] = TRUE;
                }
            }
        }
    }

  for (i = 1; i < 32; i++)
    {
      if (marked_days[i])
	iter_func ((CalendarClient *)client, i, user_data);
    }
}

void
calendar_client_vdir_set_task_completed (CalendarClientVdir *client,
				    char           *task_uid,
				    gboolean        task_completed,
				    guint           percent_complete)
{
  /* Not implemented for vdir (read-only for now) */
  g_warning ("calendar_client_vdir_set_task_completed not implemented for vdir backend");
}

gboolean
calendar_client_vdir_create_task (CalendarClientVdir *client,
                            const char     *summary)
{
  /* Not implemented for vdir (read-only for now) */
  g_warning ("calendar_client_vdir_create_task not implemented for vdir backend");
  return FALSE;
}

void
calendar_client_vdir_update_appointments (CalendarClientVdir *client)
{
  GSList *l;
  
  g_return_if_fail (CALENDAR_IS_CLIENT (client));
  
  for (l = client->priv->sources; l != NULL; l = l->next)
    {
      VdirSource *source = l->data;
      vdir_source_load_events (source, client);
    }
  
  g_signal_emit (client, signals[APPOINTMENTS_CHANGED], 0);
}

void
calendar_client_vdir_update_tasks (CalendarClientVdir *client)
{
  GSList *l;
  
  g_return_if_fail (CALENDAR_IS_CLIENT (client));
  
  for (l = client->priv->sources; l != NULL; l = l->next)
    {
      VdirSource *source = l->data;
      vdir_source_load_events (source, client);
    }
  
  g_signal_emit (client, signals[TASKS_CHANGED], 0);
}
