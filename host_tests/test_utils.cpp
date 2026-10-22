/**
 * Google Test-based unit tests for calculate_next_wakeup_interval
 */

#include <gtest/gtest.h>
#include <time.h>

#include <cstring>

#include "../main/testable_utils.h"

// Test fixture
class CalculateNextWakeupIntervalTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        // Reset config before each test
        config.enabled = false;
        config.start_minutes = 1380;  // 23:00
        config.end_minutes = 420;     // 07:00
        SetMockTime(0, 0, 0);
    }

    void SetMockTime(int hour, int minute, int second)
    {
        memset(&timeinfo, 0, sizeof(timeinfo));
        timeinfo.tm_hour = hour;
        timeinfo.tm_min = minute;
        timeinfo.tm_sec = second;
        timeinfo.tm_year = 126;  // 2026
        timeinfo.tm_mon = 0;     // January
        timeinfo.tm_mday = 20;
    }

    struct tm timeinfo;
    sleep_schedule_config_t config;
};

// Test Case 1: No sleep schedule - simple clock alignment
TEST_F(CalculateNextWakeupIntervalTest, NoSleepSchedule1HourInterval)
{
    config.enabled = false;
    SetMockTime(10, 30, 0);

    int result = calculate_next_wakeup_interval(&timeinfo, 3600, true, &config);

    EXPECT_EQ(1800, result) << "Should wake in 30 minutes (at 11:00)";
}

// Test Case 2: No sleep schedule - 30 minute interval
TEST_F(CalculateNextWakeupIntervalTest, NoSleepSchedule30MinInterval)
{
    config.enabled = false;
    SetMockTime(10, 15, 0);

    int result = calculate_next_wakeup_interval(&timeinfo, 1800, true, &config);

    EXPECT_EQ(900, result) << "Should wake in 15 minutes (at 10:30)";
}

// Test Case 3: Sleep schedule enabled, next wake-up is outside schedule
TEST_F(CalculateNextWakeupIntervalTest, SleepScheduleWakeOutside)
{
    config.enabled = true;
    config.start_minutes = 1380;  // 23:00
    config.end_minutes = 420;     // 07:00
    SetMockTime(18, 0, 0);

    int result = calculate_next_wakeup_interval(&timeinfo, 3600, true, &config);

    EXPECT_EQ(3600, result) << "Should wake in 1 hour (at 19:00)";
}

// Test Case 4: Sleep schedule enabled, next wake-up would be inside schedule
TEST_F(CalculateNextWakeupIntervalTest, SleepScheduleWakeInside)
{
    config.enabled = true;
    config.start_minutes = 1380;  // 23:00
    config.end_minutes = 420;     // 07:00
    SetMockTime(22, 30, 0);

    int result = calculate_next_wakeup_interval(&timeinfo, 3600, true, &config);

    EXPECT_EQ(30600, result)
        << "Should skip to 07:00 next day (8.5 hours) - sleep_end is exclusive";
}

// Test Case 5: Currently in sleep schedule
TEST_F(CalculateNextWakeupIntervalTest, CurrentlyInSleepSchedule)
{
    config.enabled = true;
    config.start_minutes = 1380;  // 23:00
    config.end_minutes = 420;     // 07:00
    SetMockTime(2, 0, 0);

    int result = calculate_next_wakeup_interval(&timeinfo, 3600, true, &config);

    EXPECT_EQ(18000, result) << "Should wake at 07:00 (5 hours) - sleep_end is exclusive";
}

// Test Case 6: Sleep schedule ends at aligned time
TEST_F(CalculateNextWakeupIntervalTest, SleepScheduleEndsAtAlignedTime)
{
    config.enabled = true;
    config.start_minutes = 1380;  // 23:00
    config.end_minutes = 420;     // 07:00
    SetMockTime(6, 0, 0);

    int result = calculate_next_wakeup_interval(&timeinfo, 3600, true, &config);

    EXPECT_EQ(3600, result) << "Should wake at 07:00 (1 hour)";
}

