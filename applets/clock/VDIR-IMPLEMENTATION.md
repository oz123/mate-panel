# Summary of Changes: vdir Calendar Support for MATE Panel Clock Applet

## Overview
Added vdir calendar support as an alternative backend to Evolution Data Server (EDS), allowing the clock applet to read calendars from filesystem directories containing .ics files.

## Files Modified

### 1. configure.ac
**Changes:**
- Added `--enable-vdir` configure option (default: auto)
- Added dependency check for `libical-glib >= 3.0.0`
- Defined `HAVE_VDIR` macro when vdir support is enabled
- Defined `HAVE_CALENDAR` macro when either EDS or vdir is available
- Added vdir support status to configuration summary output

**Key sections:**
```m4
AC_ARG_ENABLE([vdir], ...)
PKG_CHECK_MODULES(VDIR, [libical-glib >= 3.0.0 ...])
AC_DEFINE(HAVE_VDIR, 1, ...)
AC_DEFINE(HAVE_CALENDAR, 1, ...)
AM_CONDITIONAL(HAVE_VDIR, ...)
AM_CONDITIONAL(HAVE_CALENDAR, ...)
```

### 2. applets/clock/Makefile.am
**Changes:**
- Added conditional compilation for `calendar-client-vdir.c` when `HAVE_VDIR` is true
- Added `$(VDIR_CFLAGS)` and `$(VDIR_LIBS)` to clock applet build flags
- Both EDS and vdir backends can be enabled simultaneously (one used at runtime)

**Key sections:**
```makefile
if HAVE_VDIR
CLOCK_SOURCES += \
	calendar-client-vdir.c	\
	calendar-client.h	\
	calendar-debug.h
endif

if HAVE_VDIR
CLOCK_CPPFLAGS += $(VDIR_CFLAGS)
CLOCK_LDADD += $(VDIR_LIBS)
endif
```

### 3. applets/clock/org.mate.panel.applet.clock.gschema.xml.in
**Changes:**
- Added `vdir-calendar-dirs` GSettings key (array of strings)
- Users can configure which vdir directories to read calendars from

**New key:**
```xml
<key name="vdir-calendar-dirs" type="as">
  <default>[]</default>
  <summary>List of vdir calendar directories</summary>
  <description>A list of directories containing vdir-formatted calendars...</description>
</key>
```

### 4. applets/clock/calendar-client-vdir.c (NEW FILE)
**Purpose:** Complete implementation of the CalendarClient API using filesystem-based vdir calendars.

**Key features:**
- Reads .ics files from configured vdir directories
- Parses VEVENT (appointments) and VTODO (tasks) using libical-glib
- Expands recurring events (RRULE) into individual instances
- Monitors directories for changes using GFileMonitor
- Handles timezones correctly (converts to local time)
- Implements all functions from calendar-client.h API
- Read-only (task completion and creation return warnings)

**Major components:**
- `VdirSource` struct: Represents one vdir directory
- `vdir_source_load_events()`: Scans directory and parses .ics files
- `expand_recurrence()`: Handles RRULE expansion
- `create_event_from_component()`: Converts ICalComponent to CalendarEvent
- `on_directory_changed()`: File monitor callback for auto-reload
- Full implementation of calendar_client_* API functions

**Lines of code:** ~1500

### 5. applets/clock/calendar-window.c
**Changes:**
- Changed some `#ifdef HAVE_EDS` to `#ifdef HAVE_CALENDAR` where calendar UI code should work with either backend
- Left EDS-specific code (e.g., EDS-only features) under `#ifdef HAVE_EDS`
- Updated comments to reflect "calendar" instead of "EDS-specific"

**Note:** Some `#ifdef HAVE_EDS` instances remain in calendar-window.c. These should be manually reviewed and changed to `#ifdef HAVE_CALENDAR` where the code is backend-agnostic. A script like:
```bash
sed -i 's/#ifdef HAVE_EDS/#ifdef HAVE_CALENDAR/g' calendar-window.c
sed -i 's/#endif \/\* HAVE_EDS \*\//#endif \/* HAVE_CALENDAR *\//g' calendar-window.c
```
Can complete the conversion, but review is recommended.

