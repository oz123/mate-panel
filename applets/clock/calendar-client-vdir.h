/*
 * calendar-client-vdir.h: vdir-based calendar client header
 *
 * Copyright (C) 2025
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef __CALENDAR_CLIENT_VDIR_H__
#define __CALENDAR_CLIENT_VDIR_H__

#include <glib-object.h>
#include "calendar-client.h"

G_BEGIN_DECLS

#define CALENDAR_TYPE_CLIENT_VDIR        (calendar_client_vdir_get_type ())
#define CALENDAR_CLIENT_VDIR(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), CALENDAR_TYPE_CLIENT_VDIR, CalendarClientVdir))
#define CALENDAR_CLIENT_VDIR_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST ((k), CALENDAR_TYPE_CLIENT_VDIR, CalendarClientVdirClass))
#define CALENDAR_IS_CLIENT_VDIR(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), CALENDAR_TYPE_CLIENT_VDIR))
#define CALENDAR_IS_CLIENT_VDIR_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), CALENDAR_TYPE_CLIENT_VDIR))
#define CALENDAR_CLIENT_VDIR_GET_CLASS(o)(G_TYPE_INSTANCE_GET_CLASS ((o), CALENDAR_TYPE_CLIENT_VDIR, CalendarClientVdirClass))

typedef struct _CalendarClientVdir        CalendarClientVdir;
typedef struct _CalendarClientVdirClass   CalendarClientVdirClass;
typedef struct _CalendarClientVdirPrivate CalendarClientVdirPrivate;

typedef void (* CalendarDayIterVdir) (CalendarClientVdir *client,
                                      guint               day,
                                      gpointer            user_data);

struct _CalendarClientVdir
{
  GObject                    parent;
  CalendarClientVdirPrivate *priv;
};

struct _CalendarClientVdirClass
{
  GObjectClass parent_class;

  void (* appointments_changed) (CalendarClientVdir *client);
  void (* tasks_changed)        (CalendarClientVdir *client);
};

GType               calendar_client_vdir_get_type                (void) G_GNUC_CONST;
CalendarClientVdir *calendar_client_vdir_new                     (GSettings *settings);

void                calendar_client_vdir_get_date                (CalendarClientVdir  *client,
                                                                  guint               *year,
                                                                  guint               *month,
                                                                  guint               *day);
void                calendar_client_vdir_select_month            (CalendarClientVdir  *client,
                                                                  guint                month,
                                                                  guint                year);
void                calendar_client_vdir_select_day              (CalendarClientVdir  *client,
                                                                  guint                day);

GSList             *calendar_client_vdir_get_events              (CalendarClientVdir  *client,
                                                                  CalendarEventType    event_mask);
void                calendar_client_vdir_foreach_appointment_day (CalendarClientVdir  *client,
                                                                  CalendarDayIter      iter_func,
                                                                  gpointer             user_data);

void                calendar_client_vdir_set_task_completed      (CalendarClientVdir  *client,
                                                                  char                *task_uid,
                                                                  gboolean             task_completed,
                                                                  guint                percent_complete);

gboolean            calendar_client_vdir_create_task             (CalendarClientVdir  *client,
                                                                  const char          *summary);

void                calendar_client_vdir_update_appointments     (CalendarClientVdir *client);
void                calendar_client_vdir_update_tasks            (CalendarClientVdir *client);

G_END_DECLS

#endif /* __CALENDAR_CLIENT_VDIR_H__ */
