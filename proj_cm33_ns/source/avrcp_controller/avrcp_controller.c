/******************************************************************************
* File Name:   avrcp_sink.c
*
* Description: Source file for AVRCP controller and target logic.
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
#include <stdio.h>
#include "avrcp_controller.h"
#include "mtb_tlv320dac3100.h"
/******************************************************************************
 * MACROS
 ******************************************************************************/
#ifdef WICED_BT_TRACE
    #undef WICED_BT_TRACE
#endif

#define WICED_BT_TRACE                      printf
#define VOLUME_MAX                          (100U)
#define NO_GRP_NAV_VENDOR                   (0U)
/******************************************************************************
 *                                GLOBAL VARIABLES
 ******************************************************************************/
static bool is_playing = false;
static uint8_t volume_handle = 0;
static uint8_t volume_percentage = 50;
uint8_t avrcp_handle;

static uint8_t avrc_ct_supported_events[] =
{
    WICED_FALSE,
    WICED_TRUE,            /* AVRC_EVT_PLAY_STATUS_CHANGE             0x01    Playback Status Changed */
    WICED_TRUE,            /* AVRC_EVT_TRACK_CHANGE                   0x02    Track Changed */
    WICED_TRUE,            /* AVRC_EVT_TRACK_REACHED_END              0x03    Track End Reached */
    WICED_TRUE,            /* AVRC_EVT_TRACK_REACHED_START            0x04    Track Reached Start */
    WICED_TRUE,            /* AVRC_EVT_PLAY_POS_CHANGED               0x05    Playback position changed */
    WICED_FALSE,           /* AVRC_EVT_BATTERY_STATUS_CHANGE          0x06    Battery status changed */
    WICED_FALSE,           /* AVRC_EVT_SYSTEM_STATUS_CHANGE           0x07    System status changed */
    WICED_TRUE,            /* AVRC_EVT_APP_SETTING_CHANGE             0x08    Player application settings changed */
    WICED_FALSE,           /* AVRC_EVT_NOW_PLAYING_CHANGE             0x09    Now Playing Content Changed (AVRCP 1.4) */
    WICED_FALSE,           /* AVRC_EVT_AVAL_PLAYERS_CHANGE            0x0a    Available Players Changed Notification (AVRCP 1.4) */
    WICED_FALSE,           /* AVRC_EVT_ADDR_PLAYER_CHANGE             0x0b    Addressed Player Changed Notification (AVRCP 1.4) */
    WICED_FALSE,           /* AVRC_EVT_UIDS_CHANGE                    0x0c    UIDs Changed Notification (AVRCP 1.4) */
    WICED_TRUE             /* AVRC_EVT_VOLUME_CHANGE                  0x0d    Notify Volume Change (AVRCP 1.4) */
};

/******************************************************************************
 *                          Function Declarations
 ******************************************************************************/
static void avrc_connection_cb(uint8_t handle,
                        wiced_bt_device_address_t bda,
                        wiced_result_t result,
                        wiced_bt_avrc_ct_connection_state_t state,
                        uint32_t peer_features);
static void avrc_cmd_cb(uint8_t handle, wiced_bt_avrc_metadata_cmd_t* p_cmd);
static void avrc_rsp_cb(uint8_t handle, wiced_bt_avrc_rsp_t* p_rsp);
static void avrc_passthrough_rsp_cb(uint8_t handle,
                             wiced_bt_avrc_ctype_t ctype,
                             wiced_bt_avrc_pass_thru_hdr_t *avrc_pass_rsp);
static void avrcp_features_callback (wiced_bt_avrc_ct_features_event_t event, 
                            wiced_bt_avrc_ct_features_data_t *p_data);

/******************************************************************************
 *                          Function Definitions
 ******************************************************************************/

 /*******************************************************************************
 * Function Name: avrc_initialize
 ********************************************************************************
 * Summary:
 *     Initializes AVRCP
 *
 * Parameters:
 *     void
 *
 * Return:
 *     void
 ********************************************************************************/
