/*
 * Small event-driven system built on zbus.
 *
 *   sensor_chan  <-- published every 100 ms by sensor_thread
 *      |
 *      +-- display_lis  (LISTENER)   runs in publisher context, fast update
 *      +-- logger_sub   (SUBSCRIBER) own thread, slower aggregated logging
 */

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(zbus_demo, LOG_LEVEL_INF);

#define SENSOR_PERIOD_MS 100
#define LOG_EVERY_N 10 /* subscriber logs once per 10 samples */
#define STACK_SIZE 1024
#define SUB_QUEUE_DEPTH 4

struct sensor_msg {
  uint32_t seq;
  int16_t temp_c_x10;
  uint32_t timestamp_ms;
};

/* ------------------------------------------------------------------ */
/*  Listener - fast display update, runs in the publisher's context.   */
/*  Must stay short and non-blocking.                                  */
/* ------------------------------------------------------------------ */

static void display_cb(const struct zbus_channel *chan) {
  const struct sensor_msg *msg = zbus_chan_const_msg(chan);

  LOG_INF("[DISPLAY] seq=%u  %d.%d C  t=%u ms", msg->seq, msg->temp_c_x10 / 10,
          abs(msg->temp_c_x10 % 10), msg->timestamp_ms);
}

ZBUS_LISTENER_DEFINE(display_lis, display_cb);
ZBUS_SUBSCRIBER_DEFINE(logger_sub, SUB_QUEUE_DEPTH);

ZBUS_CHAN_DEFINE(sensor_chan,       /* channel name */
                 struct sensor_msg, /* message type */
                 NULL,              /* no validator */
                 NULL,              /* no user data */
                 ZBUS_OBSERVERS(display_lis, logger_sub), ZBUS_MSG_INIT(0));

/* ------------------------------------------------------------------ */
/*  Publisher - simulated sensor, one sample every 100 ms              */
/* ------------------------------------------------------------------ */

static void sensor_thread_fn(void *p1, void *p2, void *p3) {
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  struct sensor_msg sample = {0};

  while (1) {
    k_msleep(SENSOR_PERIOD_MS);

    sample.seq++;
    sample.temp_c_x10 = 200 + (int16_t)(sys_rand32_get() % 60); /* 20.0-25.9C */
    sample.timestamp_ms = k_uptime_get_32();

    int ret = zbus_chan_pub(&sensor_chan, &sample, K_MSEC(50));
    if (ret != 0) {
      LOG_ERR("publish failed: %d", ret);
    }
  }
}

K_THREAD_DEFINE(sensor_thread, STACK_SIZE, sensor_thread_fn, NULL, NULL, NULL,
                5, 0, 0);

/* ------------------------------------------------------------------ */
/*  Subscriber - own thread, aggregates and logs at a slower rate      */
/* ------------------------------------------------------------------ */

static void logger_thread_fn(void *p1, void *p2, void *p3) {
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  const struct zbus_channel *chan;
  struct sensor_msg msg;
  int32_t sum = 0;
  uint32_t count = 0;

  while (!zbus_sub_wait(&logger_sub, &chan, K_FOREVER)) {
    if (chan != &sensor_chan) {
      continue;
    }

    if (zbus_chan_read(chan, &msg, K_MSEC(50)) != 0) {
      LOG_WRN("channel read timed out");
      continue;
    }

    sum += msg.temp_c_x10;
    count++;

    if (count == LOG_EVERY_N) {
      int32_t avg = sum / (int32_t)count;

      LOG_INF("[LOGGER ] avg of %u samples = %d.%d C  (last seq=%u, t=%u ms)",
              count, avg / 10, abs(avg % 10), msg.seq, k_uptime_get_32());
      sum = 0;
      count = 0;
    }
  }
}

K_THREAD_DEFINE(logger_thread, STACK_SIZE, logger_thread_fn, NULL, NULL, NULL,
                6, 0, 0);

int main(void) {
  LOG_INF("=== zbus event-driven demo ===");
  LOG_INF("publish every %d ms | listener: every sample | subscriber: every %d",
          SENSOR_PERIOD_MS, LOG_EVERY_N);

  return 0;
}
