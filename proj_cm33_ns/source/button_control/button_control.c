
/******************************************************************************
* File Name:   user_button.c
*
* Description: Source file for button GPIO handling and logic.
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

/******************************************************************************
 *                                INCLUDES
 *****************************************************************************/
#include "button_control.h"
#include "avrcp_controller.h"
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#include "cybsp.h"
#include "retarget_io_init.h"
/******************************************************************************
 * MACROS
 ******************************************************************************/
#define SHORT_PRESS_THRESHOLD_TICKS       (10U)
#define LONG_PRESS_THRESHOLD_TICKS        (200U)
#define VERY_LONG_PRESS_THRESHOLD_TICKS   (1000U)

#define EVENT_BTN1_SHORT_PRESS            (1 << 0)
#define EVENT_BTN1_LONG_PRESS             (1 << 1)
#define EVENT_BTN1_VERY_LONG_PRESS        (1 << 2)
#define EVENT_BTN2_SHORT_PRESS            (1 << 3)
#define EVENT_BTN2_LONG_PRESS             (1 << 4)

#define USER_BTN1_ID                      (1U)
#define USER_BTN1_PORT_NUMBER             CYBSP_USER_BTN1_PORT
#define USER_BTN1_PIN_NUMBER              CYBSP_USER_BTN1_PIN

#define USER_BTN2_ID                      (2U)
#define USER_BTN2_PORT_NUMBER             CYBSP_USER_BTN2_PORT
#define USER_BTN2_PIN_NUMBER              CYBSP_USER_BTN2_PIN

#define VOLUME_CHANGE_STEP                (5U)
#define GPIO_INTERRUPT_PRIORITY           (5U)
#define BTN_TASK_PRIORITY                 (2U)
#define BTN_TASK_STACK_SIZE               (512U)

/******************************************************************************
 *                                GLOBAL VARIABLES
 ******************************************************************************/
EventGroupHandle_t button_event_group = NULL;
static volatile uint32_t btn1_press_tick = 0;
static volatile uint32_t btn2_press_tick = 0;
extern uint8_t avrcp_handle;
/******************************************************************************
 *                          Function Declarations
 ******************************************************************************/
static void handle_button_logic(uint8_t button);
static void button_interrupt_handler(void);

/******************************************************************************
 *                          Function Definitions
 ******************************************************************************/

/*******************************************************************************
* Function Name: user_button_init
********************************************************************************
*
* Summary:
*  Initialize the button with Interrupt
*
* Parameters:
*  None
*
* Return:
*  None
*
*******************************************************************************/
void user_button_init(void)
{
    BaseType_t rtos_result;

    button_event_group = xEventGroupCreate();

     /* Interrupt config structure */
    cy_stc_sysint_t btn1_cfg = {
        .intrSrc = CYBSP_USER_BTN1_IRQ,
        .intrPriority = GPIO_INTERRUPT_PRIORITY
    };
    cy_stc_sysint_t btn2_cfg = {
        .intrSrc = CYBSP_USER_BTN2_IRQ,
        .intrPriority = GPIO_INTERRUPT_PRIORITY
    };

    Cy_GPIO_ClearInterrupt(USER_BTN1_PORT_NUMBER, USER_BTN1_PIN_NUMBER);
    Cy_GPIO_ClearInterrupt(USER_BTN2_PORT_NUMBER, USER_BTN2_PIN_NUMBER);
    NVIC_ClearPendingIRQ(CYBSP_USER_BTN1_IRQ);
    NVIC_ClearPendingIRQ(CYBSP_USER_BTN2_IRQ);

    /* Initialize the interrupt and register interrupt callback */
    Cy_SysInt_Init(&btn1_cfg, button_interrupt_handler);
    Cy_SysInt_Init(&btn2_cfg, button_interrupt_handler);

    /* Enable the interrupt in the NVIC */
    NVIC_EnableIRQ(btn1_cfg.intrSrc);
    NVIC_EnableIRQ(btn2_cfg.intrSrc);

    rtos_result = xTaskCreate(button_event_task, "BtnTask", BTN_TASK_STACK_SIZE, NULL, BTN_TASK_PRIORITY, NULL);

    if( pdPASS != rtos_result )
    {
        printf("Failed to create button task.\r\n");
        handle_app_error();
    }

}