// Test Case 7: Sleep schedule with 2-hour interval
TEST_F(CalculateNextWakeupIntervalTest, SleepSchedule2HourInterval)
{
    config.enabled = true;
    config.start_minutes = 1380;  // 23:00
    config.end_minutes = 435;     // 07:15
    SetMockTime(22, 0, 0);

    int result = calculate_next_wakeup_interval(&timeinfo, 7200, true, &config);

    EXPECT_EQ(36000, result)
        << "Should skip to 08:00 next day (10 hours) - first aligned time >= sleep_end";
}

// Test Case 8: Same-day schedule (not overnight)
TEST_F(CalculateNextWakeupIntervalTest, SameDaySchedule)
{
    config.enabled = true;
    config.start_minutes = 720;  // 12:00
    config.end_minutes = 840;    // 14:00
    SetMockTime(11, 30, 0);

    int result = calculate_next_wakeup_interval(&timeinfo, 3600, true, &config);

    EXPECT_EQ(9000, result) << "Should skip to 14:00 (2.5 hours) - sleep_end is exclusive";
}

// Test Case 9: Edge case - exactly at midnight
TEST_F(CalculateNextWakeupIntervalTest, ExactlyAtMidnight)
{
    config.enabled = true;
    config.start_minutes = 1380;  // 23:00
    config.end_minutes = 420;     // 07:00
    SetMockTime(0, 0, 0);

    int result = calculate_next_wakeup_interval(&timeinfo, 3600, true, &config);

    EXPECT_EQ(25200, result) << "Should wake at 07:00 (7 hours) - sleep_end is exclusive";
}

// Test Case 10: 15-minute interval
TEST_F(CalculateNextWakeupIntervalTest, FifteenMinuteInterval)
{
    config.enabled = false;
    SetMockTime(10, 7, 0);

    int result = calculate_next_wakeup_interval(&timeinfo, 900, true, &config);

    EXPECT_EQ(480, result) << "Should wake at 10:15 (8 minutes)";
}

// Test Case 11: Time drift - woke up 40 seconds early, should skip to next interval
TEST_F(CalculateNextWakeupIntervalTest, TimeDriftWokeUpEarly)
{
    config.enabled = false;
    SetMockTime(16, 59, 20);

    int result = calculate_next_wakeup_interval(&timeinfo, 3600, true, &config);

    EXPECT_EQ(3640, result) << "Should skip to 18:00 since 40s < 60s threshold";
}

// New tests for Non-Aligned mode
TEST_F(CalculateNextWakeupIntervalTest, NonAlignedWakeOutsideSchedule)
{
    config.enabled = true;
    config.start_minutes = 1380;  // 23:00
    config.end_minutes = 420;     // 07:00
    SetMockTime(18, 5, 0);

    int result = calculate_next_wakeup_interval(&timeinfo, 3600, false, &config);

    EXPECT_EQ(3600, result) << "Should wake exactly in 1 hour (at 19:05)";
}

TEST_F(CalculateNextWakeupIntervalTest, NonAlignedWakeInsideSchedule)
{
    config.enabled = true;
    config.start_minutes = 1380;  // 23:00
    config.end_minutes = 420;     // 07:00
    SetMockTime(22, 30, 0);

    int result = calculate_next_wakeup_interval(&timeinfo, 3600, false, &config);

    // 22:30 + 1 hour = 23:30 (inside schedule)
    // Should wake at 07:00 next day (8.5 hours = 30600 seconds)
    EXPECT_EQ(30600, result) << "Should skip to 07:00 next day";
}

TEST_F(CalculateNextWakeupIntervalTest, NonAlignedCurrentlyInSchedule)
{
    config.enabled = true;
    config.start_minutes = 1380;  // 23:00
    config.end_minutes = 420;     // 07:00
    SetMockTime(2, 0, 0);         // Currently 02:00 (inside schedule)

    int result = calculate_next_wakeup_interval(&timeinfo, 3600, false, &config);

    EXPECT_EQ(18000, result) << "Should wake at 07:00 (5 hours)";
}

TEST_F(CalculateNextWakeupIntervalTest, NonAlignedOvernightCrossMidnight)
{
    config.enabled = true;
    config.start_minutes = 1380;  // 23:00
    config.end_minutes = 420;     // 07:00
    SetMockTime(22, 30, 0);       // 22:30, 30 mins before sleep schedule

    // Interval is 1 hour, so next wake at 23:30 (inside schedule)
    int result = calculate_next_wakeup_interval(&timeinfo, 3600, false, &config);

    EXPECT_EQ(30600, result) << "Should wake at 07:00 next day (8.5 hours)";
}

