/*
 * Copyright (C) 2004 Free Software Foundation, Inc.
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
 */

#include <config.h>

#include "calendar-client.h"
#include <glib.h>
#include <string.h>

/* Helper function to create a dummy appointment event for testing/demo purposes
 * This is only compiled when --enable-vdir is used in configure
 */
CalendarEvent *
create_dummy_appointment_event (time_t day_begin)
{
  CalendarEvent *dummy_event = g_new0 (CalendarEvent, 1);
  dummy_event->type = CALENDAR_EVENT_APPOINTMENT;
  
  CalendarAppointment *dummy = CALENDAR_APPOINTMENT (dummy_event);
  dummy->uid = g_strdup ("dummy-uid");
  dummy->backend_name = g_strdup ("Dummy");
  dummy->summary = g_strdup ("Dummy Meeting");
  dummy->description = g_strdup ("This is a dummy appointment.");
  dummy->color_string = g_strdup ("#FFC0CB"); /* Pink */
  dummy->start_time = day_begin + 3600; /* 1 hour after day start */
  dummy->end_time = dummy->start_time + 3600; /* 1 hour duration */
  dummy->is_all_day = FALSE;
  
  return dummy_event;
}
