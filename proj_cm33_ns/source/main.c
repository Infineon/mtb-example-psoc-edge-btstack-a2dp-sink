/*******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the PSoC Edge MCU: Bluetooth Classic
*              - A2DP Sink code example.
*
********************************************************************************
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
/******************************************************************************
 * INCLUDES
 *****************************************************************************/
#include "cybsp.h"
#include "mtb_hal.h"
#include "wiced_bt_trace.h"
#include "wiced_bt_cfg.h"
#include "app_bt_utils.h"
#include "a2dp_sink.h"
#include "cy_retarget_io.h"
#include "FreeRTOS.h"
#include "task.h"
#include "i2s_task.h"
#include "mtb_kvstore.h"
#include "retarget_io_init.h"
#include "app_bt_bonding.h"
#include "cyabs_rtos_impl.h"
#include "button_control.h"
/******************************************************************************
 * Macros
 *****************************************************************************/
/* The timeout value in microsecond used to wait for core to be booted */
#define CM55_BOOT_WAIT_TIME_US            (10U)
#define BT_STACK_HEAP_SIZE                (0x5000U)
#define BT_TASK_STACK_SIZE                (1024U)
#define BT_TASK_PRIORITY                  (configMAX_PRIORITIES - 5U)
#define TASK_DELAY_1S                     (1000U)
#define WIFI_PIN                          (6U)
#define KEY_RESET_VALUE                   (0U)

#define CLKHF3                            (3U)
#define CLKHF4                            (4U)
#define CLKHF5                            (5U)
#define CLKHF6                            (6U)
#define CLKHF8                            (8U)
#define CLKHF9                            (9U)
#define CLKHF12                           (12U)
#define CLKHF13                           (13U)
/* Enabling or disabling a MCWDT requires a wait time of upto 2 CLK_LF cycles
 * to come into effect. This wait time value will depend on the actual CLK_LF
 * frequency set by the BSP.
 */
#define LPTIMER_0_WAIT_TIME_USEC            (62U)

/* Define the LPTimer interrupt priority number. '1' implies highest priority.
 */
#define APP_LPTIMER_INTERRUPT_PRIORITY      (1U)

/******************************************************************************
 * Extern variables
 *****************************************************************************/
extern const wiced_bt_cfg_settings_t        a2dp_sink_cfg_settings;

/* ***************************************************************************
 * Global variables
 * **************************************************************************/
volatile bool timer_interrupt_flag = false;
wiced_bt_cfg_settings_t *wiced_bt_cfg_settings = NULL;
int8_t connection_status;

/* LPTimer HAL object */
static mtb_hal_lptimer_t lptimer_obj;

/* ***************************************************************************
 * Static variables
 * **************************************************************************/
static wiced_bt_heap_t *p_default_heap = NULL;
static TaskHandle_t bt_task_handle;

/******************************************************************************
 * Function definitions
 ******************************************************************************/
/*******************************************************************************
* Function Name: lptimer_interrupt_handler
********************************************************************************
* Summary:
* Interrupt handler function for LPTimer instance.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void lptimer_interrupt_handler(void)
{
    mtb_hal_lptimer_process_interrupt(&lptimer_obj);
}

/*******************************************************************************
* Function Name: setup_tickless_idle_timer
********************************************************************************
* Summary:
* 1. This function first configures and initializes an interrupt for LPTimer.
* 2. Then it initializes the LPTimer HAL object to be used in the RTOS
*    tickless idle mode implementation to allow the device enter deep sleep
*    when idle task runs. LPTIMER_0 instance is configured for CM33 CPU.
* 3. It then passes the LPTimer object to abstraction RTOS library that
*    implements tickless idle mode
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void setup_tickless_idle_timer(void)
{
    /* Interrupt configuration structure for LPTimer */
    cy_stc_sysint_t lptimer_intr_cfg =
    {
        .intrSrc = CYBSP_CM33_LPTIMER_0_IRQ,
        .intrPriority = APP_LPTIMER_INTERRUPT_PRIORITY
    };

    /* Initialize the LPTimer interrupt and specify the interrupt handler. */
    cy_en_sysint_status_t interrupt_init_status =
                                    Cy_SysInt_Init(&lptimer_intr_cfg,
                                                    lptimer_interrupt_handler);

    /* LPTimer interrupt initialization failed. Stop program execution. */
    if(CY_SYSINT_SUCCESS != interrupt_init_status)
    {
        handle_app_error();
    }

    /* Enable NVIC interrupt. */
    NVIC_EnableIRQ(lptimer_intr_cfg.intrSrc);

    /* Initialize the MCWDT block */
    cy_en_mcwdt_status_t mcwdt_init_status =
                                    Cy_MCWDT_Init(CYBSP_CM33_LPTIMER_0_HW,
                                                &CYBSP_CM33_LPTIMER_0_config);

    /* MCWDT initialization failed. Stop program execution. */
    if(CY_MCWDT_SUCCESS != mcwdt_init_status)
    {
        handle_app_error();
    }

    /* Enable MCWDT instance */
    Cy_MCWDT_Enable(CYBSP_CM33_LPTIMER_0_HW,
                    CY_MCWDT_CTR_Msk,
                    LPTIMER_0_WAIT_TIME_USEC);

    /* Setup LPTimer using the HAL object and desired configuration as defined
     * in the device configurator. */
    cy_rslt_t result = mtb_hal_lptimer_setup(&lptimer_obj,
                                            &CYBSP_CM33_LPTIMER_0_hal_config);

    /* LPTimer setup failed. Stop program execution. */
    if(CY_RSLT_SUCCESS != result)
    {
        handle_app_error();
    }

    /* Pass the LPTimer object to abstraction RTOS library that implements
     * tickless idle mode
     */
    cyabs_rtos_set_lptimer(&lptimer_obj);
}


