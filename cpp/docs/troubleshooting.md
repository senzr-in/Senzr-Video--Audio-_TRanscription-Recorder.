# Troubleshooting & Diagnostics

## Common Issues & Resolutions

### 1. `find_package(Senzr REQUIRED)` Fails
**Symptom**: CMake cannot find `SenzrConfig.cmake`.
**Fix**: Specify `-DCMAKE_PREFIX_PATH=/opt/senzr` or the target installation prefix where `lib/cmake/Senzr/` is located.

### 2. GPIO Access Denied (`ErrorCode::PermissionDenied`)
**Symptom**: Opening `/dev/gpiochip*` or `/sys/class/gpio` throws an error.
**Fix**: Add the execution user to the `gpio` group or run `sudo usermod -aG dialout,gpio $USER`.

### 3. Watchdog Permission Failure
**Symptom**: `open("/dev/watchdog")` fails.
**Fix**: Verify permissions on `/dev/watchdog` (requires `root` or `watchdog` group).

### 4. Database Lock / Busy (`ErrorCode::DatabaseError`)
**Symptom**: Multiple threads or processes accessing SQLite trigger `database is locked`.
**Fix**: `libsenzr` configures WAL mode automatically. Ensure the directory has read/write permissions for `.db-wal` and `.db-shm`.
