# MATE Panel Clock Applet - vdir Calendar Support

## Overview

The MATE Panel clock applet now supports reading calendar events and tasks from vdir directories as an alternative to Evolution Data Server (EDS). This allows you to use calendars managed by tools like vdirsyncer without requiring Evolution.

## What is vdir?

vdir is a file-based calendar storage format where each calendar is stored as a directory containing individual `.ics` files (one per event or task). This format is documented at: https://vdirsyncer.pimutils.org/en/stable/vdir.html

## Build Options

The clock applet can be built with three calendar backend configurations:

1. **EDS only** (default): `./configure --enable-eds` or `./configure` (auto-detects EDS)
2. **vdir only**: `./configure --disable-eds --enable-vdir`
3. **Both**: `./configure --enable-eds --enable-vdir` (note: only one backend can be used at runtime)

### Dependencies

- **For EDS support**: `libecal-2.0 >= 3.33.2`, `libedataserver-1.2 >= 3.5.3`
- **For vdir support**: `libical-glib >= 3.0.0`, `gio-2.0`

## Configuration

**Note:** vdir directories are configured exclusively through GSettings (command line). There is no GUI preferences dialog for vdir configuration. This keeps the implementation simple and follows Unix configuration principles.

### Setting up vdir directories

1. Create or identify the directories containing your vdir calendars. For example:
   ```bash
   ~/.calendars/personal/
   ~/.calendars/work/
   ```

2. Each directory should contain `.ics` files with VEVENT (appointments) or VTODO (tasks) components.

3. Configure the clock applet to use these directories via GSettings:
   ```bash
   # Set calendar directories (use ABSOLUTE paths, not ~)
   gsettings set org.mate.panel.applet.clock vdir-calendar-dirs \
     "['$HOME/.calendars/personal', '$HOME/.calendars/work']"
   
   # View current configuration
   gsettings get org.mate.panel.applet.clock vdir-calendar-dirs
   
   # Reset to default (empty list)
   gsettings reset org.mate.panel.applet.clock vdir-calendar-dirs
   ```

4. Restart the MATE panel for changes to take effect:
   ```bash
   mate-panel --replace &
   ```

**Important:** Use absolute paths (not relative like `~/calendars`). The `$HOME` variable expands correctly in the gsettings command.

### Using with vdirsyncer

