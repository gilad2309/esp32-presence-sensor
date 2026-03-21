#pragma once

// ESP-NOW transmit task. Blocks on a queue of radar_msg_t and sends each
// message to the paired receiver.
// param: QueueHandle_t
void espnow_tx_task(void *param);
