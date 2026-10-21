/******************************************************************************
* File Name:   i2s_task.c
*
* Description: Source file for I2S task.
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
/******************************************************************************
 *                                INCLUDES
 *****************************************************************************/
#include "cybsp.h"
#include "cy_pdl.h"
#include "mtb_hal.h"
#include "i2s_task.h"
#include "mtb_tlv320dac3100.h"
#include "cy_retarget_io.h"
/******************************************************************************
* Macros
******************************************************************************/
/* Debounce delay for the button */
#define DEBOUNCE_DELAY_MS           (10U)         /* In ms */

#define I2S_TASK_DELAY_TICKS        (3U)
/* HFCLK1 Clock Divider */
#define HFCLK1_CLK_DIVIDER          (4U)

#define CHECK_RESULT(result)        do                                          \
                                    {                                           \
                                        CY_ASSERT(CY_RSLT_SUCCESS == result);   \
                                    }                                           \
                                    while(0)
                                        
/*******************************************************************************
* Global Variables
********************************************************************************/
/* Audio encoder task handle */
TaskHandle_t i2s_task_handle;

/* Handle of the queue holding the Audio encoder task commands. */
QueueHandle_t i2s_task_q;

/* Variable to receive the queue data for this task. */
static i2s_task_q_data_t i2s_task_q_data;

mtb_hal_i2c_t MW_I2C_hal_obj;

cy_stc_scb_i2c_context_t MW_I2C0_context;

mtb_hal_i2c_cfg_t i2c_config =
{
    .is_target = MTB_HAL_I2C_MODE_CONTROLLER,
    .address = I2C_ADDRESS,
    .frequency_hz = I2C_FREQUENCY_HZ,
    .address_mask = MTB_HAL_I2C_DEFAULT_ADDR_MASK,
    .enable_address_callback = false
};

const cy_stc_sysint_t i2s_isr_txcfg = {
    .intrSrc = (IRQn_Type) tdm_0_interrupts_tx_0_IRQn,
    .intrPriority = 3,
};

/* Flag to maintain status of initial prefill for buffering. */
static volatile bool wait_for_prefill = true;
static volatile bool i2s_init_complete = false;
static volatile bool i2s_tx_complete = false;
static volatile bool i2s_tx_active = false;
static uint32_t i2s_tx_num_samples = 0;
static int16_t* temp_i2s_tx_buffer;
static int32_t  watermark_level_average = 0;
static int32_t  watermark_level = 0;
static uint32_t watermark_monitor_count = 0;
static int32_t  cdps = 0;

int32_t i2s_task_q_num_samples = 0;

/*******************************************************************************
* Function Prototypes
********************************************************************************/
static void i2s_isr_handler(void);
void tlv_codec_i2c_init(void);
/******************************************************************************
 *                          Function Definitions
 ******************************************************************************/

/*******************************************************************************
* Function Name: i2s_init
********************************************************************************
* Summary:
*  This function handles Hardware codec Init
*
* Parameters:
*  None
*
* Return:
*  None
*
*******************************************************************************/
void i2s_init(void)
{
    /* Create a message queue to communicate data / commands between various tasks
     * and callbacks.
     */
    i2s_task_q = xQueueCreate(I2S_TASK_QUEUE_LENGTH, sizeof(i2s_task_q_data_t));

    i2s_task_q_num_samples = 0;

    hw_codec_init();

    i2s_tx_complete = true;
    i2s_tx_active = false;

    temp_i2s_tx_buffer = NULL;
}