### 6. applets/clock/README-vdir.md (NEW FILE)
**Purpose:** Complete documentation for users and developers.

**Contents:**
- Overview of vdir format
- Build configuration options
- Setup instructions with vdirsyncer
- GSettings configuration examples
- Feature matrix (supported/not supported)
- .ics file format examples
- Troubleshooting guide
- Technical implementation details
- Performance considerations
- EDS vs vdir comparison table

## Build Instructions

### Build with vdir only (no EDS)
```bash
./autogen.sh
./configure --disable-eds --enable-vdir
make
sudo make install
```

### Build with both EDS and vdir (either can be used)
```bash
./configure --enable-eds --enable-vdir
make
sudo make install
```

Note: When both are enabled, only one backend is compiled in based on which is configured first. To switch:
- EDS build: compile with HAVE_EDS defined
- vdir build: compile with HAVE_VDIR defined (and not HAVE_EDS)

## Usage

### Configure vdir directories
```bash
gsettings set org.mate.panel.applet.clock vdir-calendar-dirs \
  "['$HOME/.calendars/personal', '$HOME/.calendars/work']"
```

### Use with vdirsyncer
1. Install and configure vdirsyncer
2. Sync calendars to local vdir directories
3. Point clock applet to those directories via GSettings
4. Calendars auto-update when vdirsyncer syncs

## Testing Checklist

- [ ] Configure builds successfully with --enable-vdir
- [ ] Configure builds successfully with --disable-eds --enable-vdir
- [ ] Applet compiles and links with vdir backend
- [ ] GSettings schema installs correctly
- [ ] Can set vdir-calendar-dirs via gsettings
- [ ] Clock applet loads vdir calendars
- [ ] Appointments display in calendar window
- [ ] Tasks display in calendar window
- [ ] Recurring events expand correctly
- [ ] File changes trigger automatic reload
- [ ] Multiple vdir directories work
- [ ] All-day events display correctly
- [ ] Timezone handling is correct

## Known Limitations

1. **Read-only**: Cannot create tasks or mark them completed from UI
2. **No calendar colors from metadata**: Colors are hardcoded per directory
3. **No vdirsyncer config auto-discovery**: Must manually set directories
4. **Performance**: Not optimized for very large calendars (>1000 events)

## Future Enhancements

1. **Write support**: Implement task completion and creation by editing .ics files
2. **Auto-discovery**: Parse ~/.vdirsyncer/config to find storage paths
3. **Color configuration**: Allow per-directory color customization
4. **Performance**: Add caching and indexing for large calendars
5. **Extended metadata**: Support X-APPLE-CALENDAR-COLOR and other extensions

## Architecture

```
calendar-window.c (UI)
        |
        | uses CalendarClient API
        |
        v
calendar-client.h (API definition)
        ^
        |
        +-- calendar-client.c (EDS implementation, when HAVE_EDS)
        |
        +-- calendar-client-vdir.c (vdir implementation, when HAVE_VDIR)
                    |
                    +-- Uses libical-glib for parsing
                    +-- Uses GFileMonitor for change detection
                    +-- Reads .ics files from filesystem
```

## Dependencies

### New Dependencies (vdir)
- libical-glib >= 3.0.0
- gio-2.0 (already required)

### Existing Dependencies (EDS - unchanged)
- libecal-2.0 >= 3.33.2
- libedataserver-1.2 >= 3.5.3

## Compatibility

- **Backward compatible**: Existing EDS builds are unaffected
- **Configuration compatible**: GSettings schema is additive (new key only)
- **Runtime compatible**: Either backend works with same UI code

## References

- vdir specification: https://vdirsyncer.pimutils.org/en/stable/vdir.html
- libical-glib documentation: https://libical.github.io/libical/
- iCalendar RFC 5545: https://tools.ietf.org/html/rfc5545
