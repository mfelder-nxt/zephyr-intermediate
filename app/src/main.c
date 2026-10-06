#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/task_wdt/task_wdt.h>

LOG_MODULE_REGISTER(zephyr_course, LOG_LEVEL_INF);

#define SENSOR_PERIOD_MS 500
#define TASK_STACK_SIZE 1024
#define QUEUE_DEPTH 10

struct sensor_msg {
  uint32_t seq;
  int16_t temp_c_x10;
  uint32_t timestamp_ms;
};
K_MSGQ_DEFINE(sensor_msgq, sizeof(struct sensor_msg), QUEUE_DEPTH,
              __alignof__(struct sensor_msg));

static void task_wdt_cb(int channel_id, void *user_data) {
  ARG_UNUSED(channel_id);
  ARG_UNUSED(user_data);
  LOG_ERR("watch dog triggered - queue full");
}

static int chan = -1;

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

    int ret = k_msgq_put(&sensor_msgq, &sample, K_FOREVER);
    if (ret != 0) {
      LOG_ERR("publish failed: %d", ret);
    }
    task_wdt_feed(chan);
  }
}

K_THREAD_DEFINE(sensor_thread, TASK_STACK_SIZE, sensor_thread_fn, NULL, NULL,
                NULL, 5, 0, 1000);

static void logger_thread_fn(void *p1, void *p2, void *p3) {
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  struct sensor_msg msg;

  while (!k_msgq_get(&sensor_msgq, &msg, K_FOREVER)) {
    LOG_INF("Temperature: %d.%d C  (seq=%u, t=%u ms)", msg.temp_c_x10 / 10,
            abs(msg.temp_c_x10 % 10), msg.seq, msg.timestamp_ms);
    k_msleep(1000);
  }
}

K_THREAD_DEFINE(logger_thread, TASK_STACK_SIZE, logger_thread_fn, NULL, NULL,
                NULL, 6, 0, 1000);

static void health_check_thread_fn(void *p1, void *p2, void *p3) {
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  uint32_t used;

  while (1) {
    used = k_msgq_num_used_get(&sensor_msgq);
    if (used > (QUEUE_DEPTH / 4 * 3)) {
      LOG_WRN("Queue filling up (now at %d/%d)", used, QUEUE_DEPTH);
    }
    k_msleep(SENSOR_PERIOD_MS);
  }
}

K_THREAD_DEFINE(health_check_thread, TASK_STACK_SIZE, health_check_thread_fn,
                NULL, NULL, NULL, 7, 0, 1000);

int main(void) {
  LOG_INF("=== l5 - task 1 ===");

  task_wdt_init(NULL);
  chan = task_wdt_add(1000, task_wdt_cb, (void *)sensor_thread);

  return 0;
}