TEST_F(CalculateNextWakeupIntervalTest, NonAlignedSameDaySchedule)
{
    config.enabled = true;
    config.start_minutes = 720;  // 12:00
    config.end_minutes = 840;    // 14:00
    SetMockTime(11, 30, 0);      // 11:30, 30 mins before sleep schedule

    // Interval is 1 hour, so next wake at 12:30 (inside schedule)
    int result = calculate_next_wakeup_interval(&timeinfo, 3600, false, &config);

    EXPECT_EQ(9000, result) << "Should wake at 14:00 (2.5 hours)";
}

TEST_F(CalculateNextWakeupIntervalTest, SameDayScheduleWraparound)
{
    config.enabled = true;
    config.start_minutes = 0;  // 00:00
    config.end_minutes = 480;  // 08:00
    SetMockTime(23, 40, 51);   // Current time 23:40:51

    // Rotation interval 1 hour aligned
    // Next aligned time would be 00:00 (tomorrow)
    // 00:00 falls in schedule [00:00, 08:00)
    // So should skip to 08:00 tomorrow.

    int result = calculate_next_wakeup_interval(&timeinfo, 3600, true, &config);

    // 23:40:51 to 08:00:00 next day
    // 23:40:51 -> 24:00:00 = 19m 9s = 1149s
    // 00:00:00 -> 08:00:00 = 8h = 28800s
    // Total = 29949s
    EXPECT_EQ(29949, result) << "Should wake at 08:00 tomorrow (wrapper around)";
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

// ============================================================================
// Bus arrivals
// ============================================================================

TEST(ParseBusServiceListTest, TrimsAndSkipsEmptyEntries)
{
    char services[5][BUS_SERVICE_NO_LEN];
    ASSERT_EQ(3, parse_bus_service_list(" 12, 36 ,,851 ", services, 5));
    EXPECT_STREQ("12", services[0]);
    EXPECT_STREQ("36", services[1]);
    EXPECT_STREQ("851", services[2]);
}

TEST(ParseBusServiceListTest, StopsAtMax)
{
    char services[5][BUS_SERVICE_NO_LEN];
    ASSERT_EQ(5, parse_bus_service_list("1,2,3,4,5,6,7", services, 5));
    EXPECT_STREQ("5", services[4]);
}

TEST(ParseBusServiceListTest, EmptyInput)
{
    char services[5][BUS_SERVICE_NO_LEN];
    EXPECT_EQ(0, parse_bus_service_list("", services, 5));
    EXPECT_EQ(0, parse_bus_service_list(NULL, services, 5));
    EXPECT_EQ(0, parse_bus_service_list(" , ,", services, 5));
}

TEST(ParseBusServiceListTest, SkipsOverlongEntries)
{
    char services[5][BUS_SERVICE_NO_LEN];
    ASSERT_EQ(1, parse_bus_service_list("NOTASERVICE, 15", services, 5));
    EXPECT_STREQ("15", services[0]);
}

static time_t Utc(int year, int month, int day, int hour, int minute, int second)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    return timegm(&t);
}

TEST(LtaMinutesUntilTest, RoundsDown)
{
    time_t now = Utc(2026, 9, 23, 23, 40, 0);  // 07:40:00 in Singapore (UTC+8)
    EXPECT_EQ(5, lta_minutes_until("2026-09-24T07:45:59+08:00", now));
    EXPECT_EQ(1, lta_minutes_until("2026-09-24T07:41:00+08:00", now));
}

TEST(LtaMinutesUntilTest, DueOrPastIsZero)
{
    time_t now = Utc(2026, 9, 23, 23, 40, 0);
    EXPECT_EQ(0, lta_minutes_until("2026-09-24T07:40:59+08:00", now));
    EXPECT_EQ(0, lta_minutes_until("2026-09-24T07:38:00+08:00", now));
}