/*******************************************************************************
* Function Name: button_event_task
********************************************************************************
* Summary:
*  FreeRTOS task that waits for button press events signaled via an event group
*  and sends the appropriate AVRCP command based on the button action detected.
*
* Parameters:
*  pvParameters - Pointer to task parameters (unused in this implementation).
*
* Return:
*  None
*
*******************************************************************************/
void button_event_task(void *pvParameters)
{
    (void) pvParameters; /* unused */
    EventBits_t bits;
    bool is_playing = false;
    while (true)
    {
        bits = xEventGroupWaitBits(button_event_group,
                                   EVENT_BTN1_SHORT_PRESS | EVENT_BTN1_LONG_PRESS |
                                   EVENT_BTN2_SHORT_PRESS | EVENT_BTN2_LONG_PRESS |
                                   EVENT_BTN1_VERY_LONG_PRESS,
                                   pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & EVENT_BTN1_VERY_LONG_PRESS)
        {
            is_playing = avrc_is_playing();
            avrc_send_command(avrcp_handle, is_playing ? AVRC_ID_PAUSE : AVRC_ID_PLAY);
            avrc_set_is_playing(!is_playing);
        }
        if (bits & EVENT_BTN1_LONG_PRESS)
        {
            avrc_send_command(avrcp_handle, AVRC_ID_FORWARD);
        }
        if (bits & EVENT_BTN1_SHORT_PRESS)
        {
            avrc_set_volume(avrc_get_volume() + VOLUME_CHANGE_STEP);
        }
        if (bits & EVENT_BTN2_LONG_PRESS)
        {
            avrc_send_command(avrcp_handle, AVRC_ID_BACKWARD);
        }
        if (bits & EVENT_BTN2_SHORT_PRESS)
        {
            avrc_set_volume(avrc_get_volume() - VOLUME_CHANGE_STEP);
        }
    }
}

/*******************************************************************************
 * Function Name: handle_button_logic
 ********************************************************************************
 * Summary:
 *     Detects whether a button press is short or long and sets the
 *     corresponding event flag. The 'button' parameter identifies which button
 *     triggered the logic.
 *
 * Parameters:
 *     uint8_t button : Button identifier (USER_BTN1_ID or USER_BTN2_ID)
 *
 * Return:
 *     void
 ********************************************************************************/
static void handle_button_logic(uint8_t button)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    TickType_t now = xTaskGetTickCountFromISR();
    static volatile bool pressed = false;
    EventBits_t event = 0;

    if (button == USER_BTN1_ID)
    {
        if (false == Cy_GPIO_Read(USER_BTN1_PORT_NUMBER, USER_BTN1_PIN_NUMBER))
        {
            if (!pressed)
            {
                pressed = true;
                btn1_press_tick = now;
            }
        }
        else if (pressed)
        {
            pressed = false;
            TickType_t duration = now - btn1_press_tick;

            if(duration >= VERY_LONG_PRESS_THRESHOLD_TICKS)
            {
                event = EVENT_BTN1_VERY_LONG_PRESS;
            }
            else if(duration >= LONG_PRESS_THRESHOLD_TICKS)
            {
                event = EVENT_BTN1_LONG_PRESS;
            }
            else if(duration >= SHORT_PRESS_THRESHOLD_TICKS)
            {
                event = EVENT_BTN1_SHORT_PRESS;
            }
        }
    }
    else if (button == USER_BTN2_ID)
    {
        if (false == Cy_GPIO_Read(USER_BTN2_PORT_NUMBER, USER_BTN2_PIN_NUMBER))
        {
            if (!pressed)
            {
                pressed = true;
                btn2_press_tick = now;
            }
        }
        else if (pressed)
        {
            pressed = false;
            TickType_t duration = now - btn2_press_tick;

            if(duration >= LONG_PRESS_THRESHOLD_TICKS)
            {
                event = EVENT_BTN2_LONG_PRESS;
            }
            else if(duration >= SHORT_PRESS_THRESHOLD_TICKS)
            {
                event = EVENT_BTN2_SHORT_PRESS;
            }
        }

    }

    if (event != 0)
    {
        xEventGroupSetBitsFromISR(button_event_group, event, &xHigherPriorityTaskWoken);
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}


/*******************************************************************************
 * Function Name: button_interrupt_handler
 ********************************************************************************
 * Summary:
 *     GPIO interrupt handler to detect button interrupts and call logic handler
 *     for the appropriate button.
 *
 * Parameters:
 *     None
 *
 * Return:
 *     void
 ********************************************************************************/
static void button_interrupt_handler(void)
{
    if (Cy_GPIO_GetInterruptStatus(USER_BTN1_PORT_NUMBER, USER_BTN1_PIN_NUMBER))
    {
        Cy_GPIO_ClearInterrupt(USER_BTN1_PORT_NUMBER, USER_BTN1_PIN_NUMBER);
        handle_button_logic(USER_BTN1_ID);
    }

    if (Cy_GPIO_GetInterruptStatus(USER_BTN2_PORT_NUMBER, USER_BTN2_PIN_NUMBER))
    {
        Cy_GPIO_ClearInterrupt(USER_BTN2_PORT_NUMBER, USER_BTN2_PIN_NUMBER);
        handle_button_logic(USER_BTN2_ID);
    }
}

/* [] END OF FILE */