/******************************************************************************
 * Function Name: application_start()
 *******************************************************************************
 * Summary:
 *          Application start to initialize stack and other required modules
 *
 * Parameters:
 *          None
 *
 * Return:
 *          None
 *
 ******************************************************************************/
void application_start(void *task_params)
{
    wiced_result_t wiced_result = WICED_BT_SUCCESS;

    printf("\x1b[2J\x1b[;H");
    printf( "\r\n********************************************************" );
    printf( "\r\n        PSOC EDGE MCU:BLUETOOTH - A2DP Sink          \r\n" );
    printf( "********************************************************\r\n" );

    /* Register call back and configuration with stack */
    wiced_result = wiced_bt_stack_init(a2dp_sink_management_callback, &a2dp_sink_cfg_settings);

    /* Check if stack initialization was successful */
    if (WICED_BT_SUCCESS == wiced_result)
    {
        /* Create a buffer heap, make it the default heap.  */
        p_default_heap = wiced_bt_create_heap("app", NULL, BT_STACK_HEAP_SIZE,
                NULL, WICED_TRUE);
    }

    if ((WICED_BT_SUCCESS == wiced_result) && (NULL != p_default_heap))
    {
        fprintf(stdout, "Bluetooth Stack Initialization Successful...\r\n");
    }
    else /* Exit App if stack init was not successful or heap creation failed */
    {
        fprintf(stderr, "Bluetooth Stack Initialization or heap creation failed!! Exiting App...\n\r");
         handle_app_error();
    }

    printf("Wait few seconds... Patch is getting downloaded\n\r");

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_1S));
    }
}

/******************************************************************************
 * Function Name: bt_task_create()
 *******************************************************************************
 * Summary:
 *           BT stack initialization function wrapper
 *
 * Parameters:
 *           None
 *
 * Return:
 *          None
 *
 ******************************************************************************/
void bt_task_create(void)
{
    BaseType_t status;
    status = xTaskCreate(application_start, "BT task", BT_TASK_STACK_SIZE, NULL, BT_TASK_PRIORITY, &bt_task_handle);
    if (status != pdPASS)
    {
        printf("Error in starting BT task \n");
        handle_app_error();
    }
}

/******************************************************************************
 * Function Name: main()
 *******************************************************************************
 * Summary:
 *          A2DP Sink application entry function
 *
 * Parameters:
 *          None
 *
 *
 * Return:
 *          int : Status code
 *
 ******************************************************************************/
int main(void)
{
    cy_rslt_t result;

    /* Initialize and Verify the BSP initialization */
    result = cybsp_init();
    CY_ASSERT(CY_RSLT_SUCCESS == result);

    /*Unused Clock Disabled */
    Cy_SysClk_ClkHfDisable(CLKHF3);
    Cy_SysClk_ClkHfDisable(CLKHF4);
    Cy_SysClk_ClkHfDisable(CLKHF5);
    Cy_SysClk_ClkHfDisable(CLKHF6);
    Cy_SysClk_ClkHfDisable(CLKHF8);
    Cy_SysClk_ClkHfDisable(CLKHF9);
    Cy_SysClk_ClkHfDisable(CLKHF12);
    Cy_SysClk_ClkHfDisable(CLKHF13);

    /* Setup the LPTimer instance for CM33 CPU. */
    setup_tickless_idle_timer();

    /* Initialize retarget-io middleware */
    init_retarget_io();

    /*Initialize the WIFI power pin*/
    Cy_GPIO_Pin_FastInit(GPIO_PRT11, WIFI_PIN, CY_GPIO_DM_STRONG_IN_OFF, TRUE, P11_6_GPIO);

    /* Disabling the1 WIFI Domain */
    Cy_GPIO_Write(GPIO_PRT11, WIFI_PIN, FALSE);

    app_kv_store_init();

    if(false == Cy_GPIO_Read(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_PIN))
    {
        /* Reset Kv-store library, this will clear the flash */
        result = mtb_kvstore_reset(&kvstore_obj);
        if(CY_RSLT_SUCCESS == result)
        {
            printf("\x1b[2J\x1b[;H");
            printf("Successfully reset kv-store library,"
                   "Please reset the device to generate new keys!\n");
        }
        else
        {
            printf("\x1b[2J\x1b[;H");
            printf("Failed to reset kv-store libray\n");
        }
        /* Clear peer link keys and identity keys structure */
        memset(&bond_info, KEY_RESET_VALUE, sizeof(bond_info));
        memset(&identity_keys, KEY_RESET_VALUE, sizeof(identity_keys));
        handle_app_error();
    }

    /* Create a BT task */
    bt_task_create();

    /* Initialize I2S Peripheral */
    i2s_init();

    /* Initialize USER button */
    user_button_init();

    /* Enable CM55. CY_CORTEX_M55_APPL_ADDR must be updated if CM55 memory layout is changed. */
    Cy_SysEnableCM55(MXCM55, CY_CM55_APP_BOOT_ADDR, CM55_BOOT_WAIT_TIME_US);

    /* Enable global interrupts */
    __enable_irq();
    
    /* System Domain Idle Power Mode Configuration */
    Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);

    /* SoCMEM Idle Power Mode Configuration */
    Cy_SysPm_SetSOCMEMDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP_OFF);

    /* Start the FreeRTOS scheduler */
    vTaskStartScheduler();

    /* Should never get here */
    handle_app_error();
}

/* [] END OF FILE */