TEST(LtaMinutesUntilTest, HonoursOffsets)
{
    time_t now = Utc(2026, 9, 23, 23, 40, 0);
    EXPECT_EQ(10, lta_minutes_until("2026-09-23T23:50:00Z", now));
    EXPECT_EQ(10, lta_minutes_until("2026-09-23T18:50:00-05:00", now));
}

TEST(LtaMinutesUntilTest, CrossesMidnightAndMonthEnd)
{
    time_t now = Utc(2026, 9, 30, 15, 55, 0);  // 23:55 on 30 Sep in Singapore
    EXPECT_EQ(10, lta_minutes_until("2026-10-01T00:05:00+08:00", now));
}

TEST(LtaMinutesUntilTest, EmptyOrMalformed)
{
    time_t now = Utc(2026, 9, 23, 23, 40, 0);
    EXPECT_EQ(-1, lta_minutes_until("", now));
    EXPECT_EQ(-1, lta_minutes_until(NULL, now));
    EXPECT_EQ(-1, lta_minutes_until("soon", now));
    EXPECT_EQ(-1, lta_minutes_until("2026-13-01T00:00:00+08:00", now));
}

TEST(BusWindowTest, SameDayAndOvernight)
{
    EXPECT_TRUE(is_minute_in_window(390, 390, 570));
    EXPECT_FALSE(is_minute_in_window(570, 390, 570));
    EXPECT_TRUE(is_minute_in_window(1400, 1380, 60));
    EXPECT_TRUE(is_minute_in_window(30, 1380, 60));
    EXPECT_FALSE(is_minute_in_window(60, 1380, 60));
    EXPECT_FALSE(is_minute_in_window(500, 500, 500));
}

class BusWakeupTest : public ::testing::Test
{
   protected:
    struct tm At(int hour, int minute, int second)
    {
        struct tm t;
        memset(&t, 0, sizeof(t));
        t.tm_year = 126;
        t.tm_mon = 8;
        t.tm_mday = 24;
        t.tm_hour = hour;
        t.tm_min = minute;
        t.tm_sec = second;
        return t;
    }

    // Window 06:30 - 09:30, refresh every 2 minutes, 60s lead
    int Wakeup(const struct tm &t, int next_rotation)
    {
        return calculate_bus_wakeup_interval(&t, next_rotation, 390, 570, 120, 60);
    }
};

TEST_F(BusWakeupTest, RefreshesInsideWindow)
{
    EXPECT_EQ(120, Wakeup(At(7, 0, 0), 3600));
}

TEST_F(BusWakeupTest, SoonerPhotoRotationWins)
{
    EXPECT_EQ(60, Wakeup(At(7, 0, 0), 60));
}

TEST_F(BusWakeupTest, WakesWhenWindowOpens)
{
    EXPECT_EQ(1200, Wakeup(At(6, 10, 0), 3000)) << "06:10 -> 06:30, before the 07:00 rotation";
}

TEST_F(BusWakeupTest, PhotoRotationBeforeWindow)
{
    EXPECT_EQ(1800, Wakeup(At(5, 0, 0), 1800));
}

TEST_F(BusWakeupTest, WindowOpensTomorrowWithRotationOff)
{
    EXPECT_EQ(73800, Wakeup(At(10, 0, 0), 0)) << "10:00 -> 06:30 next day";
}

TEST_F(BusWakeupTest, LeadAbsorbsEarlyWake)
{
    EXPECT_EQ(120, Wakeup(At(6, 29, 20), 0)) << "Woke 40s early for the 06:30 window";
}

TEST_F(BusWakeupTest, WindowClosedAfterEnd)
{
    EXPECT_EQ(75540, Wakeup(At(9, 31, 0), 0)) << "09:31 -> 06:30 next day";
}

TEST_F(BusWakeupTest, OvernightWindow)
{
    struct tm t = At(0, 30, 0);
    EXPECT_EQ(120, calculate_bus_wakeup_interval(&t, 0, 1380, 60, 120, 60));
}

TEST_F(BusWakeupTest, EmptyWindowNeverShowsBuses)
{
    struct tm t = At(7, 0, 0);
    EXPECT_EQ(3600, calculate_bus_wakeup_interval(&t, 3600, 420, 420, 120, 60));
    EXPECT_EQ(86400, calculate_bus_wakeup_interval(&t, 0, 420, 420, 120, 60));
}
