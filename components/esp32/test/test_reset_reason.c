#include "unity.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_attr.h"
#include "soc/rtc_periph.h"
#include "driver/timer.h"
#include "esp32/rom/rtc.h"
#include "esp_sleep.h"

#define RTC_BSS_ATTR __attribute__((section(".rtc.bss")))

#define CHECK_VALUE 0x89abcdef

static __NOINIT_ATTR uint32_t s_noinit_val;
static RTC_NOINIT_ATTR uint32_t s_rtc_noinit_val;
static RTC_DATA_ATTR uint32_t s_rtc_data_val;
static RTC_BSS_ATTR uint32_t s_rtc_bss_val;
/* There is no practical difference between placing something into RTC_DATA and
 * RTC_RODATA. This only checks a usage pattern where the variable has a non-zero
 * initializer (should be initialized by the bootloader).
 */
static RTC_RODATA_ATTR uint32_t s_rtc_rodata_val = CHECK_VALUE;
static RTC_FAST_ATTR uint32_t s_rtc_force_fast_val;
static RTC_SLOW_ATTR uint32_t s_rtc_force_slow_val;


static void setup_values(void)
{
    s_noinit_val = CHECK_VALUE;
    s_rtc_noinit_val = CHECK_VALUE;
    s_rtc_data_val = CHECK_VALUE;
    s_rtc_bss_val = CHECK_VALUE;
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(CHECK_VALUE, s_rtc_rodata_val,
            "s_rtc_rodata_val should already be set up");
    s_rtc_force_fast_val = CHECK_VALUE;
    s_rtc_force_slow_val = CHECK_VALUE;
}

/* This test needs special test runners: rev1 silicon, and SPI flash with
 * fast start-up time. Otherwise reset reason will be RTCWDT_RESET.
 */
TEST_CASE("reset reason ESP_RST_POWERON", "[reset][ignore]")
{
    TEST_ASSERT_EQUAL(ESP_RST_POWERON, esp_reset_reason());
}

static void do_deep_sleep(void)
{
    setup_values();
    esp_sleep_enable_timer_wakeup(10000);
    esp_deep_sleep_start();
}

static void check_reset_reason_deep_sleep(void)
{
    TEST_ASSERT_EQUAL(ESP_RST_DEEPSLEEP, esp_reset_reason());

    TEST_ASSERT_EQUAL_HEX32(CHECK_VALUE, s_rtc_noinit_val);
    TEST_ASSERT_EQUAL_HEX32(CHECK_VALUE, s_rtc_data_val);
    TEST_ASSERT_EQUAL_HEX32(CHECK_VALUE, s_rtc_bss_val);
    TEST_ASSERT_EQUAL_HEX32(CHECK_VALUE, s_rtc_rodata_val);
    TEST_ASSERT_EQUAL_HEX32(CHECK_VALUE, s_rtc_force_fast_val);
    TEST_ASSERT_EQUAL_HEX32(CHECK_VALUE, s_rtc_force_slow_val);
}

TEST_CASE_MULTIPLE_STAGES("reset reason ESP_RST_DEEPSLEEP", "[reset_reason][reset=DEEPSLEEP_RESET]",
        do_deep_sleep,
        check_reset_reason_deep_sleep);

static void do_exception(void)
{
    setup_values();
    *(int*) (0x40000001) = 0;
}

static void do_abort(void)
{
    setup_values();
    abort();
}

static void check_reset_reason_panic(void)
{
    TEST_ASSERT_EQUAL(ESP_RST_PANIC, esp_reset_reason());

    TEST_ASSERT_EQUAL_HEX32(CHECK_VALUE, s_noinit_val);
    TEST_ASSERT_EQUAL_HEX32(CHECK_VALUE, s_rtc_noinit_val);
    TEST_ASSERT_EQUAL_HEX32(0, s_rtc_data_val);
    TEST_ASSERT_EQUAL_HEX32(0, s_rtc_bss_val);
    TEST_ASSERT_EQUAL_HEX32(CHECK_VALUE, s_rtc_rodata_val);
    TEST_ASSERT_EQUAL_HEX32(0, s_rtc_force_fast_val);
    TEST_ASSERT_EQUAL_HEX32(0, s_rtc_force_slow_val);
}

TEST_CASE_MULTIPLE_STAGES("reset reason ESP_RST_PANIC after exception", "[reset_reason][reset=LoadStoreError,SW_CPU_RESET]",
        do_exception,
        check_reset_reason_panic);

TEST_CASE_MULTIPLE_STAGES("reset reason ESP_RST_PANIC after abort", "[reset_reason][reset=abort,SW_CPU_RESET]",
        do_abort,
        check_reset_reason_panic);

static void do_restart(void)
{
    setup_values();
    esp_restart();
}

#if portNUM_PROCESSORS > 1
static void do_restart_from_app_cpu(void)
{
    setup_values();
    xTaskCreatePinnedToCore((TaskFunction_t) &do_restart, "restart", 2048, NULL, 5, NULL, 1);
    vTaskDelay(2);
}
#endif

