/******************************************************************************
* File Name:   i2s_task.h
*
* Description: Header file for I2S task.
*
*******************************************************************************
* (c) 2024-2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
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