void avrc_initialize(void)
{
    wiced_result_t result;

    result = wiced_bt_avrc_ct_init(
        REMOTE_CONTROL_FEATURE_CONTROLLER | REMOTE_CONTROL_FEATURE_TARGET,
        avrc_ct_supported_events,
        avrc_connection_cb,
        avrc_cmd_cb,
        avrc_rsp_cb,
        avrc_passthrough_rsp_cb
    );

    if (WICED_SUCCESS != result)
    {
        WICED_BT_TRACE("AVRC CT Init failed: %d\n", result);
    }

    wiced_bt_avrc_ct_features_register(avrcp_features_callback );
}

/*******************************************************************************
 * Function Name: avrc_send_command
 ********************************************************************************
 * Summary:
 *     Sends a AVRCP passthrough command using the given handle and operation ID.
 *     It sends both PRESS and RELEASE commands to complete the AVRCP action.
 *
 * Parameters:
 *     uint8_t handle : AVRCP connection handle
 *     uint8_t op_id  : Operation ID for the AVRCP command
 *
 * Return:
 *     void
 ********************************************************************************/
void avrc_send_command(uint8_t handle, uint8_t op_id)
{
    wiced_bt_avrc_ct_send_pass_through_cmd(handle, op_id, AVRC_STATE_PRESS, NO_GRP_NAV_VENDOR);
    wiced_bt_avrc_ct_send_pass_through_cmd(handle, op_id, AVRC_STATE_RELEASE, NO_GRP_NAV_VENDOR);
}

/*******************************************************************************
 * Function Name: avrc_is_playing
 ********************************************************************************
 * Summary:
 *     Checks whether AVRCP playback is currently active.
 *     Useful for determining if media is currently playing.
 *
 * Parameters:
 *     void
 *
 * Return:
 *     bool : true if media is playing, false otherwise
 ********************************************************************************/
bool avrc_is_playing(void)
{
    return is_playing;
}

/*******************************************************************************
 * Function Name: avrc_set_is_playing
 ********************************************************************************
 * Summary:
 *     Sets the internal AVRCP playback state.
 *     Can be used to manually update whether media is currently playing.
 *
 * Parameters:
 *     bool state : true to indicate playing, false for paused/stopped
 *
 * Return:
 *     void
 ********************************************************************************/
void avrc_set_is_playing(bool state)
{
    is_playing = state;
}

/*******************************************************************************
 * Function Name: avrc_get_volume
 ********************************************************************************
 * Summary:
 *     Retrieves the current volume level as a percentage.
 *
 * Parameters:
 *     void
 *
 * Return:
 *     uint8_t : Volume level (0 to 100)
 ********************************************************************************/
uint8_t avrc_get_volume(void)
{
    return volume_percentage;
}

/*******************************************************************************
 * Function Name: avrc_set_volume
 ********************************************************************************
 * Summary:
 *     Sets the volume level and sends an AVRCP volume command.
 *     Clips the value to the 0–100 range and maps it to 0–127 for AVRCP.
 *
 * Parameters:
 *     int8_t volume : Desired volume level (expected range: 0–100)
 *
 * Return:
 *     void
 ********************************************************************************/
void avrc_set_volume(uint8_t volume)
{

    if(volume > VOLUME_MAX)
    {
        volume = VOLUME_MAX;
    }

    volume_percentage = volume;

    WICED_BT_TRACE("AVRC ACTION: Setting volume: %d%%\n", volume_percentage);
    wiced_bt_avrc_ct_set_volume_cmd(volume_handle, ((uint32_t) volume_percentage) * 127 / 100);
    mtb_tlv320dac3100_adjust_speaker_output_volume(((uint32_t) volume_percentage) * 127 / 100);
}