static void check_reset_reason_sw(void)
{
    TEST_ASSERT_EQUAL(ESP_RST_SW, esp_reset_reason());

    TEST_ASSERT_EQUAL_HEX32(CHECK_VALUE, s_noinit_val);
    TEST_ASSERT_EQUAL_HEX32(CHECK_VALUE, s_rtc_noinit_val);
    TEST_ASSERT_EQUAL_HEX32(0, s_rtc_data_val);
    TEST_ASSERT_EQUAL_HEX32(0, s_rtc_bss_val);
    TEST_ASSERT_EQUAL_HEX32(CHECK_VALUE, s_rtc_rodata_val);
    TEST_ASSERT_EQUAL_HEX32(0, s_rtc_force_fast_val);
    TEST_ASSERT_EQUAL_HEX32(0, s_rtc_force_slow_val);
}

TEST_CASE_MULTIPLE_STAGES("reset reason ESP_RST_SW after restart", "[reset_reason][reset=SW_CPU_RESET]",
        do_restart,
        check_reset_reason_sw);

#if portNUM_PROCESSORS > 1
TEST_CASE_MULTIPLE_STAGES("reset reason ESP_RST_SW after restart from APP CPU", "[reset_reason][reset=SW_CPU_RESET]",
        do_restart_from_app_cpu,
        check_reset_reason_sw);
#endif


static void do_int_wdt(void)
{
    setup_values();
    portENTER_CRITICAL_NESTED();
    while(1);
}

static void do_int_wdt_hw(void)
{
    setup_values();
    XTOS_SET_INTLEVEL(XCHAL_NMILEVEL);
    while(1);
}

static void check_reset_reason_int_wdt(void)
{
    TEST_ASSERT_EQUAL(ESP_RST_INT_WDT, esp_reset_reason());
    TEST_ASSERT_EQUAL_HEX32(CHECK_VALUE, s_rtc_noinit_val);
}

TEST_CASE_MULTIPLE_STAGES("reset reason ESP_RST_INT_WDT after interrupt watchdog (panic)",
        "[reset_reason][reset=Interrupt wdt timeout on CPU0,SW_CPU_RESET]",
        do_int_wdt,
        check_reset_reason_int_wdt);

TEST_CASE_MULTIPLE_STAGES("reset reason ESP_RST_INT_WDT after interrupt watchdog (hw)",
        "[reset_reason][reset=TG1WDT_SYS_RESET]",
        do_int_wdt_hw,
        check_reset_reason_int_wdt);

static void do_task_wdt(void)
{
    setup_values();
    esp_task_wdt_init(1, true);
    esp_task_wdt_add(xTaskGetIdleTaskHandleForCPU(0));
    while(1);
}

static void check_reset_reason_task_wdt(void)
{
    TEST_ASSERT_EQUAL(ESP_RST_TASK_WDT, esp_reset_reason());

    TEST_ASSERT_EQUAL_HEX32(CHECK_VALUE, s_noinit_val);
    TEST_ASSERT_EQUAL_HEX32(CHECK_VALUE, s_rtc_noinit_val);
    TEST_ASSERT_EQUAL_HEX32(0, s_rtc_data_val);
    TEST_ASSERT_EQUAL_HEX32(0, s_rtc_bss_val);
    TEST_ASSERT_EQUAL_HEX32(CHECK_VALUE, s_rtc_rodata_val);
    TEST_ASSERT_EQUAL_HEX32(0, s_rtc_force_fast_val);
    TEST_ASSERT_EQUAL_HEX32(0, s_rtc_force_slow_val);
}

TEST_CASE_MULTIPLE_STAGES("reset reason ESP_RST_TASK_WDT after task watchdog",
        "[reset_reason][reset=abort,SW_CPU_RESET]",
        do_task_wdt,
        check_reset_reason_task_wdt);

static void do_rtc_wdt(void)
{
    setup_values();
    WRITE_PERI_REG(RTC_CNTL_WDTWPROTECT_REG, RTC_CNTL_WDT_WKEY_VALUE);
    REG_SET_FIELD(RTC_CNTL_WDTCONFIG0_REG, RTC_CNTL_WDT_SYS_RESET_LENGTH, 7);
    REG_SET_FIELD(RTC_CNTL_WDTCONFIG0_REG, RTC_CNTL_WDT_STG0, RTC_WDT_STG_SEL_RESET_SYSTEM);
    WRITE_PERI_REG(RTC_CNTL_WDTCONFIG1_REG, 10000);
    REG_SET_BIT(RTC_CNTL_WDTCONFIG0_REG, RTC_CNTL_WDT_FLASHBOOT_MOD_EN);
    while(1);
}

static void check_reset_reason_any_wdt(void)
{
    TEST_ASSERT_EQUAL(ESP_RST_WDT, esp_reset_reason());
    TEST_ASSERT_EQUAL_HEX32(CHECK_VALUE, s_rtc_noinit_val);
}