/*******************************************************************************
* Function Name: i2s_isr_handler
********************************************************************************
* Summary:
*  I2S ISR handler.
*
* Parameters:
* None
*
* Return:
* None
*******************************************************************************/
void i2s_isr_handler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* Get interrupt status and check for tigger interrupt and errors */
    uint32_t intr_status = Cy_AudioTDM_GetTxInterruptStatusMasked(TDM_STRUCT0_TX);

    i2s_tx_complete = true;

    if(CY_TDM_INTR_TX_FIFO_TRIGGER & intr_status)
    {
        /* If prefill is enabled, wait until the required number of queue items
                 * are present before first playback.
                 */
        if (wait_for_prefill)
        {
            if (I2S_TX_INITIAL_FRAME_DELAY > uxQueueMessagesWaitingFromISR(i2s_task_q))
            {
                for (uint32_t i = 0; i < (HW_FIFO_HALF_SIZE / 2); i++)
                {
                    Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, 0UL);
                    Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, 0UL);
                }
            }
            else
            {
                wait_for_prefill = false;
            }
        }
        else
        {
                /* Receive the data from the queue and process it. */
            if (pdTRUE == xQueueReceiveFromISR(i2s_task_q, &i2s_task_q_data,
                                                       &xHigherPriorityTaskWoken))
            {
                temp_i2s_tx_buffer = (int16_t *) i2s_task_q_data.data;
                i2s_tx_num_samples = (i2s_task_q_data.data_len / sizeof(int16_t));
                i2s_task_q_num_samples -= i2s_tx_num_samples;
                watermark_level = i2s_task_q_num_samples / 32;
                watermark_level -= (I2S_TX_INITIAL_FRAME_DELAY - 1);
                watermark_level_average += watermark_level;

                watermark_monitor_count ++;
                if (watermark_monitor_count >= I2S_WATERMARK_MONITOR_COUNT)
                {
                    watermark_monitor_count = 0;
                    watermark_level_average /= ((int32_t)I2S_WATERMARK_MONITOR_COUNT);
                    if (watermark_level_average == 0)
                    {
                        cdps = 0 * (1 << Q_CDPS);
                        IFX_SetClockDrift(&asrc_mem, cdps);
                    }
                    else if (watermark_level_average > 0)
                    {
                        cdps = -60 * (1 << Q_CDPS);
                        cdps &= 0xFFFFFFFE;
                        IFX_SetClockDrift(&asrc_mem, cdps);
                    }
                    else
                    {
                        cdps = 30 * (1 << Q_CDPS);
                        cdps &= 0xFFFFFFFE;
                        IFX_SetClockDrift(&asrc_mem, cdps);
                    }
                }

                for(uint32_t j = 0; j < i2s_tx_num_samples; j ++)
                {
                    Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, (uint32_t)temp_i2s_tx_buffer[j]);
                    Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, (uint32_t)temp_i2s_tx_buffer[j]);
                }
                vPortFree(temp_i2s_tx_buffer);
                portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

            }
            else
            {
                for (uint32_t i = 0; i < (HW_FIFO_HALF_SIZE / 2); i++)
                {
                    Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, 0UL);
                    Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, 0UL);
                }
            }

        }
    }
    else if(CY_TDM_INTR_TX_FIFO_UNDERFLOW & intr_status)
    {
        printf(">> Error: I2S transmit underflowed\r\n");
    }

    /* Clear all TX I2S Interrupt */
    Cy_AudioTDM_ClearTxInterrupt(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);

 }

/*******************************************************************************
 * Function Name: app_i2s_init
 ********************************************************************************
* Summary: Initialize I2S interrupt and I2S
*
* Parameters:
*  None
*
* Return:
*  None
*
*******************************************************************************/
void app_i2s_init(void)
{
    /* Initialize the I2S interrupt */
    Cy_SysInt_Init(&i2s_isr_txcfg, i2s_isr_handler);

   /* Initialize the I2S */
    cy_en_tdm_status_t volatile return_status = Cy_AudioTDM_Init(TDM_STRUCT0, &CYBSP_TDM_CONTROLLER_0_config);
    if (CY_TDM_SUCCESS != return_status)
    {
        CY_ASSERT(0);
    }

    /* Clear TX interrupts */
    Cy_AudioTDM_ClearTxInterrupt(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);
    Cy_AudioTDM_SetTxInterruptMask(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);
}

/*******************************************************************************
* Function Name: hw_codec_init
********************************************************************************
* Summary:
*  Initialize the I2S, TLV and HW codec.
*
* Parameters:
*  None
*
* Return:
*  None
*
*******************************************************************************/
void hw_codec_init(void)
{
    /* Initialize the MCLK Pin */
   (void)Cy_GPIO_Pin_FastInit(GPIO_PRT21, 3U, CY_GPIO_DM_STRONG_IN_OFF, 0x00U, P21_3_TDM_TDM_TX_MCK0);

    /* Initialize the I2S */
    app_i2s_init();

    i2s_init_complete = true;
    wait_for_prefill = true;

    /* Initialize I2C used to configure TLV codec */
    tlv_codec_i2c_init();

    /* TLV codec/ MW init */
    mtb_tlv320dac3100_init(&MW_I2C_hal_obj);

    /* Configure internal clock dividers to achieve desired sample rate */
    mtb_tlv320dac3100_configure_clocking(MCLK_HZ, SAMPLE_RATE_HZ, I2S_WORD_LENGTH,TLV320DAC3100_SPK_AUDIO_OUTPUT);

    /* Activate TLV320DAC3100 */
    mtb_tlv320dac3100_activate();


}


