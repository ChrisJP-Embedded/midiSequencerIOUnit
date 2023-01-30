
#include "esp_err.h"
#include "esp_log.h"
#include "systemLowLevel.h"



static void configureUart(void);
static void configureTimers(void);
static bool timerISR_midiDeltaTimeClock(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx);


gptimer_handle_t gptimer = NULL; //Handle for timer used to generate delta-times


volatile bool deltaTimerFired = false;
static bool timerISR_midiDeltaTimeClock(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    deltaTimerFired = true;
    return false;
}


void initSystemLowLevel(void)
{
    configureTimers();
    configureUart();
}


inline void startDeltaTimer(uint32_t deltaTime)
{
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = deltaTime, //Count in microseconds
    };
    gptimer_set_raw_count(gptimer, 0);
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));
    ESP_ERROR_CHECK(gptimer_start(gptimer));
}


static void configureTimers(void)
{
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, //1us tick
        
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = timerISR_midiDeltaTimeClock, // register user callback

    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
}


static void configureUart(void)
{
    uart_config_t uart_config = {
        .baud_rate = 31250,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB
    };

    ESP_ERROR_CHECK(uart_param_config(MIDI_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(MIDI_UART_NUM, 43, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(MIDI_UART_NUM, 140, 140, 0, NULL, 0));
}
