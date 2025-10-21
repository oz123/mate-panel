# Remaining Manual Steps

## 1. Complete calendar-window.c Conversion

The file `calendar-window.c` still has several instances of `#ifdef HAVE_EDS` that should be changed to `#ifdef HAVE_CALENDAR` for backend-agnostic calendar UI code.

### Option A: Manual Review (Recommended)
Review each `#ifdef HAVE_EDS` in calendar-window.c and decide:
- Keep as `HAVE_EDS` if it's truly EDS-specific
- Change to `HAVE_CALENDAR` if it's general calendar UI that works with any backend

### Option B: Bulk Replace (Faster, needs testing)
```bash
cd applets/clock
sed -i 's/#ifdef HAVE_EDS/#ifdef HAVE_CALENDAR/g' calendar-window.c
sed -i 's/#endif \/\* HAVE_EDS \*\//#endif \/* HAVE_CALENDAR *\//g' calendar-window.c
```

After either approach, verify the changes compile and run correctly.

## 2. Test the Build

```bash
# From mate-panel root directory
./autogen.sh
./configure --disable-eds --enable-vdir
make
# Check for compilation errors
```

## 3. Test Runtime

```bash
# Set up test vdir calendar
mkdir -p ~/.calendars/test
cat > ~/.calendars/test/event.ics << 'EOF'
BEGIN:VCALENDAR
VERSION:2.0
PRODID:-//Test//Test//EN
BEGIN:VEVENT
UID:test-event-001
DTSTAMP:20250121T120000Z
DTSTART:20250121T140000
DTEND:20250121T150000
SUMMARY:Test Event
DESCRIPTION:This is a test event
END:VEVENT
END:VCALENDAR
EOF

# Configure the applet
gsettings set org.mate.panel.applet.clock vdir-calendar-dirs "['$HOME/.calendars/test']"

# Restart the panel
mate-panel --replace &

# Click the clock to see if the test event appears
```

## 4. Handle Edge Cases in calendar-client-vdir.c

The current implementation is functional but could be improved:

### Timezone Handling
Review the timezone conversion logic in `icaltime_to_timet()` and `expand_recurrence()` to ensure correct handling of:
- Events with TZID but no VTIMEZONE definition
- Floating time events (no timezone specified)
- Events crossing DST boundaries

### Recurrence Edge Cases
Test and handle:
- EXDATE (exception dates)
- RDATE (additional dates)
- RECURRENCE-ID (modified instances)
- Complex RRULE patterns (BYDAY with BYMONTHDAY, etc.)

### Error Handling
Add more robust error handling for:
- Malformed .ics files (continue processing other files)
- Permission errors when reading directories
- Missing VTIMEZONE definitions

## 5. Documentation Updates

### Update Main MATE Panel README
Add a note about vdir support:
```markdown
## Calendar Support

The clock applet supports two calendar backends:
- Evolution Data Server (EDS) - full read/write support
- vdir - read-only support for filesystem-based calendars

See applets/clock/README-vdir.md for details.
```

### Update INSTALL or BUILD Documentation
Mention the new --enable-vdir configure option.

## 6. Testing Matrix

Test these combinations:

| Configuration | Expected Result |
|---------------|----------------|
| --enable-eds (no vdir) | EDS backend, existing behavior |
| --enable-vdir (no EDS) | vdir backend, reads .ics files |
| --enable-eds --enable-vdir | One backend chosen at compile time |
| --disable-eds --disable-vdir | No calendar support, UI hidden |

## 7. Package Maintainer Notes

For distribution packages, consider:

### Debian/Ubuntu package split:
- `mate-panel` - base package
- `mate-panel-calendar-eds` - EDS backend (Recommends: evolution-data-server)
- `mate-panel-calendar-vdir` - vdir backend (Suggests: vdirsyncer)

### Build-Depends:
```
libical-glib-dev (>= 3.0.0) [for vdir support]
```

### README.Debian:
Document how users can switch between backends or configure vdir directories.

## 8. Upstream Submission

If submitting to MATE upstream:

1. Create a clean git branch
2. Split changes into logical commits:
   - Commit 1: Add configure.ac and Makefile.am changes
   - Commit 2: Add GSettings schema
   - Commit 3: Add calendar-client-vdir.c implementation
   - Commit 4: Update calendar-window.c conditionals
   - Commit 5: Add documentation

3. Test on multiple distributions:
   - Ubuntu/Debian
   - Fedora/RHEL
   - Arch Linux

4. Submit merge request with:
   - Description of feature
   - Rationale (use case for vdir support)
   - Testing performed
   - Documentation updates

## 9. Known Issues to Address

1. **Color Hardcoding**: Currently uses `#3465A4` for all events
   - Solution: Add color configuration per vdir directory
   - Or: Parse `X-APPLE-CALENDAR-COLOR` from .ics files

2. **No Write Support**: Tasks can't be marked complete
   - Solution: Implement by modifying .ics files atomically
   - Challenge: Handle concurrent modifications from vdirsyncer

3. **Performance with Large Calendars**: No indexing or caching by file
   - Solution: Cache parsed events with mtime tracking
   - Or: Use SQLite cache similar to EDS

## 10. Optional Enhancements

### Auto-discover vdirsyncer storage
Parse `~/.vdirsyncer/config` to automatically find local storage paths:

```c
static void
parse_vdirsyncer_config(CalendarClient *client)
{
  // Parse ~/.vdirsyncer/config
  // Look for [storage xyz] sections with type = "filesystem"
  // Extract path = values
  // Add to sources list
}
```

### Per-directory color configuration
Add GSettings key:
```xml
<key name="vdir-calendar-colors" type="a{ss}">
  <default>{}</default>
  <summary>Color mapping for vdir calendars</summary>
  <description>Maps directory paths to hex colors</description>
</key>
```

### Support for .color files
Read `.color` file in each vdir directory:
```bash
echo "#FF5733" > ~/.calendars/work/.color
```

## Completion Checklist

- [ ] Complete calendar-window.c HAVE_EDS → HAVE_CALENDAR conversion
- [ ] Test build with --enable-vdir
- [ ] Test runtime with sample .ics files
- [ ] Verify recurring events work correctly
- [ ] Verify file monitoring triggers reload
- [ ] Test with multiple vdir directories
- [ ] Test timezone handling
- [ ] Update main project documentation
- [ ] Create distribution package metadata
- [ ] Test on multiple Linux distributions
- [ ] Submit upstream (if desired)

Good luck! The core implementation is complete and functional.
