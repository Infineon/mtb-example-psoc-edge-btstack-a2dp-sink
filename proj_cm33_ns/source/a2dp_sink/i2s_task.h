/******************************************************************************
* File Name:   i2s_task.h
*
* Description: Header file for I2S task.
*
*******************************************************************************
* Copyright 2024-2025, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/
#ifndef I2S_TASK_H
#define I2S_TASK_H

/******************************************************************************
 *                                INCLUDES
 *****************************************************************************/
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "cybsp.h"
#include "IFX_asrc.h"
/******************************************************************************
 *                                MACROS
 *****************************************************************************/
/* Actual latency = I2S_TX_INITIAL_FRAME_DELAY * 1 packet duration = I2S_TX_INITIAL_FRAME_DELAY * 14.5 ms */
#define I2S_TX_INITIAL_FRAME_DELAY                  (10)

#define DECODED_PCM_FRAME_2CH_SIZE                  (5 * 128 * 2 * sizeof(int16_t))                                         /* In bytes; ~14.5125 ms of data per channel for 44100 Hz sampling rate = 1 packet (5 frames) */
#define DECODED_PCM_FRAME_1CH_SIZE                  (5 * 128 * sizeof(int16_t))                                             /* In bytes; ~14.5125 ms of data per channel for 44100 Hz sampling rate = 1 packet (5 frames) */
#define DECODED_PCM_BUFFER_SIZE                     (DECODED_PCM_FRAME_1CH_SIZE * (I2S_TX_INITIAL_FRAME_DELAY + 5u))        /* In bytes; ~87.075 ms of data per channel for 44100 Hz sampling rate */

#define I2S_TASK_QUEUE_LENGTH                       (50u)
#define I2S_SAMPLING_RATE_8k                        (8000UL)
#define I2S_SAMPLING_RATE_16k                       (16000UL)
#define I2S_SAMPLING_RATE_44_1k                     (44100UL)
#define I2S_SAMPLING_RATE_48k                       (48000UL)
/* I2S hardware FIFO size */
#define I2S_HW_FIFO_SIZE                            (128U)
/* I2S hardware half FIFO size is 64 */
#define HW_FIFO_HALF_SIZE                           (I2S_HW_FIFO_SIZE/2)

#define I2S_WATERMARK_MONITOR_COUNT                 (10000U)
#define I2S_WATERMARK_MONITOR_CONSECUTIVE_COUNT     (3U)

#define I2S_SAMPLING_RATE                           I2S_SAMPLING_RATE_44_1k

/* 44.1KHz playback */
#define MCLK_HZ                            (5644800U)
/* Sampling rate in KHz */
#define SAMPLE_RATE_HZ                     (TLV320DAC3100_DAC_SAMPLE_RATE_16_KHZ)
/* I2S word length parameter */
#define I2S_WORD_LENGTH                    (TLV320DAC3100_I2S_WORD_SIZE_16)

/* I2C address */
#define I2C_ADDRESS                       (0x18)
/* I2C frequency in Hz */
#define I2C_FREQUENCY_HZ                  (400000U)

/*******************************************************************************
* Data structure and enumeration
********************************************************************************/
/* Data-type for I2S task queue data */
typedef struct
{
    uint32_t data_len;     /* Total number of bytes of data to be transmitted via I2S */
    uint8_t* data;         /* Pointer to the start of the data to be transmitted */
} i2s_task_q_data_t;

/*******************************************************************************
* Extern Variables
********************************************************************************/
extern TaskHandle_t  i2s_task_handle;
extern QueueHandle_t i2s_task_q;
extern int32_t i2s_task_q_num_samples;
extern IFX_ASRC_STRUCT_t    asrc_mem;
/*******************************************************************************
* Function Prototypes
********************************************************************************/
void i2s_init(void);
void hw_codec_init(void);
void start_i2s(void);
void stop_i2s(void);

#endif  /* I2S_TASK_H */
