#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "radar_data.h"
#include "espnow_sender.h"

#define TX_QUEUE_DEPTH 10

void app_main(void)
{
    QueueHandle_t tx_queue = xQueueCreate(TX_QUEUE_DEPTH, sizeof(radar_msg_t));

    xTaskCreate(radar_reader_task, "radar_rd",  3072, tx_queue, 5, NULL);
    xTaskCreate(espnow_tx_task,   "espnow_tx", 4096, tx_queue, 3, NULL);
}