TEST_CASE_MULTIPLE_STAGES("reset reason ESP_RST_WDT after RTC watchdog",
        "[reset_reason][reset=RTCWDT_RTC_RESET]",
        do_rtc_wdt,
        check_reset_reason_any_wdt);


static void do_brownout(void)
{
    setup_values();
    printf("Manual test: lower the supply voltage to cause brownout\n");
    vTaskSuspend(NULL);
}

static void check_reset_reason_brownout(void)
{
    TEST_ASSERT_EQUAL(ESP_RST_BROWNOUT, esp_reset_reason());

    TEST_ASSERT_EQUAL_HEX32(CHECK_VALUE, s_noinit_val);
    TEST_ASSERT_EQUAL_HEX32(CHECK_VALUE, s_rtc_noinit_val);
    TEST_ASSERT_EQUAL_HEX32(0, s_rtc_data_val);
    TEST_ASSERT_EQUAL_HEX32(0, s_rtc_bss_val);
    TEST_ASSERT_EQUAL_HEX32(CHECK_VALUE, s_rtc_rodata_val);
    TEST_ASSERT_EQUAL_HEX32(0, s_rtc_force_fast_val);
    TEST_ASSERT_EQUAL_HEX32(0, s_rtc_force_slow_val);
}

TEST_CASE_MULTIPLE_STAGES("reset reason ESP_RST_BROWNOUT after brownout event",
        "[reset_reason][ignore][reset=SW_CPU_RESET]",
        do_brownout,
        check_reset_reason_brownout);


#ifdef CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
#ifndef CONFIG_FREERTOS_UNICORE
#include "soc/soc_memory_layout.h"
#include "xt_instr_macros.h"
#include "xtensa/config/specreg.h"

static int size_stack = 1024 * 3;
static StackType_t *start_addr_stack;

static int fibonacci(int n, void* func(void))
{
    int tmp1 = n, tmp2 = n;
    uint32_t base, start;
    RSR(WINDOWBASE, base);
    RSR(WINDOWSTART, start);
    printf("WINDOWBASE = %-2d   WINDOWSTART = 0x%x\n", base, start);
    if (n <= 1) {
        StackType_t *last_addr_stack = get_sp();
        StackType_t *used_stack = (StackType_t *) (start_addr_stack - last_addr_stack);
        printf("addr_stack = %p, used[%p]/all[0x%x] space in stack\n", last_addr_stack, used_stack, size_stack);
        func();
        return n;
    }
    int fib = fibonacci(n - 1, func) + fibonacci(n - 2, func);
    printf("fib = %d\n", (tmp1 - tmp2) + fib);
    return fib;
}

static void test_task(void *func)
{
    start_addr_stack = get_sp();
    if (esp_ptr_external_ram(start_addr_stack)) {
        printf("restart_task: uses external stack, addr_stack = %p\n", start_addr_stack);
    } else {
        printf("restart_task: uses internal stack, addr_stack = %p\n", start_addr_stack);
    }
    fibonacci(35, func);
}

static void func_do_exception(void)
{
    *((int *) 0) = 0;
}

static void init_restart_task(void)
{
    StackType_t *stack_for_task = (StackType_t *) heap_caps_calloc(1, size_stack, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    printf("init_task: current addr_stack = %p, stack_for_task = %p\n", get_sp(), stack_for_task);
    static StaticTask_t task_buf;
    xTaskCreateStaticPinnedToCore(test_task, "test_task", size_stack, esp_restart, 5, stack_for_task, &task_buf, 1);
    while (1) { };
}

static void init_task_do_exception(void)
{
    StackType_t *stack_for_task = (StackType_t *) heap_caps_calloc(1, size_stack, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    printf("init_task: current addr_stack = %p, stack_for_task = %p\n", get_sp(), stack_for_task);
    static StaticTask_t task_buf;
    xTaskCreateStaticPinnedToCore(test_task, "test_task", size_stack, func_do_exception, 5, stack_for_task, &task_buf, 1);
    while (1) { };
}

static void test1_finish(void)
{
    TEST_ASSERT_EQUAL(ESP_RST_SW, esp_reset_reason());
    printf("test - OK\n");
}

static void test2_finish(void)
{
    TEST_ASSERT_EQUAL(ESP_RST_PANIC, esp_reset_reason());
    printf("test - OK\n");
}

TEST_CASE_MULTIPLE_STAGES("reset reason ESP_RST_SW after restart in a task with spiram stack", "[spiram_stack][reset=SW_CPU_RESET]",
        init_restart_task,
        test1_finish);

TEST_CASE_MULTIPLE_STAGES("reset reason ESP_RST_PANIC after an exception in a task with spiram stack", "[spiram_stack][reset=StoreProhibited,SW_CPU_RESET]",
        init_task_do_exception,
        test2_finish);

#endif // CONFIG_FREERTOS_UNICORE
#endif // CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY


/* Not tested here: ESP_RST_SDIO */
