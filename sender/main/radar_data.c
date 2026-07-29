#include "radar_data.h"
#include "deep_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_sleep.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// === UART pin mapping =========================================================
//   C4001 TX  ->  GPIO 4  (ESP32 receives)
//   C4001 RX  ->  GPIO 5  (ESP32 transmits)
#define RADAR_UART_PORT UART_NUM_1
#define RADAR_RX_GPIO 4
#define RADAR_TX_GPIO 5
#define RADAR_BAUD_RATE 9600
#define BUF_SIZE 128
#define UART_EVENT_QUEUE_DEPTH 16

// === Sensor configuration ====================================================
#define DETECT_THRESHOLD 30 // speed mode threshold (higher = less sensitive)
#define MIN_RANGE_CM 30
#define MAX_RANGE_CM 2500 // SEN0609 hardware max: 25 m

// === Send policy =============================================================
#define HEARTBEAT_INTERVAL_MS 3000 // re-send while person is present
#define ABSENT_TIMEOUT_MS 5000     // milliseconds of continuous "not present" before declaring absent

// === Frame parsing ===========================================================

static bool parse_frame(char *line, radar_msg_t *out)
{
    char *parts[8];
    int count = 0;
    char *tok = strtok(line, ",");
    while (tok && count < 8)
    {
        parts[count++] = tok;
        tok = strtok(NULL, ",");
    }

    if (count < 2)
        return false;

    // Speed mode: $DFDMD,<targets>,<??>,<range>,<speed>,...
    if (strcmp(parts[0], "$DFDMD") == 0 && count >= 5)
    {
        out->present = (atoi(parts[1]) > 0);
        out->range = atof(parts[3]);
        out->speed = atof(parts[4]);
        return true;
    }

    // Existence mode: $DFHPD,<present>,...
    if (strcmp(parts[0], "$DFHPD") == 0)
    {
        out->present = (bool)atoi(parts[1]);
        out->range = 0.0f;
        out->speed = 0.0f;
        return true;
    }

    return false;
}

// === Sensor UART commands ====================================================

static void send_raw(const char *cmd)
{
    printf("[CMD] >>> %s\n", cmd);
    uart_write_bytes(RADAR_UART_PORT, cmd, strlen(cmd));
    vTaskDelay(100 / portTICK_PERIOD_MS);
}

static void read_response(void)
{
    uint8_t resp[128];
    int len = uart_read_bytes(RADAR_UART_PORT, resp, sizeof(resp) - 1,
                              200 / portTICK_PERIOD_MS);
    if (len > 0)
    {
        resp[len] = '\0';
        printf("[CMD] <<< %s\n", (char *)resp);
    }
}

// Matches the DFRobot library's writeCMD() pattern:
// stop → command → save → start
static void send_config_cmd(const char *cmd)
{
    uart_flush(RADAR_UART_PORT);
    send_raw("sensorStop");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    read_response();

    send_raw(cmd);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    send_raw("saveConfig");
    vTaskDelay(100 / portTICK_PERIOD_MS);
    send_raw("sensorStart");
    vTaskDelay(100 / portTICK_PERIOD_MS);
    read_response();
}

static void configure_sensor(void)
{
    char cmd[32];

    vTaskDelay(500 / portTICK_PERIOD_MS);

    send_config_cmd("setRunApp 1");

    snprintf(cmd, sizeof(cmd), "setRange %d %d", MIN_RANGE_CM, MAX_RANGE_CM);
    send_config_cmd(cmd);

    snprintf(cmd, sizeof(cmd), "setThrFactor %d", DETECT_THRESHOLD);
    send_config_cmd(cmd);

    printf("Configuration done — threshold: %d\n", DETECT_THRESHOLD);
}

// === Debounce + send logic ===================================================

typedef struct
{
    QueueHandle_t tx_queue;
    bool last_present;
    TickType_t first_absent_tick;
    bool absent_timing;
    TickType_t last_send_tick;
} send_state_t;

static void process_frame(send_state_t *state, radar_msg_t *msg)
{
    // Debounce: require ABSENT_TIMEOUT_MS of continuous "not present" before declaring absent
    if (msg->present)
    {
        state->absent_timing = false;
    }
    else
    {
        if (!state->absent_timing)
        {
            state->absent_timing = true;
            state->first_absent_tick = xTaskGetTickCount();
        }
        if ((xTaskGetTickCount() - state->first_absent_tick) < pdMS_TO_TICKS(ABSENT_TIMEOUT_MS))
            msg->present = true; // suppress — not enough time has passed
    }

    // Only send on state change or periodic heartbeat while present
    bool state_changed = (msg->present != state->last_present);
    bool heartbeat_due = msg->present &&
                         (xTaskGetTickCount() - state->last_send_tick >=
                          pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS));

    if (!state_changed && !heartbeat_due)
        return;

    if (xQueueSend(state->tx_queue, msg, 0) == pdTRUE)
    {
        state->last_present = msg->present;
        state->last_send_tick = xTaskGetTickCount();
    }
    else
    {
        printf("[WARN] TX queue full, dropping frame\n");
    }
}

// === Task entry point =========================================================

void radar_reader_task(void *param)
{
    send_state_t state = {
        .tx_queue = (QueueHandle_t)param,
        .last_present = false,
        .absent_timing = false,
        .first_absent_tick = 0,
        .last_send_tick = 0,
    };

    // UART init
    QueueHandle_t uart_event_queue;
    uart_config_t uart_cfg = {
        .baud_rate = RADAR_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_driver_install(RADAR_UART_PORT, BUF_SIZE * 2, 0,
                        UART_EVENT_QUEUE_DEPTH, &uart_event_queue, 0);
    uart_param_config(RADAR_UART_PORT, &uart_cfg);
    uart_set_pin(RADAR_UART_PORT, RADAR_TX_GPIO, RADAR_RX_GPIO,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    printf("C4001 radar reader started (9600 baud)\n");

    /* Only configure sensor on first power-on.
       On wake from deep sleep, C4001 retains config in flash. */
    if (get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED)
    {
        printf("[RADAR] First boot — configuring sensor\n");
        configure_sensor();
    }
    else
    {
        printf("[RADAR] Wake from sleep — skipping sensor config (retained in flash)\n");
    }

    // Main loop: assemble lines from UART bytes, parse, and forward
    uint8_t buf[BUF_SIZE];
    char line[BUF_SIZE];
    int line_len = 0;
    uart_event_t event;

    while (1)
    {
        if (!xQueueReceive(uart_event_queue, &event, portMAX_DELAY))
            continue;
        if (event.type != UART_DATA)
            continue;

        int len = uart_read_bytes(RADAR_UART_PORT, buf, event.size, 0);
        for (int i = 0; i < len; i++)
        {
            char c = (char)buf[i];
            if (c == '\n' || c == '\r')
            {
                if (line_len > 0)
                {
                    line[line_len] = '\0';
                    radar_msg_t msg;
                    if (parse_frame(line, &msg))
                        process_frame(&state, &msg);
                    line_len = 0;
                }
            }
            else if (line_len < BUF_SIZE - 1)
            {
                line[line_len++] = c;
            }
        }
    }
}
