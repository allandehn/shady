# potato-bench

Tiny regression tracker for the potato rule (smooth, then low RAM, then small disk).

    POTATO_START_CMD="build/APP --self-test" tools/potato-bench.sh -n 5 -i 5 -- build/APP [args]

Appends one CSV line per run to `~/.cache/potato-bench/<app>.csv` (override with `-o`):

`date,commit,app,size_b,cold_ms,warm_ms,idle_s,rss_kb,pss_kb,frame_avg/max/hitches`

- **size_b**: binary size in bytes.
- **warm_ms**: mean of `POTATO_START_CMD`, using hyperfine if it's on PATH, else a timed loop. Unset means `na`.
- **cold_ms**: only when run as root (it drops the page cache), otherwise `na`.
- **rss_kb / pss_kb**: from `/proc/PID/smaps_rollup` after `-i` seconds idle.
- **frames**: parsed from `QSG_RENDER_TIMING=1` output. avg/max ms, plus hitches (>16.7 ms). Shows `na` without a real GPU or window (e.g. offscreen).

Needs only sh, coreutils and awk. Compare runs on the same machine only.