/*******************************************************************************
 * Function Name: tlv_codec_i2c_init
 ********************************************************************************
* Summary: Initialize I2C used for the TLV codec configurations
*
* Parameters:
*  None
*
* Return:
*  None
*
*******************************************************************************/
void tlv_codec_i2c_init(void)
{
    cy_en_scb_i2c_status_t result;
    cy_rslt_t hal_result;

    /* Initialize and enable the I2C*/
    result = Cy_SCB_I2C_Init(CYBSP_I2C_CONTROLLER_HW, &CYBSP_I2C_CONTROLLER_config, &MW_I2C0_context);
    if(result != CY_SCB_I2C_SUCCESS)
    {
        CY_ASSERT(0);
    }
    /* Enable I2C hardware. */
    Cy_SCB_I2C_Enable(CYBSP_I2C_CONTROLLER_HW);

    /* I2C HAL init */
    hal_result = mtb_hal_i2c_setup(&MW_I2C_hal_obj, &CYBSP_I2C_CONTROLLER_hal_config, &MW_I2C0_context, NULL);
    if (CY_RSLT_SUCCESS != hal_result)
    {
        CY_ASSERT(0);
    }

    /* Configure the I2C block.*/
    hal_result = mtb_hal_i2c_configure(&MW_I2C_hal_obj, &i2c_config);
    if (CY_RSLT_SUCCESS != hal_result)
    {
        CY_ASSERT(0);
    }
}


/*******************************************************************************
* Function Name: start_i2s
********************************************************************************
* Summary:
*  Start I2S
*
* Parameters:
*  None
*
* Return:
*  void
*
*******************************************************************************/
void start_i2s(void)
{
    if (i2s_init_complete)
    {
        if (!i2s_tx_active)
        {
            /* Start the I2S TX */
            Cy_AudioTDM_EnableTx(TDM_STRUCT0_TX);

            /* Clear TX interrupts */
            Cy_AudioTDM_ClearTxInterrupt(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);
            Cy_AudioTDM_SetTxInterruptMask(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);

            for(uint32_t i = 0; i < (64 / 2); i++)
            {
                /* Write data in FIFO */
                Cy_AudioTDM_WriteTxData(TDM0_TDM_STRUCT0_TDM_TX_STRUCT, 0UL);
                Cy_AudioTDM_WriteTxData(TDM0_TDM_STRUCT0_TDM_TX_STRUCT, 0UL);
            }

            NVIC_EnableIRQ(i2s_isr_txcfg.intrSrc);

            /* Activate and enable I2S TX interrupts */
            Cy_AudioTDM_ActivateTx(TDM_STRUCT0_TX);

            i2s_tx_active = true;
        }
    }
}

/*******************************************************************************
* Function Name: stop_i2s
********************************************************************************
* Summary:
*  Stop I2S
**
* Parameters:
*  None
*
* Return:
*  void
*
*******************************************************************************/
void stop_i2s(void)
{
    if (i2s_init_complete)
    {
        if (i2s_tx_active)
        {
            /* To clear FIFO there is no direct way so Disable and Enable Tx, FIFO will be cleared as a side effect */
            NVIC_DisableIRQ(i2s_isr_txcfg.intrSrc);

            Cy_AudioTDM_DeActivateTx(TDM_STRUCT0_TX);
            Cy_AudioI2S_DisableTx(TDM_STRUCT0_TX);
            Cy_AudioI2S_EnableTx(TDM_STRUCT0_TX);
            Cy_AudioI2S_DisableTx(TDM_STRUCT0_TX);

            i2s_tx_active = false;

            if (pdTRUE != xQueueReset(i2s_task_q))
            {
                CY_ASSERT(0);
            }

            i2s_task_q_num_samples = 0;
            watermark_level_average = 0;
        }
        i2s_tx_complete = true;
    }
}


/* [] END OF FILE */
