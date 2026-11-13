# L6-TASK1: control start latency

## Root cause and correction

Zephyr runs the ready thread with the lowest numerical priority first.
Originally, control had priority **7** and maintenance had priority **4**.
The timer makes both ready, so maintenance runs its **45,000 us busy wait**
before control can receive the event. This is scheduling interference, not
a long control computation.

Control now has priority **3**, above maintenance's unchanged **4**.
The maintenance load remains **45 ms**.

`DEADLINE_MS` is **10 ms**, inclusive. The measurement is the time from the
timer event's `ready_ms` timestamp to the **start of control processing**,
immediately after `k_msgq_get()` returns, before tracing and logging.
It does not measure processing completion or when deferred logs reach UART.
Every control log includes the cumulative count of latencies greater than
10 ms, independently of the rate-limited warning.

## Repeated measurements

Both runs used Zephyr 4.4.0, SDK 1.0.1, QEMU 10.0.2, `qemu_x86/atom`,
the same [configuration](../app/prj.conf) and [UART overlay](../app/boards/qemu_x86.overlay),
a 500 ms initial timer delay, and a 250 ms event period.
The failing build differs from the corrected source only in
`CONTROL_PRIORITY` (7 instead of 3); both use the 10 ms deadline and the same
measurement instrumentation. Each fresh boot was stopped after sequence 19.

| Run | Sequences | Start latency | Deadline misses |
| --- | --- | --- | --- |
| [Failing console](failing/console.log) | 0-19 | 40 ms for each sequence | 20 / 20 |
| [Corrected console](corrected/console.log) | 0-19 | 0 ms for each sequence | 0 / 20 |

The build uses `CONFIG_SYS_CLOCK_TICKS_PER_SEC=100`: uptime measurements have
10 ms resolution. Thus 0 ms means the same tick, not literally zero delay,
and the nominal 45 ms busy wait can appear as 40 ms.

## Kernel shell evidence

Both full console captures include the actual `kernel thread list` output.
Relevant excerpts:

```text
Failing:
uart:~$ kernel thread list
 0x11d000 maintenance
    options: 0x0, priority: 4 timeout: 0
    state: pending, entry: 0x100a5f
 0x11d0a0 control
    options: 0x0, priority: 7 timeout: 0
    state: pending, entry: 0x100a80

Corrected:
uart:~$ kernel thread list
 0x11d000 maintenance
    options: 0x0, priority: 4 timeout: 0
    state: pending, entry: 0x100a5f
 0x11d0a0 control
    options: 0x0, priority: 3 timeout: 0
    state: pending, entry: 0x100a80
```

These snapshots were taken between events: control is waiting on the queue,
and maintenance is waiting on its semaphore. They confirm priorities and
waiting states, but are not snapshots of the delayed scheduling interval.

## CTF evidence: native_sim in WSL

The repository and Zephyr v4.4.0 were cloned into the Linux filesystem at
`~/zephyr-l6`, in Ubuntu 24.04 under WSL 2. Both configurations were built
for **native_sim/native/64**, using GCC 13.3.0's host toolchain; no Linux
Zephyr SDK is required for this target. The
[board configuration](../app/boards/native_sim_native_64.conf) selects
synchronous POSIX file tracing instead of UART tracing and disables the
extra native logging backend to avoid duplicate UART logs.

The event period, 45 ms maintenance busy wait, 10 ms deadline, and 100 Hz
uptime tick were unchanged. Only control priority differs between builds.
Both simulations exited normally at **5.4 simulated seconds**, after
sequence 19 and before the next timer event. The POSIX backend flushes
complete events directly to disk, avoiding a partially transmitted final
UART event. **Babeltrace 2.0.5 decoded both complete streams with exit status
0**, without truncating or repairing the captures.

| Native run | Per-sequence console | Uptime start latency, seq 0-19 | Misses | CTF timer-to-control-start |
| --- | --- | --- | --- | --- |
| Priority 7 | [Failing](native_sim/failing/console.log) | 40 ms each | 20 / 20 | 45 ms each |
| Priority 3 | [Corrected](native_sim/corrected/console.log) | 0 ms each | 0 / 20 | 0 ms each |

Every console sequence and cumulative miss count was checked against its
CTF `control_event_ready` / `control_start` pair. CTF timestamps resolve the
45 ms scheduling delay that the 10 ms uptime tick reports as 40 ms.
The zero simulated delay is not a worst-case guarantee on physical hardware.
Both native consoles also contain actual `kernel thread list` output:
maintenance is priority 4, control is priority 7 or 3, and both are pending
between events.

### Scheduler switches around delayed sequence 0

The committed [failing scheduler excerpt](native_sim/failing/scheduler-events.txt)
contains actual Babeltrace output, filtered to switches and named events
between timer sequences 0 and 1:

- **0.500000000 s:** `control_event_ready`, sequence 0, `ready_ms=500`;
  idle switches out and maintenance switches in.
- **0.545000000 s:** maintenance switches out after its 45 ms busy wait;
  control switches in and emits `control_start`, sequence 0, latency 40.

Control was ready but could not start while higher-priority maintenance ran.
This directly relates the delayed sequence to the scheduler's priority
choice, rather than to log-output latency.

The [corrected scheduler excerpt](native_sim/corrected/scheduler-events.txt)
shows control switching in and emitting `control_start` at **0.500000000 s**,
then blocking on the queue. Only then does maintenance switch in; it still
runs until **0.545000000 s**. Raising control priority fixes the delay without
removing the maintenance load.

### Repeating the native run and decode

Run in the WSL clone (Bash):

```bash
set -euo pipefail
cd ~/zephyr-l6/zephyr-intermediate
source ../.venv/bin/activate
export ZEPHYR_BASE="$(realpath ../zephyr)"
export ZEPHYR_TOOLCHAIN_VARIANT=host
west zephyr-export
west build -b native_sim/native/64 -d build-native-corrected app
printf 'kernel thread list\r\n' |
    build-native-corrected/zephyr/zephyr.exe \
    --no-rt --no-color --uart_stdinout --stop_at=5.4 \
    --trace-file=trace/native_sim/corrected/ctf/channel0_0 \
    > trace/native_sim/corrected/console.log
babeltrace2 --clock-seconds --no-delta trace/native_sim/corrected/ctf \
    > build-native-corrected/events.txt
```

For the failing build, copy `app` into a separate application directory,
change only `CONTROL_PRIORITY` to 7, and use separate build, console, and
trace paths. `--no-rt` advances simulated time independently of host wall
time; timestamps are simulated seconds, not Linux scheduling measurements.

The [failing CTF directory](native_sim/failing/ctf/) and
[corrected CTF directory](native_sim/corrected/ctf/) contain only `metadata`
and `channel0_0`. Keep text logs and decoded output outside these directories:
Babeltrace treats other regular files there as binary streams.
The supplied [original capture](channel0_0) and the earlier QEMU artifacts
are unchanged historical evidence; the complete native captures above are
the paired, decoded scheduler evidence for this chapter.
