/*
 * temp_alarm - Error-path unit tests (homework skeleton)
 *
 * Task 2: fill in the stubs according to TEST_SPEC.md. Each stub calls
 * ztest_test_skip(), so the binary builds and runs cleanly until the test is
 * implemented.
 *
 * Run:
 *   west twister -T tests/temp_alarm -p native_sim
 */

#include <errno.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "sensor_fake.h"
#include "temp_alarm.h"

#define DEFAULT_THRESHOLD 30

/* Same before() hook as in test_basic.c: a clean fake and an initialised
 * module. */
static void before(void *fixture) {
  ARG_UNUSED(fixture);

  sensor_fake_reset();
  sensor_fake_init();

  zassume_ok(temp_alarm_init(&fake_sensor_dev, DEFAULT_THRESHOLD),
             "precondition: temp_alarm_init must succeed");
}

/*
 * ============================================================================
 * Test Suite: temp_alarm_sensor_errors
 *
 * Failures from the sensor driver, injected through the fakes.
 * ============================================================================
 */
ZTEST_SUITE(temp_alarm_sensor_errors, NULL, NULL, before, NULL, NULL);

ZTEST(temp_alarm_sensor_errors, test_fetch_error_propagates) {
  struct temp_alarm_status status;

  fake_sensor_sample_fetch_fake.return_val = -EIO;

  int ret = temp_alarm_check();
  zassert_equal(ret, -EIO, "check must propagate -EIO, got %d", ret);

  zassert_ok(temp_alarm_get_status(&status), "get_status must succeed");
  zassert_equal(status.last_error, -EIO, "last error should be -EIO, got: %d",
                status.last_error);
  zassert_equal(fake_sensor_channel_get_fake.call_count, 0,
                "channel_get should not be called, got %u",
                fake_sensor_channel_get_fake.call_count);
}

ZTEST(temp_alarm_sensor_errors, test_channel_get_error_propagates) {
  struct temp_alarm_status status;

  fake_sensor_channel_get_fake.custom_fake = NULL;
  fake_sensor_channel_get_fake.return_val = -EIO;

  int ret = temp_alarm_check();
  zassert_equal(ret, -EIO, "check must propagate -EIO, got %d", ret);

  zassert_ok(temp_alarm_get_status(&status), "get_status must succeed");
  zassert_equal(status.last_error, -EIO, "last error should be -EIO, got: %d",
                status.last_error);
  zassert_equal(fake_sensor_sample_fetch_fake.call_count, 1,
                "sample_fetch should be called once, got %u",
                fake_sensor_sample_fetch_fake.call_count);
}

ZTEST(temp_alarm_sensor_errors, test_fail_then_recover) {
  struct temp_alarm_status status;
  static int seq[] = {-EIO, 0};

  SET_RETURN_SEQ(fake_sensor_sample_fetch, seq, ARRAY_SIZE(seq));
  int ret = temp_alarm_check();
  zassert_equal(ret, -EIO, "seq[0] should return -EIO, got %d", ret);

  sensor_fake_set_temperature(20);
  ret = temp_alarm_check();
  zassert_ok(ret, "check must succeed");
  zassert_ok(temp_alarm_get_status(&status), "get_status must succeed");
  zassert_equal(status.last_temp, 20, "last_temp must be 20, got %d",
                status.last_temp);
  zassert_false(status.is_alarming,
                "is_alarming must be false below the threshold");
  zassert_equal(status.last_error, -EIO, "last error should be -EIO, got: %d",
                status.last_error);
}

/*
 * ============================================================================
 * Test Suite: temp_alarm_invalid_input
 *
 * Arguments the public API must reject.
 * ============================================================================
 */
ZTEST_SUITE(temp_alarm_invalid_input, NULL, NULL, before, NULL, NULL);

ZTEST(temp_alarm_invalid_input, test_get_status_null) {
  int ret = temp_alarm_get_status(NULL);
  zassert_equal(ret, -EINVAL, "check should return -EINVAL, got %d", ret);
}

ZTEST(temp_alarm_invalid_input, test_init_without_device) {
  int ret = temp_alarm_init(NULL, DEFAULT_THRESHOLD);
	zassert_equal(ret, -ENODEV, "check should return -ENODEV, got %d", ret);
}

ZTEST(temp_alarm_invalid_input, test_init_threshold_out_of_range) {
  int ret = temp_alarm_init(&fake_sensor_dev, -1);
  zassert_equal(ret, -EINVAL, "check should return -EINVAL, got %d", ret);

	ret = temp_alarm_init(&fake_sensor_dev, 101);
	zassert_equal(ret, -EINVAL, "check should return -EINVAL, got %d", ret);
}

ZTEST(temp_alarm_invalid_input, test_set_threshold_out_of_range) {
  struct temp_alarm_status status;

	int ret = temp_alarm_set_threshold(-1);
  zassert_equal(ret, -EINVAL, "check should return -EINVAL, got %d", ret);

	ret = temp_alarm_set_threshold(101);
  zassert_equal(ret, -EINVAL, "check should return -EINVAL, got %d", ret);

  /* Threshold is not in the status struct: probe it at 30 C and 31 C */
  sensor_fake_set_temperature(DEFAULT_THRESHOLD);
  zassert_ok(temp_alarm_check(), "check must succeed");
  zassert_ok(temp_alarm_get_status(&status), "get_status must succeed");
  zassert_false(status.is_alarming, "30 C must not alarm, threshold changed");

  sensor_fake_set_temperature(DEFAULT_THRESHOLD + 1);
  zassert_ok(temp_alarm_check(), "check must succeed");
  zassert_ok(temp_alarm_get_status(&status), "get_status must succeed");
  zassert_true(status.is_alarming, "31 C must alarm, threshold changed");
  zassert_equal(status.alarm_count, 1, "exactly one alarm expected, got %u",
                status.alarm_count);
}