/*******************************************************************************
* Function Name: avrc_connection_cb
********************************************************************************
* Summary:
*  AVRCP connection state callback. Called by the AVRCP controller when the
*  connection state with a peer device changes (connected, disconnected,
*  initialized, browse connected/disconnected). Logs the state and any
*  relevant information such as the connection handle and peer feature flags.
*
* Parameters:
*  uint8_t handle                                - AVRCP connection handle
*  wiced_bt_device_address_t bda                 - Bluetooth device address of peer
*  wiced_result_t result                         - Result code for the connection event
*  wiced_bt_avrc_ct_connection_state_t state     - Connection state enum
*  uint32_t peer_features                        - Bitmask of peer AVRCP features
*
* Return:
*  void
*******************************************************************************/
static void avrc_connection_cb(uint8_t handle, wiced_bt_device_address_t bda, wiced_result_t result,
                        wiced_bt_avrc_ct_connection_state_t state, uint32_t peer_features)
{
    switch (state)
    {
        case REMOTE_CONTROL_DISCONNECTED:
            avrcp_handle = handle;
            WICED_BT_TRACE("AVRC CT Disconnected: handle=%d, result=0x%x\n", handle, result);
            break;

        case REMOTE_CONTROL_CONNECTED:
            avrcp_handle = handle;
            WICED_BT_TRACE("AVRC CT Connected: handle=%d\n", handle);
            break;

        case REMOTE_CONTROL_INITIALIZED:
            WICED_BT_TRACE("AVRC CT Initialized: handle=%d\n", handle);
            break;

        case REMOTE_CONTROL_BROWSE_DISCONNECTED:
            WICED_BT_TRACE("AVRC Browse Disconnected: handle=%d\n", handle);
            break;

        case REMOTE_CONTROL_BROWSE_CONNECTED:
            WICED_BT_TRACE("AVRC Browse Connected: handle=%d\n", handle);
            break;

        default:
            WICED_BT_TRACE("AVRC CT Unknown State: %d, handle=%d\n", state, handle);
            break;
    }
}

/*******************************************************************************
* Function Name: avrc_rsp_cb
********************************************************************************
* Summary:
*  AVRCP controller response callback. Called when the AVRCP controller
*  receives a response from the peer device (e.g. notifications or metadata
*  responses). This handler inspects the response and logs/handles relevant
*  PDUs such as absolute volume change notifications.
*
* Parameters:
*  uint8_t handle               - AVRCP connection handle
*  wiced_bt_avrc_rsp_t *p_rsp   - Pointer to the AVRCP response structure
*
* Return:
*  void
*******************************************************************************/
static void avrc_rsp_cb(uint8_t handle, wiced_bt_avrc_rsp_t* p_rsp)
{
    switch (p_rsp->type.metadata.metadata_hdr.pdu)
    {
        case AVRC_PDU_REGISTER_NOTIFICATION:
            if (p_rsp->type.metadata.u.reg_notif.event_id == AVRC_EVT_PLAY_STATUS_CHANGE)
            {
                if(p_rsp->type.metadata.u.reg_notif.param.play_status == AVRC_PLAYSTATE_PAUSED)
                {
                    WICED_BT_TRACE("AVRC EVENT: Track paused\n");
                    avrc_set_is_playing(false);
                }
                else if(p_rsp->type.metadata.u.reg_notif.param.play_status == AVRC_PLAYSTATE_PLAYING)
                {
                    WICED_BT_TRACE("AVRC EVENT: Track played\n");
                    avrc_set_is_playing(true);
                }
            }
            break;

       default:
           break;
    }
}

