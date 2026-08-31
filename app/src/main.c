/*
 * Lecture 3 - Homework Starter Code
 *
 * GOAL: Convert a polling loop to an event-driven workqueue architecture.
 *
 * The starter code works but is INEFFICIENT.
 * polling_thread wakes every 10ms to check a flag.
 * sensor_sim fires every 100ms - that's 10 wasted wake-ups per event.
 *
 *
 * ================================================================
 * TASKS
 * ================================================================
 *
 * TASK 1 (starter - already works, just run it):
 *   Run the starter. Count wake-ups vs real events in the log.
 *   Expected: ~10 wake-ups per sensor event. Confirm this.
 *
 * TASK 2 (implement):
 *   Replace polling_thread with a k_work handler.
 *   sensor_sim should call k_work_submit() instead of setting a flag.
 *   The handler should do what polling_thread currently does.
 *
 *   Steps:
 *   - Define a work item with K_WORK_DEFINE
 *   - Write the handler function
 *   - In sensor_sim: call k_work_submit() (remove k_sem_give + flag)
 *   - Remove the polling_thread entirely
 *
 * TASK 3 (verify):
 *   Add k_uptime_get_32() to your handler's LOG_INF.
 *   Confirm handler runs only when sensor_sim fires (every ~100ms).
 *   No unnecessary wake-ups.
 *
 * BONUS (debounce):
 *   Change sensor_sim to fire 5 events within 20ms (not 1 per 100ms).
 *   Use k_work_reschedule with 30ms delay so only ONE handler
 *   call occurs after the burst - not 5.
 *   Log the reschedule timestamps to confirm the burst collapses.
 *
 * ================================================================
 */

#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(homework, LOG_LEVEL_DBG);

#define STACK_SIZE 1024
#define SENSOR_MS 100  /* sensor fires every 100ms */
#define POLL_MS 10     /* polling consumer checks every 10ms */
#define EVENT_COUNT 10 /* total sensor events to produce */

#define BURST_EVENT_COUNT 5
#define BURST_MS 4

/* ================================================================
 * STARTER CODE -- inefficient polling version
 * Run this first, then replace with workqueue in Task 2.
 * ================================================================ */
static void sensor_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(debounce_work, sensor_handler);

/* Shared flag between sensor_sim and polling_thread */
static volatile bool sensor_flag;

/* Statistics */
static int total_events;
static int total_wakeups;
static int total_processed;

/* ------------------------------------------------------------------ */
/*  sensor_sim - fires EVENT_COUNT events, 100ms apart               */
/* ------------------------------------------------------------------ */

static void sensor_sim_fn(void *p1, void *p2, void *p3) {
  for (int i = 0; i < EVENT_COUNT; i++) {
    k_msleep(SENSOR_MS);
    for (int j = 0; j < BURST_EVENT_COUNT; j++) {
      k_msleep(BURST_MS);
      total_events++;
      LOG_INF("[SENSOR] event %d - burst %d  tick=%u", i, j, k_uptime_get_32());

      int ret = k_work_reschedule(&debounce_work, K_MSEC(30));
      if (ret < 0) {
        LOG_ERR("submit failed: %d", ret);
      }
    }
  }

  LOG_INF("[SENSOR] all events produced");
}

/* ------------------------------------------------------------------ */
/*  polling_thread - checks flag every 10ms                          */
/*                                                                     */
/*  TASK 2: Replace this entire function + thread with a k_work       */
/*  handler. The handler body is the same as what's inside the        */
/*  if (sensor_flag) block below.                                      */
/* ------------------------------------------------------------------ */

static void polling_fn(struct k_work *work) {

  while (total_processed < EVENT_COUNT) {
    k_msleep(POLL_MS);
    total_wakeups++;

    if (sensor_flag) {
      sensor_flag = false;
      total_processed++;

      /*
       * This is the "real work". In Task 2 this goes into
       * the k_work handler body.
       */
      LOG_INF("[CONSUMER] processed event %d  wakeups_so_far=%d  tick=%u",
              total_processed, total_wakeups, k_uptime_get_32());
    }
  }

  /* Summary after all events processed */
  LOG_INF("\n");
  LOG_INF("[SUMMARY] events=%d  total_wakeups=%d  wasted=%d", total_processed,
          total_wakeups, total_wakeups - total_processed);
  LOG_INF("[SUMMARY] wasted wakeups = %d%% of all wakeups",
          (total_wakeups - total_processed) * 100 / total_wakeups);
}

/* ------------------------------------------------------------------ */
/*  Threads                                                             */
/*                                                                     */
/*  TASK 2: Remove the polling_thread define. Add a K_WORK_DEFINE     */
/*  for your handler here instead.                                     */
/* ------------------------------------------------------------------ */

K_THREAD_DEFINE(sensor_thread, STACK_SIZE, sensor_sim_fn, NULL, NULL, NULL, 5,
                0, 0);

static void sensor_handler(struct k_work *work) {
  ARG_UNUSED(work);
  total_processed++;
  LOG_INF("[HANDLER] processed event %d  tick=%u", total_processed,
          k_uptime_get_32());
}

/* BONUS PLACEHOLDER - for debounce:
 *
 * K_WORK_DELAYABLE_DEFINE(debounce_work, sensor_handler);
 * In sensor_sim:
 * ================================================================ */

int main(void) {
  LOG_INF("=== L3 Homework: Polling to Workqueue ===");
  LOG_INF("Starter: polling every %dms, sensor fires every %dms", POLL_MS,
          SENSOR_MS);
  LOG_INF("Expected wasted wakeups: ~%d per event", (SENSOR_MS / POLL_MS) - 1);
  LOG_INF("Run this, count wakeups, then convert to workqueue.");

  /* Wait long enough for all events to complete */
  k_msleep((EVENT_COUNT + 2) * SENSOR_MS + 500);

  return 0;
}
