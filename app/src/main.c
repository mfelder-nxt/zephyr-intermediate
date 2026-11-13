#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(l2_task1, LOG_LEVEL_DBG);

#define STACK_SIZE 1024

#define TASK_PRIO 5

#define MAX_COUNT 1000000

static volatile uint32_t counter;
static volatile uint32_t cntr;

static struct k_sem done_sem;

static K_MUTEX_DEFINE(mtx_counter);

void unprotected_counter(void *p1, void *p2, void *p3) {
  for (uint32_t i = 0; i < MAX_COUNT; i++) {
    cntr++;
  }

  k_sem_give(&done_sem);
}

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

K_THREAD_DEFINE(thread_c, STACK_SIZE, unprotected_counter, NULL, NULL, NULL, TASK_PRIO, 0, 0);
K_THREAD_DEFINE(thread_d, STACK_SIZE, unprotected_counter, NULL, NULL, NULL, TASK_PRIO, 0, 0);

int main(void) {
  k_sem_init(&done_sem, 0, 2);
  LOG_INF("=== Lecture 2 - Task 1 ===");

  k_sem_take(&done_sem, K_FOREVER);
  k_sem_take(&done_sem, K_FOREVER);
  k_sem_take(&done_sem, K_FOREVER);
  k_sem_take(&done_sem, K_FOREVER);

  LOG_INF("Counter with mutex value after both tasks finished: %d", counter);
  if (counter != MAX_COUNT * 2) {
    LOG_ERR("Counter missmatches excpected value");
  } else {
    LOG_INF("No race, as the mutex locks the thread from interrupting by another one and the read, add and write instruction run in order.");
  }

  LOG_INF("Unprotected Counter value after both tasks finished: %d", cntr);
  if (cntr != MAX_COUNT * 2) {
    LOG_ERR("Counter missmatches, as the task can and is interrupted at some stage between the read, add and write instruction.");
  } else {
    LOG_INF("No race");
  }

  LOG_INF("Mutex protected total: %d", counter);
  LOG_INF("Unprotected total: %d", cntr);
  LOG_INF("The mutex protected counter counts correctyl as the three instructions (r,a,w) are not interrupted.");
  LOG_INF("The unprotected counter misses some counts, as the second task may execute a read right before the first task done the write and therfore overwrites the inceremented value afterwards.s");
  return 0;
}
