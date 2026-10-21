/******************************************************************************
* File Name:   a2dp_sink.h
*
* Description: Header file for A2DP sink task.
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
#ifndef A2DP_SINK_H
#define A2DP_SINK_H
/******************************************************************************
 *                                INCLUDES
 *****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include "string.h"
#include "wiced_bt_stack.h"
#include "wiced_memory.h"
#include "wiced_timer.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_types.h"
#include "wiced_bt_l2c.h"
#include "wiced_bt_ble.h"
#include "wiced_bt_a2d.h"
#include "wiced_bt_a2d_sbc.h"
#include "wiced_bt_a2dp_sink.h"
#include "wiced_bt_a2dp_defs.h"
#include "wiced_bt_trace.h"
#include "hcidefs.h"
#include "wiced_bt_gatt.h"
#include "wiced_bt_cfg.h"
#include "wiced_bt_uuid.h"
#include "wiced_result.h"
#include "wiced_timer.h"
#include "app_bt_utils.h"
#include "wiced_bt_avrc_ct.h"

/******************************************************************************
 *                                MACROS
 *****************************************************************************/
#define A2DP_PEER_DISCONNECTED          (0U)
#define A2DP_PEER_CONNECTED             (1U)
#define A2DP_PEER_MAINTAIN_STATUS       (2U)

/******************************************************************************
 *                        STRUCTURES AND ENUMERATIONS
 ******************************************************************************/

typedef enum
{
    AV_STATE_IDLE, /* Initial state (channel is unused) */
    AV_STATE_CONFIGURED, /* Remote has sent configuration request */
    AV_STATE_CONNECTED, /* Signaling Channel is connected and active */
    AV_STATE_STARTED, /* Data streaming */
} AV_STATE;

typedef enum
{
    AV_STREAM_STATE_STOPPED,
    AV_STREAM_STATE_STARTING,
    AV_STREAM_STATE_STARTED,
    AV_STREAM_STATE_STOPPING
} AV_STREAM_STATE;

typedef struct
{
    wiced_bt_device_address_t peerBda; /* Peer bd address */
    AV_STATE state; /* AVDT State machine state */
    AV_STREAM_STATE audio_stream_state; /* Audio Streaming to host state */
    wiced_bt_a2dp_codec_info_t codec_config; /* Codec configuration information */
} tAV_APP_CB;

/****************************************************************************
 *                              FUNCTION DECLARATIONS
 ***************************************************************************/

uint16_t a2dp_sink_write_nvram (int nvram_id, int data_len, void *p_data);
uint16_t a2dp_sink_read_nvram (int nvram_id, void *p_data, int data_len);
wiced_result_t a2dp_sink_management_callback (
        wiced_bt_management_evt_t event,
        wiced_bt_management_evt_data_t *p_event_data);
void a2dp_sink_app_init( void );
void a2dp_sink_write_eir( void );
void a2dp_sink_control_cback (wiced_bt_a2dp_sink_event_t event,
                              wiced_bt_a2dp_sink_event_data_t *p_data);
void a2dp_sink_data_cback (uint8_t *p_rx_media, uint32_t media_len);
void avrc_connection_cb(uint8_t handle, wiced_bt_device_address_t bda, wiced_result_t result,
                        wiced_bt_avrc_ct_connection_state_t state, uint32_t peer_features);
void avrc_cmd_cb(uint8_t handle, wiced_bt_avrc_metadata_cmd_t* p_cmd);
void avrc_rsp_cb(uint8_t handle, wiced_bt_avrc_rsp_t* p_rsp);
void avrc_passthrough_rsp_cb(uint8_t handle, wiced_bt_avrc_ctype_t ctype,
                             wiced_bt_avrc_pass_thru_hdr_t *avrc_pass_rsp);
#endif  /* A2DP_APP_H */

/* [] END OF FILE */