/*******************************************************************************
* Function Name: avrc_cmd_cb
********************************************************************************
* Summary:
*  AVRCP controller command callback. Called when the local AVRCP controller
*  receives a metadata command (PDU) from the peer. This function inspects the
*  PDU and logs basic information (e.g. GET_CAPABILITIES, REGISTER_NOTIFICATION).
*
* Parameters:
*  uint8_t handle                       - AVRCP connection handle
*  wiced_bt_avrc_metadata_cmd_t *p_cmd  - Pointer to the metadata command
*
* Return:
*  void
*******************************************************************************/
static void avrc_cmd_cb(uint8_t handle, wiced_bt_avrc_metadata_cmd_t* p_cmd)
{
   switch (p_cmd->metadata_hdr.pdu)
   {
       case AVRC_PDU_SET_ABSOLUTE_VOLUME:
           volume_handle = handle;
           volume_percentage = p_cmd->u.volume * 100 /127;
           mtb_tlv320dac3100_adjust_speaker_output_volume(((uint32_t) volume_percentage) * 127 / 100);
           WICED_BT_TRACE("AVRC CMD: Setting volume: %d\n", volume_percentage);
           break;

       default:
           break;
   }
}

/*******************************************************************************
* Function Name: avrc_passthrough_rsp_cb
********************************************************************************
* Summary:
*  AVRCP passthrough response callback. Called when the remote peer responds
*  to a passthrough (play/pause/volume/track) command. Logs the operation and
*  whether the command was accepted or rejected.
*
* Parameters:
*  uint8_t handle                             - AVRCP connection handle
*  wiced_bt_avrc_ctype_t ctype                - Response type (accept/reject)
*  wiced_bt_avrc_pass_thru_hdr_t *avrc_pass_rsp- Pointer to passthru response header
*
* Return:
*  void
*******************************************************************************/
static void avrc_passthrough_rsp_cb(uint8_t handle, wiced_bt_avrc_ctype_t ctype, wiced_bt_avrc_pass_thru_hdr_t *avrc_pass_rsp)
{
    uint8_t msgAccepted = ctype;
    bool validOperationId = true;
    WICED_BT_TRACE("AVRC EVENT:");

    switch (avrc_pass_rsp->operation_id)
    {
        case AVRC_ID_PLAY:
            WICED_BT_TRACE("TRACK PLAY");
            break;

        case AVRC_ID_PAUSE:
            WICED_BT_TRACE("TRACK PAUSE");
            break;

        case AVRC_ID_VOL_UP:
            WICED_BT_TRACE("VOLUME UP");
            break;

        case AVRC_ID_VOL_DOWN:
            WICED_BT_TRACE("VOLUME DOWN");
            break;

        case AVRC_ID_FORWARD:
            WICED_BT_TRACE("NEXT TRACK");
            break;

        case AVRC_ID_BACKWARD:
            WICED_BT_TRACE("PREVIOUS TRACK");
            break;

        default:
            WICED_BT_TRACE("Unknown passthrough op_id=0x%x\n", avrc_pass_rsp->operation_id);
            validOperationId = false;
            break;
    }

    if(validOperationId && (AVRC_RSP_ACCEPT == msgAccepted))
    {
        WICED_BT_TRACE(" action acknowledged and accepted by remote end\n");
    }
    else if(validOperationId && (AVRC_RSP_REJ == msgAccepted))
    {
        WICED_BT_TRACE(" action acknowledged but rejected by remote end\n");
    }
}

/*******************************************************************************
* Function Name: avrcp_features_callback
********************************************************************************
* Summary:
*  Callback invoked when AVRCP controller feature information is reported.
*  Example: indicates if the peer supports Absolute Volume.
*
* Parameters:
*  wiced_bt_avrc_ct_features_event_t event         - Feature event type
*  wiced_bt_avrc_ct_features_data_t *p_data       - Pointer to feature data
*
* Return:
*  void
*******************************************************************************/
static void avrcp_features_callback (wiced_bt_avrc_ct_features_event_t event,
                              wiced_bt_avrc_ct_features_data_t *p_data)
{
    switch(event)
    {
        case WICED_BT_AVRC_CT_FEATURES_ABS_VOL_SUPPORTED:
            /* Event indicating if the peer device supports Absolute Volume */
            WICED_BT_TRACE("AVRC EVENT: AbsVolSupported by remote end? %s\n",
                    p_data->abs_vol_supported.supported ? "YES" : "NO");
            break;

        default:
            break;
     }
}

/* [] END OF FILE */

