#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(l2_task1, LOG_LEVEL_DBG);

#define STACK_SIZE 1024

#define TASK_PRIO 5

#define MAX_COUNT 1000000

static volatile uint32_t counter;

static struct k_sem done_sem;

static K_MUTEX_DEFINE(mtx_counter);

void t_counter(void *p1, void *p2, void *p3) {
  for (uint32_t i = 0; i < MAX_COUNT; i++) {
    k_mutex_lock(&mtx_counter, K_FOREVER);
    counter++;
    k_mutex_unlock(&mtx_counter);
  }

  k_sem_give(&done_sem);
}


K_THREAD_DEFINE(thread_a, STACK_SIZE, t_counter, NULL, NULL, NULL, TASK_PRIO, 0, 0);

K_THREAD_DEFINE(thread_b, STACK_SIZE, t_counter, NULL, NULL, NULL, TASK_PRIO, 0, 0);

int main(void) {
  k_sem_init(&done_sem, 0, 2);
  LOG_INF("=== Lecture 2 - Task 1 ===");

  k_sem_take(&done_sem, K_FOREVER);
  k_sem_take(&done_sem, K_FOREVER);

  LOG_INF("Counter value after both tasks finished: %d", counter);
  if (counter != MAX_COUNT * 2) {
    LOG_ERR("Counter missmatches excpected value");
  } else {
    LOG_INF("No race");
  }
  return 0;
}