[vdirsyncer](https://vdirsyncer.pimutils.org/) is a popular tool for synchronizing calendars between different services (CalDAV, Google Calendar, etc.) and local vdir storage.

1. Install vdirsyncer:
   ```bash
   # Debian/Ubuntu
   sudo apt install vdirsyncer
   
   # Fedora
   sudo dnf install vdirsyncer
   
   # pip
   pip install vdirsyncer
   ```

2. Configure vdirsyncer (example `~/.vdirsyncer/config`):
   ```ini
   [general]
   status_path = "~/.vdirsyncer/status/"

   [pair my_calendar]
   a = "my_calendar_local"
   b = "my_calendar_remote"
   collections = ["from a", "from b"]

   [storage my_calendar_local]
   type = "filesystem"
   path = "~/.calendars/"
   fileext = ".ics"

   [storage my_calendar_remote]
   type = "caldav"
   url = "https://example.com/caldav/"
   username = "your_username"
   password = "your_password"
   ```

3. Discover and sync:
   ```bash
   vdirsyncer discover
   vdirsyncer sync
   ```

4. Set up automatic sync (optional):
   ```bash
   # Add to crontab
   */15 * * * * /usr/bin/vdirsyncer sync
   ```

5. Configure MATE clock applet:
   ```bash
   gsettings set org.mate.panel.applet.clock vdir-calendar-dirs \
     "['$HOME/.calendars/personal', '$HOME/.calendars/work']"
   ```

## Features

### Supported
- ✅ Display appointments (VEVENT) from `.ics` files
- ✅ Display tasks (VTODO) from `.ics` files
- ✅ Recurring events (RRULE) with automatic expansion
- ✅ All-day events
- ✅ Multiple calendar sources (directories)
- ✅ Automatic reload when `.ics` files change (via GFileMonitor)
- ✅ Color-coding by calendar source
- ✅ Timezone handling

### Not Yet Supported (Read-Only)
- ❌ Marking tasks as completed
- ❌ Creating new tasks via the UI
- ❌ Editing events/tasks

The vdir backend is currently **read-only**. If you need to modify events or tasks, edit the `.ics` files directly or use a calendar application that supports vdir.

## File Format

Each `.ics` file in a vdir directory should be a valid iCalendar file. Example:

```icalendar
BEGIN:VCALENDAR
VERSION:2.0
PRODID:-//Example//Calendar//EN
BEGIN:VEVENT
UID:12345678-1234-1234-1234-123456789012
DTSTAMP:20250101T120000Z
DTSTART:20250115T100000
DTEND:20250115T110000
SUMMARY:Team Meeting
DESCRIPTION:Weekly team sync
END:VEVENT
END:VCALENDAR
```

For recurring events:
```icalendar
BEGIN:VEVENT
UID:recurring-event-uid
DTSTAMP:20250101T120000Z
DTSTART:20250106T140000
DTEND:20250106T150000
SUMMARY:Recurring Meeting
RRULE:FREQ=WEEKLY;BYDAY=MO,WE,FR
END:VEVENT
```

## Troubleshooting

### Calendars not appearing

1. Check that the directories are configured correctly:
   ```bash
   gsettings get org.mate.panel.applet.clock vdir-calendar-dirs
   ```

2. Verify the directories exist and contain `.ics` files:
   ```bash
   ls -la ~/.calendars/personal/*.ics
   ```

3. Check that `.ics` files are valid:
   ```bash
   # Install icalendar tools
   sudo apt install python3-icalendar
   
   # Validate
   python3 -c "import icalendar; icalendar.Calendar.from_ical(open('event.ics', 'rb').read())"
   ```

4. Check panel logs for errors:
   ```bash
   journalctl --user -f | grep mate-panel
   ```

### Events not updating

The clock applet monitors vdir directories for changes automatically. If events don't update:

1. Restart the panel:
   ```bash
   mate-panel --replace &
   ```

2. Check file permissions on the vdir directories

3. Verify vdirsyncer is running and syncing successfully:
   ```bash
   vdirsyncer sync --verbosity=INFO
   ```

## Technical Details

### Implementation

The vdir backend (`calendar-client-vdir.c`) implements the same `CalendarClient` API as the EDS backend but reads calendars directly from the filesystem:

1. **Directory enumeration**: Scans configured vdir directories for `.ics` files
2. **Parsing**: Uses `libical-glib` to parse iCalendar data
3. **Recurrence expansion**: Expands recurring events (RRULE) into individual instances for the requested time range
4. **Monitoring**: Uses `GFileMonitor` to watch for file changes and automatically reload
5. **Timezone handling**: Converts event times to local timezone using system timezone

### File Organization

- `calendar-client.h`: Common API used by both backends
- `calendar-client.c`: EDS implementation (when `HAVE_EDS` is defined)
- `calendar-client-vdir.c`: vdir implementation (when `HAVE_VDIR` is defined)
- `calendar-sources.c/h`: EDS-specific source discovery (not used with vdir)
- `calendar-window.c`: UI code (works with either backend via `HAVE_CALENDAR`)

### Performance Considerations

- The vdir backend caches parsed events in memory
- File monitoring minimizes unnecessary rescans
- Recurrence expansion is limited to 1000 instances per rule to prevent runaway expansion
- Only events within the requested time range (typically one month) are processed

## Comparison: EDS vs vdir

| Feature | EDS | vdir |
|---------|-----|------|
| **Setup Complexity** | High (requires Evolution) | Low (just directories) |
| **Dependencies** | libecal, libedataserver | libical-glib |
| **Write Support** | Yes (full) | No (read-only) |
| **Sync Tools** | Evolution, GNOME Calendar | vdirsyncer, many others |
| **File Format** | SQLite database | Plain .ics files |
| **Performance** | Optimized for large calendars | Good for typical use |
| **Multiple Calendars** | Full support | Directory-based |

## Contributing

To add write support (task completion, event creation):

1. Implement `calendar_client_set_task_completed()` to modify `.ics` files
2. Implement `calendar_client_create_task()` to generate new `.ics` files
3. Handle concurrent modification (file locking or UID-based conflict resolution)
4. Test with vdirsyncer to ensure compatibility

## License

This code is licensed under the GNU General Public License v2 or later, consistent with the MATE Panel project.

## See Also

- [vdirsyncer documentation](https://vdirsyncer.pimutils.org/)
- [vdir specification](https://vdirsyncer.pimutils.org/en/stable/vdir.html)
- [iCalendar RFC 5545](https://tools.ietf.org/html/rfc5545)
- [MATE Desktop](https://mate-desktop.org/)
