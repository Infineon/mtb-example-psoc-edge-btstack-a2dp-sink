/******************************************************************************
* File Name:   a2dp_sink.c
*
* Description: Source file for A2DP sink task.
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
#include "wiced_bt_a2dp_sink.h"
#include "sbc_decoder.h"
#include "sbc_dec_func_declare.h"
#include "sbc_dct.h"
#include "sbc_types.h"
#include "a2dp_sink.h"
#include "app_bt_bonding.h"
#include "i2s_task.h"
#include "IFX_asrc.h"
#include "avrcp_controller.h"
#include "wiced_bt_cfg.h"
/******************************************************************************
 * MACROS
 ******************************************************************************/
#ifdef WICED_BT_TRACE
    #undef WICED_BT_TRACE
#endif

#define WICED_BT_TRACE                      printf

#define WICED_HS_EIR_BUF_MAX_SIZE           (264U)

#define MSBC_STATIC_MEM_SIZE                (1920U) /* Bytes */
#define MSBC_SCRATCH_MEM_SIZE               (2048U) /* Bytes */

#define SBC_SYNC_BYTE                       (0x9C)

/* Channel Mode Values */
#define CH_MODE_MONO                        (0U)
#define CH_MODE_DUAL                        (1U)
#define CH_MODE_STEREO                      (2U)
#define CH_MODE_JOINT_STEREO                (3U)

#define CH_SINGLE                           (1U)
#define CH_DUAL                             (2U)

#define SHIFT_4                             (4U)
#define SHIFT_2                             (2U)

#define CH_MODE_MASK                        (0x0C)
#define BLOCK_MASK                          (0x30)
#define SUB_BAND_MASK                       (0x01)
#define BIT_POOL_MASK                       (0xff)

/* Audio Sample Rate in Hz*/
#define AUDIO_SAMPLE_RATE_48kHz             (48000U)
#define AUDIO_SAMPLE_RATE_44_1kHz           (44100U)
#define AUDIO_SAMPLE_RATE_32kHz             (32000U)
#define AUDIO_SAMPLE_RATE_16kHz             (16000U)
#define BUFFER_SIZE                         (2000U)
#define FIXED_PACKET_SAMPLES                (32U)
#define OUTPUT_BUFFER1_MAX                  (965U)

#define A2DP_SINK_FRAME_SAMPLES_PER_CHANNEL     (128u) /* Note that this is num of samples in a frame not packet. 1 packet contains 5 frames */
#define A2DP_SINK_FRAME_SIZE_PER_CHANNEL        (A2DP_SINK_FRAME_SAMPLES_PER_CHANNEL * sizeof(int16_t)) /* Note that this is frame size not packet size. 1 packet contains 5 frames */
#define I2S_TX_MAX_FRAME_SIZE_PER_CHANNEL       (10 * A2DP_SINK_FRAME_SAMPLES_PER_CHANNEL * sizeof(int16_t)) /* DECODED_PCM_FRAME_2CH_SIZE */

/******************************************************************************
 *                                GLOBAL VARIABLES
 ******************************************************************************/
uint8_t pincode[4] = { 0x30, 0x30, 0x30, 0x30 };
extern const uint8_t a2dp_sink_sdp_db[];
extern const wiced_bt_cfg_settings_t a2dp_sink_cfg_settings;
extern wiced_bt_device_address_t bt_device_address;
/* Stack configuration */
//extern const uint8_t                        a2dp_sink_sdp_db[A2DP_SINK_SDP_DB_SIZE];
//extern const wiced_bt_cfg_settings_t        a2dp_sink_cfg_settings;
extern wiced_bt_a2dp_config_data_t          bt_audio_config;

static SINT32 staticMem[MSBC_STATIC_MEM_SIZE / sizeof (SINT32)];
static SINT32 scratchMem[MSBC_SCRATCH_MEM_SIZE / sizeof (SINT32)];
static int              PcmBytesPerFrame;
static SBC_DEC_PARAMS   strDecParams = { 0 };
static unsigned long    audio_frames_to_write = 0;
static int16_t buffer[BUFFER_SIZE];
static uint32_t write_index = 0;
static uint8_t decoded_pcm_data[DECODED_PCM_BUFFER_SIZE] = {0};

IFX_ASRC_STRUCT_t    asrc_mem;
static uint32_t input_sampling_freq = 0;
static uint32_t output_sampling_freq = 0;
static uint16_t asrc_out_len = 0;

static uint8_t sbc_frame_decoded_pcm_data[2 * A2DP_SINK_FRAME_SIZE_PER_CHANNEL + 10u] = {0};
static int32_t asrc_in_i2s_tx_buffer[(A2DP_SINK_FRAME_SIZE_PER_CHANNEL / 2 + 10u)] = {0};
int32_t asrc_out_i2s_tx_buffer[OUTPUT_BUFFER1_MAX + 10u] = {0};
static i2s_task_q_data_t i2s_task_q_data;

static int16_t* frame_in_buffer;
static int16_t* frame_out_buffer;
static uint32_t i2s_tx_num_samples = 0;

long                    audio_frames_written = 0;
uint16_t                sample_rate = 0;
tAV_APP_CB              av_app_cb;
uint8_t  bondindex = 0;

uint8_t app_notify_client_char_config[] = {0x00, 0x00};

/******************************************************************************
 *                          Function Declarations
 ******************************************************************************/
void a2dp_sink_control_cback(wiced_bt_a2dp_sink_event_t event,
                             wiced_bt_a2dp_sink_event_data_t* p_data);
void a2dp_sink_app_init(void);
uint16_t wiced_app_cfg_sdp_record_get_size(void);
/******************************************************************************
 *                          Function Definitions
 ******************************************************************************/

/* ****************************************************************************
 * Function Name: get_control_event_name
 *******************************************************************************
 * Summary:
 *          Gives the string equivalent for the Event type
 *
 * Parameters:
 *          event - Control event called back
 *
 * Return:
 *          Pointer to String containing the Event Type
 *
 * ***************************************************************************/
static const char *get_control_event_name(wiced_bt_a2dp_sink_event_t event)
{
    switch((int)event)
    {
        CASE_RETURN_STR(WICED_BT_A2DP_SINK_CONNECT_EVT)
        CASE_RETURN_STR(WICED_BT_A2DP_SINK_DISCONNECT_EVT)
        CASE_RETURN_STR(WICED_BT_A2DP_SINK_START_IND_EVT)
        CASE_RETURN_STR(WICED_BT_A2DP_SINK_START_CFM_EVT)
        CASE_RETURN_STR(WICED_BT_A2DP_SINK_SUSPEND_EVT)
        CASE_RETURN_STR(WICED_BT_A2DP_SINK_CODEC_CONFIG_EVT)
    }

    return NULL;
}

/* ****************************************************************************
 * Function Name: get_state_name
 ******************************************************************************
 * Summary:
 *          Gives the string equivalent for the State
 *
 * Parameters:
 *          state - AV State
 *
 * Return:
 *          Pointer to String containing the State Name
 *
 * ***************************************************************************/
static const char *get_state_name(AV_STATE state)
{
    switch((int)state)
    {
        CASE_RETURN_STR(AV_STATE_IDLE)
        CASE_RETURN_STR(AV_STATE_CONFIGURED)
        CASE_RETURN_STR(AV_STATE_CONNECTED)
        CASE_RETURN_STR(AV_STATE_STARTED)
    }

    return NULL;
}

/* ****************************************************************************
 * Function Name: a2dp_sink_get_frame_len
 ******************************************************************************
 * Summary:
 *          Calculates the frame length based on the CODEC parameters chosen
 *
 * Parameters:
 *          pHeader: Header containing the SBC parameters
 *
 * Return:
 *          Frame Length
 *
 * ***************************************************************************/
static uint16_t a2dp_sink_get_frame_len(uint8_t *pHeader)
{
    uint8_t *pframeByte0;
    uint8_t *pframeByte1;
    uint8_t *pframeByte2;
    uint8_t  blocks;
    uint8_t  channelMode;
    uint8_t  subbands;
    uint8_t  bitpool;
    uint8_t  num_channels;
    uint8_t  join;
    uint32_t FrameLen;
    uint32_t tmp;

    pframeByte0 = pHeader;
    FrameLen = 0;

    if (*pframeByte0 != SBC_SYNC_BYTE)
    {
        WICED_BT_TRACE ("a2dp_sink_get_frame_len(): Bad Sync Byte: 0x%02x !!!", *pframeByte0);
        return (0);
    }

    pframeByte1 = pframeByte0 + 1;
    pframeByte2 = pframeByte1 + 1;

    /* Derive the Number of blocks and, frequency,
     * channel mode according to the Frame Header */

    /* Table 12.12: Syntax of frame_header from Advanced Audio Distribution /
     * Profile Specification
     *
     * frame_header()
     * {
     *    syncword                8
     *    sampling_frequency      2
     *    blocks                  2
     *    channel_mode            2
     *    allocation_method       1
     *    subbands                1
     *    bitpool                 8
     *    crc_check               8
     *    If (channel_mode==JOINT_STEREO)
     *    {
     *       for (sb=0;sb<nrof_subbands-1;sb++)
     *       {
     *           join[sb] 1 UiMsbf
     *       }
     *       RFA 1 UiMsbf
     *    }
     * }
     */

    blocks = (*(unsigned char *)pframeByte1 & BLOCK_MASK) >> SHIFT_4;

    /* Check and assign the SBC equivalent block alignment */
    switch (blocks)
    {
    case 0x0:
        blocks = SBC_BLOCK_0;
        break;
    case 0x1:
        blocks = SBC_BLOCK_1;
        break;
    case 0x2:
        blocks = SBC_BLOCK_2;
        break;
    case 0x3:
        blocks = SBC_BLOCK_3;
        break;
    }
    channelMode    = (*(unsigned char *)pframeByte1 & CH_MODE_MASK) >> SHIFT_2;
    num_channels = (channelMode == SBC_MONO) ? SBC_DUAL : SBC_STEREO;

    subbands    = (*(unsigned char *)pframeByte1 & SUB_BAND_MASK);
    subbands = (subbands == 0) ? SBC_SUB_BANDS_4 : SBC_SUB_BANDS_8;

    /* Refer to 12.9 Calculation of Bit Rate and Frame Length in
     * A2DP Specification A2DP_v1.3.2.pdf
     * for the formula and calculations
     */
    bitpool = (*(unsigned char *)pframeByte2 & BIT_POOL_MASK);

    FrameLen = 4 + (4 * subbands * num_channels) / 8;

    join = (channelMode == CH_MODE_JOINT_STEREO) ? CH_MODE_DUAL : CH_MODE_MONO;

    if (channelMode < 2) /* channel Mode is not Join */
    {
        tmp =  (blocks * num_channels * bitpool);
    }
    else
    {
        tmp =  (join * subbands) + (blocks * bitpool);
    }

    FrameLen += tmp / 8 + ((tmp % 8) ? 1 : 0);

    return ((uint16_t)FrameLen);
}


/* ****************************************************************************
 * Function Name: a2dp_audio_decoder_init
 ******************************************************************************
 * Summary:
 *          Initializes the SBC Decoder
 *
 * Parameters:
 *          None
 *
 * Return:
 *          None
 *
 * ***************************************************************************/
static void a2dp_audio_decoder_init(void)
{
    wiced_bt_a2dp_codec_info_t *codec_info = &av_app_cb.codec_config;

    WICED_BT_TRACE("a2dp_audio_decoder_init");

    memset (staticMem, 0, sizeof (staticMem));
    memset (scratchMem, 0, sizeof (scratchMem));
    memset (&strDecParams, 0, sizeof (strDecParams));

    /* Fill the Decoder parameters */
    strDecParams.s32StaticMem  = staticMem;
    strDecParams.s32ScratchMem = scratchMem;

    strDecParams.numOfBlocks = codec_info->cie.sbc.block_len;

    printf("  codec_info->cie.sbc.ch_mode = 0x%X\r\n", codec_info->cie.sbc.ch_mode);

    if ((codec_info->cie.sbc.ch_mode == A2D_SBC_IE_CH_MD_STEREO) ||
            (codec_info->cie.sbc.ch_mode == A2D_SBC_IE_CH_MD_JOINT))
    {
        strDecParams.numOfChannels = CH_DUAL;
    }
    else
    {
        strDecParams.numOfChannels = CH_SINGLE;
    }

    if (codec_info->cie.sbc.num_subbands == A2D_SBC_IE_SUBBAND_4)
    {
        strDecParams.numOfSubBands = SBC_SUB_BANDS_4;
    }
    else if (codec_info->cie.sbc.num_subbands == A2D_SBC_IE_SUBBAND_8)
    {
        strDecParams.numOfSubBands = SBC_SUB_BANDS_8;
    }

    if (codec_info->cie.sbc.alloc_mthd == A2D_SBC_IE_ALLOC_MD_S)
    {
        strDecParams.allocationMethod = SBC_SNR;
    }
    else if (codec_info->cie.sbc.alloc_mthd == A2D_SBC_IE_ALLOC_MD_L)
    {
        strDecParams.allocationMethod = SBC_LOUDNESS;
    }

    if (codec_info->cie.sbc.samp_freq == A2D_SBC_IE_SAMP_FREQ_44)
    {
        sample_rate = AUDIO_SAMPLE_RATE_44_1kHz;
    }
    else if (codec_info->cie.sbc.samp_freq == A2D_SBC_IE_SAMP_FREQ_48)
    {
        sample_rate = AUDIO_SAMPLE_RATE_48kHz;
    }
    else
    {
        sample_rate = AUDIO_SAMPLE_RATE_32kHz;
    }

    WICED_BT_TRACE("strDecParams.numOfBlocks = %d\r\n", strDecParams.numOfBlocks);
    WICED_BT_TRACE("strDecParams.numOfChannels = %d\r\n", strDecParams.numOfChannels);
    WICED_BT_TRACE("strDecParams.numOfSubBands = %d\r\n", strDecParams.numOfSubBands);
    WICED_BT_TRACE("strDecParams.allocationMethod = %d\r\n", strDecParams.allocationMethod);
    WICED_BT_TRACE("sample_rate = %d\r\n", sample_rate);
    WICED_BT_TRACE("strDecParams.numOfChannels = %d\r\n", strDecParams.numOfChannels);

    input_sampling_freq = I2S_SAMPLING_RATE;
    output_sampling_freq = I2S_SAMPLING_RATE;

    init_IFX_asrc(&asrc_mem, input_sampling_freq, output_sampling_freq);

    frame_out_buffer = (int16_t *) decoded_pcm_data;

    SBC_Decoder_decode_Init(&strDecParams);

    WICED_BT_TRACE("\r\nnblocks %d nchannels %d nsubbands %d ameth %d freq %d\r\n",
                   strDecParams.numOfBlocks , strDecParams.numOfChannels,
                   strDecParams.numOfSubBands, strDecParams.allocationMethod,
                   sample_rate);

    frame_out_buffer = (int16_t *)decoded_pcm_data;

    PcmBytesPerFrame = strDecParams.numOfBlocks * strDecParams.numOfChannels *
                        strDecParams.numOfSubBands * 2;

    /* Calculate the audio frames to write */
    audio_frames_to_write = PcmBytesPerFrame / strDecParams.numOfChannels;
    audio_frames_to_write /= 2; /* Bits per sample is 16 */

    WICED_BT_TRACE("PcmBytesPerFrame = %d\r\n", PcmBytesPerFrame);
    WICED_BT_TRACE("audio_frames_to_write = %lu\r\n", audio_frames_to_write);
}

/* ****************************************************************************
 * Function Name: a2dp_sink_control_cback
 ******************************************************************************
 * Summary:
 *          Control callback supplied by  the a2dp sink profile code.
 *
 * Parameters:
 *          event - control event called back
 *          p_data - event data
 *
 * Return:
 *          None
 *
 * ***************************************************************************/
void a2dp_sink_control_cback(wiced_bt_a2dp_sink_event_t event,
                             wiced_bt_a2dp_sink_event_data_t* p_data)
{
    WICED_BT_TRACE( "[%s] Event: (%d) %s state: (%d) %s\n\r", __FUNCTION__, event,
                    get_control_event_name(event),
                    av_app_cb.state, get_state_name(av_app_cb.state));

    switch(event)
    {
        case WICED_BT_A2DP_SINK_CODEC_CONFIG_EVT:
            /**< Codec config event, received when codec config for a streaming
             * session is updated */
            /* Maintain State */
            av_app_cb.state = AV_STATE_CONFIGURED;

            if (NULL == p_data)
            {
                WICED_BT_TRACE ("A2DP Sink Control Callback with event data pointer NULL \n");
                return;
            }
            /* Save the configuration to setup the decoder if necessary. */
            memcpy( &av_app_cb.codec_config, &p_data->codec_config.codec, sizeof( wiced_bt_a2dp_codec_info_t ) );

            WICED_BT_TRACE("a2dp sink codec configuration done\n");

            WICED_BT_TRACE("Initializing SBC Decoder ...\n");

            a2dp_audio_decoder_init();

            break;

        case WICED_BT_A2DP_SINK_CONNECT_EVT:
            if (NULL == p_data)
            {
                WICED_BT_TRACE ("A2DP Sink Control Callback with event data pointer NULL \n");
                return;
            }
            /**< Connected event, received on establishing connection to a peer
             * device. Ready to stream. */
            if (p_data->connect.result == WICED_SUCCESS)
            {

                WICED_BT_TRACE( "[%s] Connected : ", __FUNCTION__);
                print_bd_address(p_data->connect.bd_addr);

                /* Save the address of the remote device on remote connection */
                memcpy(av_app_cb.peerBda, p_data->connect.bd_addr, sizeof(wiced_bt_device_address_t));

                /* Maintain State */
                av_app_cb.state = AV_STATE_CONNECTED;

                WICED_BT_TRACE(" a2dp sink connected \n");

                /* Set the LED */
                Cy_GPIO_Set(CYBSP_USER_LED_PORT, CYBSP_LED_BLUE_NUM);
            }
            else
            {
                WICED_BT_TRACE("a2dp Connection failed \n");
            }
            break;

        case WICED_BT_A2DP_SINK_DISCONNECT_EVT:
            /**< Disconnected event, received on disconnection from a peer device */
            /* Maintain State */
            av_app_cb.state = AV_STATE_IDLE;
            WICED_BT_TRACE(" a2dp sink disconnected \n");

            /* Clear the LED */
            Cy_GPIO_Clr(CYBSP_USER_LED_PORT, CYBSP_LED_BLUE_NUM);
            break;

        case WICED_BT_A2DP_SINK_START_IND_EVT:
            if (NULL == p_data)
            {
                WICED_BT_TRACE ("A2DP Sink Control Callback with event data pointer NULL \n");
                return;
            }
            /**< Start stream indication, Send response */
            WICED_BT_TRACE("  a2dp sink start indication from Peer @handle: %d, %x \n", p_data->start_ind.handle,
                           p_data->start_ind.label );
            if (!wiced_bt_a2dp_sink_send_start_response( p_data->start_ind.handle, p_data->start_ind.label,
                                                          A2D_SUCCESS))
            {
                /* Maintain State */
                av_app_cb.state = AV_STATE_STARTED;
                WICED_BT_TRACE(" a2dp sink streaming started \n");
                /* Set the LED */
                Cy_GPIO_Set(CYBSP_USER_LED_PORT, CYBSP_LED_GREEN_NUM);

                start_i2s();
            }
            break;

        case WICED_BT_A2DP_SINK_START_CFM_EVT:
            if (NULL == p_data)
            {
                WICED_BT_TRACE ("A2DP Sink Control Callback with event data pointer NULL \n");
                return;
            }
            /**< Start stream event, received when audio streaming is about to
             * start */
            /* Maintain State */
            av_app_cb.state = AV_STATE_STARTED;
            WICED_BT_TRACE(" a2dp sink streaming started handle:%d\n", p_data->start_cfm.handle);
            break;

        case WICED_BT_A2DP_SINK_SUSPEND_EVT:
            /**< Suspend stream event, received when audio streaming is
             * suspended */
            /* Maintain State */
            av_app_cb.state = AV_STATE_CONNECTED;
            WICED_BT_TRACE(" a2dp sink streaming suspended \n");
            /* clear the LED */
            Cy_GPIO_Clr(CYBSP_USER_LED_PORT, CYBSP_LED_GREEN_NUM);

            stop_i2s();

            break;

        default:
            break;
    }
}

/* ****************************************************************************
 * Function Name: send_audio_data
 ******************************************************************************
 * Summary:
 *          Sends data to I2S Queue
 *
 * Parameters:
 *          incoming_data   -  media data
 *          size -  data length
 *
 * Return:
 *          None
 *
 * ***************************************************************************/
void send_audio_data(const int16_t* incoming_data, uint32_t size)
{
    uint32_t i,j,k;

    for (i = 0; i < size; i++)
    {
        buffer[write_index++] = incoming_data[i];

        if (write_index >= BUFFER_SIZE) {
            write_index = 0;
        }
            if (write_index >= FIXED_PACKET_SAMPLES)
            {
                int16_t* packet = (int16_t*) pvPortMalloc(FIXED_PACKET_SAMPLES * sizeof(int16_t));
                for (j = 0; j < FIXED_PACKET_SAMPLES; j++) {
                    packet[j] = buffer[j];
                }

                i2s_task_q_data.data = (uint8_t *)packet;
                i2s_task_q_data.data_len = FIXED_PACKET_SAMPLES * sizeof(int16_t);

                if (pdTRUE != xQueueSend(i2s_task_q, &i2s_task_q_data, portMAX_DELAY))
                {
                    CY_ASSERT(0);
                }

                i2s_task_q_num_samples += (i2s_task_q_data.data_len / sizeof(int16_t));
                for(k = 0; k < (write_index - FIXED_PACKET_SAMPLES);k++){
                    buffer[k]= buffer[k+FIXED_PACKET_SAMPLES];
                }
                write_index -= FIXED_PACKET_SAMPLES;
            }
    }
}

/* ****************************************************************************
 * Function Name: a2dp_sink_data_cback
 ******************************************************************************
 * Summary:
 *          Data supplied by  the a2dp sink profile code.
 *
 * Parameters:
 *          p_a2dp_data   - A2DP media data
 *          a2dp_data_len - A2DP data length
 *
 * Return:
 *          None
 *
 * ***************************************************************************/
void a2dp_sink_data_cback(uint8_t* p_rx_media, uint32_t media_len)
{
    uint8_t         i = 0, num_frames = 0;
    uint16_t        frame_len = 0;
    int16_t        *pOut = NULL;

    num_frames =  *p_rx_media & 0x0F;
    p_rx_media++;
    media_len--;

    for (i = 0; i < num_frames; i++)
    {
        /* Get frame length. stop if any error */
        if ((frame_len = a2dp_sink_get_frame_len (p_rx_media)) == 0)
        {
            break;
        }

        if (frame_len > media_len)
        {
            WICED_BT_TRACE ("a2dp_sink_proc_stream_data(): media too short %d %d %d %d\r\n", num_frames, i,
                            frame_len, (int)media_len);
            break;
        }

        pOut = (int16_t *) sbc_frame_decoded_pcm_data;


        SBC_Decoder_decoder(&strDecParams, p_rx_media, frame_len, (SINT16 *)pOut);

        frame_in_buffer = (int16_t *) sbc_frame_decoded_pcm_data;

        for (uint32_t i = 0; i < (PcmBytesPerFrame / (2 * sizeof(int16_t))); i++)
        {
            asrc_in_i2s_tx_buffer[i] = (int32_t) (*(int16_t *)frame_in_buffer);

            /* For stereo to mono conversion */
            frame_in_buffer += 2;
        }

        asrc_out_len = 2 * OUTPUT_BUFFER_MAX;

        IFX_asrc(asrc_in_i2s_tx_buffer, (PcmBytesPerFrame / (2 * sizeof(int16_t))), asrc_out_i2s_tx_buffer, &asrc_out_len, &asrc_mem);

        frame_out_buffer = (int16_t *) decoded_pcm_data;

        for (uint32_t i = 0; i < asrc_out_len; i++)
        {
            frame_out_buffer[i] = (int16_t) asrc_out_i2s_tx_buffer[i];
        }
        i2s_tx_num_samples += asrc_out_len;

        send_audio_data(frame_out_buffer, asrc_out_len) ;

        p_rx_media += frame_len;
        media_len  -= frame_len;
    }
}

/******************************************************************************
 * Function Name: a2dp_sink_write_eir
 *******************************************************************************
 * Summary:
 *          Prepare extended inquiry response data.  Current version publishes
 *          audio sink services.
 *
 * Parameters:
 *          None
 *
 * Return:
 *          None
 *
 ******************************************************************************/
void a2dp_sink_write_eir(void)
{
    uint8_t *pBuf;
    uint8_t *p;
    uint8_t length;

    pBuf = (uint8_t*)wiced_bt_get_buffer( WICED_HS_EIR_BUF_MAX_SIZE );

    if ( !pBuf )
    {
        return;
    }
    p = pBuf;

    length = strlen( (char *)a2dp_sink_cfg_settings.device_name );

    *p++ = length + 1;
    *p++ = BT_EIR_COMPLETE_LOCAL_NAME_TYPE;   /* EIR type full name */
    memcpy( p, a2dp_sink_cfg_settings.device_name, length );
    p += length;
    *p++ = ( 1 * 2 ) + 1;     /* length of services + 1 */
    *p++ =   BT_EIR_COMPLETE_16BITS_UUID_TYPE;
    /* EIR type full list of 16 bit service UUIDs */
    *p++ =   UUID_SERVCLASS_AUDIO_SINK & 0xff;
    *p++ = (UUID_SERVCLASS_AUDIO_SINK >> 8) & 0xff;
    *p++ = 0;

}

/******************************************************************************
 * Function Name: a2dp_sink_app_init()
 *******************************************************************************
 * Summary:
 *   This function handles application level initialization tasks and is
 *   called from the BT management callback once the Bluetooth stack enabled
 *   event (BTM_ENABLED_EVT) is triggered
 *   This function is executed in the BTM_ENABLED_EVT management callback.
 *
 * Parameters:
 *   None
 *
 * Return:
 *   None
 *
 ******************************************************************************/
void a2dp_sink_app_init(void)
{
    wiced_result_t result = WICED_BT_SUCCESS;
    av_app_cb.state = AV_STATE_IDLE;

    /* Register with the A2DP sink profile code */
    result = wiced_bt_a2dp_sink_init( &bt_audio_config,
                                         a2dp_sink_control_cback );
    if (result == WICED_BT_SUCCESS)
    {
        wiced_bt_a2dp_sink_register_data_cback(a2dp_sink_data_cback);
    }
}

/******************************************************************************
 * Function Name: a2dp_sink_management_callback()
 *******************************************************************************
 * Summary:
 *   This is a Bluetooth stack event handler function to receive management
 *   events from the Bluetooth stack and process as per the application.
 *
 * Parameters:
 *   wiced_bt_management_evt_t event : BLE event code of one byte length
 *   wiced_bt_management_evt_data_t *p_event_data: Pointer to BLE management
 *   event structures
 *
 * Return:
 *  wiced_result_t: Error code from WICED_RESULT_LIST or BT_RESULT_LIST
 *
 ******************************************************************************/

wiced_result_t a2dp_sink_management_callback( wiced_bt_management_evt_t event,
                                              wiced_bt_management_evt_data_t *p_event_data )
{
    wiced_bt_ble_advert_mode_t          *p_mode; /* Advertisement Mode */
    wiced_bt_device_address_t           bda = {0};
    cy_rslt_t                           result = WICED_BT_SUCCESS;

    wiced_bt_power_mgmt_notification_t *p_power_mgmt_notification;
    wiced_bt_dev_encryption_status_t   *p_encryption_status;
    wiced_bt_dev_pairing_cplt_t        *p_pairing_cmpl;
    int                                 pairing_result;

    WICED_BT_TRACE("A2dp sink management callback: %s\n", get_bt_event_name(event));

    switch( event )
    {
            /* Bluetooth  stack enabled */
        case BTM_ENABLED_EVT:
            /*stop and delete freeRTOS timer*/

            if (p_event_data == NULL)
            {
                WICED_BT_TRACE("Callback data pointer p_event_data is NULL \n");
                break;
            }
            if (WICED_BT_SUCCESS == p_event_data->enabled.status)
            {
                /* Bluetooth is enabled */
                wiced_bt_set_local_bdaddr((uint8_t *)bt_device_address, BLE_ADDR_PUBLIC);
                wiced_bt_dev_read_local_addr(bda);
                WICED_BT_TRACE("Bluetooth Enabled \n");
                WICED_BT_TRACE("Local Bluetooth Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                        bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

                /* Enable pairing */
                wiced_bt_set_pairable_mode(WICED_TRUE, 0);

                a2dp_sink_write_eir();

                /* create SDP records */
                wiced_bt_sdp_db_init((uint8_t*) a2dp_sink_sdp_db,
                        wiced_app_cfg_sdp_record_get_size());

               /* start the a2dp application */
               a2dp_sink_app_init();

               avrc_initialize();

               /* Making the sink device discoverable and connectable */
               wiced_bt_dev_set_discoverability(BTM_GENERAL_DISCOVERABLE,
                    BTM_DEFAULT_DISC_WINDOW, BTM_DEFAULT_DISC_INTERVAL);
               wiced_bt_dev_set_connectability(WICED_TRUE, BTM_DEFAULT_CONN_WINDOW,
                    BTM_DEFAULT_CONN_INTERVAL);
            }
            else
            {
                WICED_BT_TRACE("Bluetooth Enable failed \n");
            }
            break;

        case BTM_DISABLED_EVT:
            WICED_BT_TRACE("Bluetooth Disabled \n");
            break;

        case BTM_PIN_REQUEST_EVT:
            if (p_event_data == NULL)
            {
                WICED_BT_TRACE("Callback data pointer p_event_data is NULL \n");
                break;
            }
            WICED_BT_TRACE("remote address= ");
            print_bd_address(*p_event_data->pin_request.bd_addr);

            wiced_bt_dev_pin_code_reply(*p_event_data->pin_request.bd_addr,
                    (wiced_result_t)result/*WICED_BT_SUCCESS*/, 4, &pincode[0]);
            break;

        case BTM_USER_CONFIRMATION_REQUEST_EVT:
            if (p_event_data == NULL)
            {
                WICED_BT_TRACE("Callback data pointer p_event_data is NULL \n");
                break;
            }
            WICED_BT_TRACE("Pair Key: %d \n",
                    (int)p_event_data->user_confirmation_request.numeric_value);
            wiced_bt_dev_confirm_req_reply(WICED_BT_SUCCESS,
                    p_event_data->user_confirmation_request.bd_addr);
            break;

        case BTM_PASSKEY_NOTIFICATION_EVT:
            if (p_event_data == NULL)
            {
                WICED_BT_TRACE("Callback data pointer p_event_data is NULL \n");
                break;
            }
            WICED_BT_TRACE("PassKey Notification. BDA: ");
            print_bd_address(p_event_data->user_passkey_notification.bd_addr);
            WICED_BT_TRACE("PassKey Notification.  Key %d \n",
                    (int)p_event_data->user_passkey_notification.passkey);
            wiced_bt_dev_confirm_req_reply(WICED_BT_SUCCESS,
                    p_event_data->user_passkey_notification.bd_addr);
            break;

        case BTM_PAIRING_IO_CAPABILITIES_BR_EDR_REQUEST_EVT:
            if (p_event_data == NULL)
            {
                WICED_BT_TRACE("Callback data pointer p_event_data is NULL \n");
                break;
            }
            /* Use the default security for BR/EDR*/
            p_event_data->pairing_io_capabilities_br_edr_request.local_io_cap =
                    BTM_IO_CAPABILITIES_DISPLAY_AND_YES_NO_INPUT;
            p_event_data->pairing_io_capabilities_br_edr_request.auth_req =
                    BTM_AUTH_SINGLE_PROFILE_GENERAL_BONDING_NO;
            break;

        case BTM_PAIRING_IO_CAPABILITIES_BLE_REQUEST_EVT:
            if (p_event_data == NULL)
            {
                WICED_BT_TRACE("Callback data pointer p_event_data is NULL \n");
                break;
            }
            p_event_data->pairing_io_capabilities_ble_request.local_io_cap      =
                    BTM_IO_CAPABILITIES_NONE;
            p_event_data->pairing_io_capabilities_ble_request.oob_data          =
                    BTM_OOB_NONE;
            p_event_data->pairing_io_capabilities_ble_request.auth_req          =
                    BTM_LE_AUTH_REQ_SC_BOND;
            p_event_data->pairing_io_capabilities_ble_request.max_key_size      =
                    0x10;
            p_event_data->pairing_io_capabilities_ble_request.init_keys         =
                    BTM_LE_KEY_PENC|BTM_LE_KEY_PID|BTM_LE_KEY_PCSRK|BTM_LE_KEY_LENC;
            p_event_data->pairing_io_capabilities_ble_request.resp_keys         =
                    BTM_LE_KEY_PENC|BTM_LE_KEY_PID|BTM_LE_KEY_PCSRK|BTM_LE_KEY_LENC;
            break;

        case BTM_PAIRING_COMPLETE_EVT:
            if (p_event_data == NULL)
            {
                WICED_BT_TRACE("Callback data pointer p_event_data is NULL \n");
                break;
            }
            p_pairing_cmpl = &p_event_data->pairing_complete;

            if( p_pairing_cmpl->transport == BT_TRANSPORT_BR_EDR )
            {
                pairing_result = p_pairing_cmpl->pairing_complete_info.br_edr.status;
            }
            else
            {
                pairing_result = p_pairing_cmpl->pairing_complete_info.ble.status;
            }
            WICED_BT_TRACE("Pairing complete %d \n",pairing_result );
            /* Update Num of bonded devices and next free slot in slot data*/
            result = app_bt_update_slot_data();
            break;

        case BTM_PAIRED_DEVICE_LINK_KEYS_UPDATE_EVT:
            if (p_event_data == NULL)
            {
                WICED_BT_TRACE("Callback data pointer p_event_data is NULL \n");
                break;
            }
            /* save device keys to NVRAM */
            result = app_bt_save_device_link_keys(&(p_event_data->paired_device_link_keys_update));
            if (CY_RSLT_SUCCESS == result)
            {
                printf("Successfully Bonded to ");
                print_bd_address(p_event_data->paired_device_link_keys_update.bd_addr);
            }
            else
            {
                printf("Failed to bond! \n");
            }
            break;

        case  BTM_PAIRED_DEVICE_LINK_KEYS_REQUEST_EVT:
            if (p_event_data == NULL)
            {
                WICED_BT_TRACE("Callback data pointer p_event_data is NULL \n");
                break;
            }
            /* Paired Device Link Keys Request */
            printf("Paired Device Link keys Request Event for device ");
            print_bd_address((uint8_t *)(p_event_data->paired_device_link_keys_request.bd_addr));

            /* Need to search to see if the BD_ADDR we are
             * looking for is in NVRAM. If not, we return WICED_BT_ERROR
             * and the stack will generate keys and will then call
             * BTM_PAIRED_DEVICE_LINK_KEYS_UPDATE_EVT so that they
             * can be stored
             */

            /* Assume the device won't be found.
             * If it is, we will set this back to WICED_BT_SUCCESS */
            result = WICED_BT_ERROR;
            bondindex = app_bt_find_device_in_flash(p_event_data->paired_device_link_keys_request.bd_addr);

            if(BOND_INDEX_MAX > bondindex)
            {
                /* Copy the keys to where the stack wants it */
                memcpy(&(p_event_data->paired_device_link_keys_request),
                       &bond_info.link_keys[bondindex],
                       sizeof(wiced_bt_device_link_keys_t));
                result = WICED_BT_SUCCESS;
            }
            else
            {
                printf("Device Link Keys not found in the database! \n");
            }
            break;

        case BTM_POWER_MANAGEMENT_STATUS_EVT:
            if (p_event_data == NULL)
            {
                WICED_BT_TRACE("Callback data pointer p_event_data is NULL \n");
                break;
            }

            p_power_mgmt_notification = &p_event_data->power_mgmt_notification;

            WICED_BT_TRACE("Power mgmt status event -- bstatus:%d \n",p_power_mgmt_notification->status);
            break;

        case BTM_LOCAL_IDENTITY_KEYS_UPDATE_EVT:
            if (p_event_data == NULL)
            {
                WICED_BT_TRACE("Callback data pointer p_event_data is NULL \n");
                break;
            }
            /* Update of local privacy keys - save to NVRAM */
           result = app_bt_save_local_identity_key(p_event_data->local_identity_keys_update);
            if (CY_RSLT_SUCCESS != result)
            {
                result = WICED_BT_ERROR;
            }
            break;

        case BTM_LOCAL_IDENTITY_KEYS_REQUEST_EVT:
            if (p_event_data == NULL)
            {
                WICED_BT_TRACE("Callback data pointer p_event_data is NULL \n");
                break;
            }
            /* Read Local Identity Resolution Keys if present in NVRAM*/
            result = app_bt_read_local_identity_keys();

            if(CY_RSLT_SUCCESS == result)
            {
                memcpy(&(p_event_data->local_identity_keys_request),
                       &(identity_keys), sizeof(wiced_bt_local_identity_keys_t));
                print_array(&identity_keys, sizeof(wiced_bt_local_identity_keys_t));
                result = WICED_BT_SUCCESS;
            }
            else
            {
               result = WICED_BT_ERROR;
            }
             break;

        case BTM_ENCRYPTION_STATUS_EVT:
            if (p_event_data == NULL)
            {
                WICED_BT_TRACE("Callback data pointer p_event_data is NULL \n");
                break;
            }
            p_encryption_status = &p_event_data->encryption_status;
            WICED_BT_TRACE( "Encryption Status Event:  res %d", p_encryption_status->result);
            print_bd_address(p_encryption_status->bd_addr);
            /* Check and retreive the index of the bond data of the device that
             * got connected */
            /* This call will return BOND_INDEX_MAX if the device is not found */
            bondindex = app_bt_find_device_in_flash(p_event_data->encryption_status.bd_addr);
            if(bondindex < BOND_INDEX_MAX)
            {
                app_bt_restore_bond_data();
                app_bt_restore_cccd();
                /* Set CCCD value from the value that was previously saved in the NVRAM */
                app_notify_client_char_config[0] = peer_cccd_data[bondindex];
                printf("Bond info present in Flash for device: ");
                print_bd_address(p_event_data->encryption_status.bd_addr);
            }
            else
            {
                printf("No Bond info present in Flash for device: ");
                print_bd_address(p_event_data->encryption_status.bd_addr);
                bondindex=0;
            }
            break;

        case BTM_SECURITY_REQUEST_EVT:
            if (p_event_data == NULL)
            {
                WICED_BT_TRACE("Callback data pointer p_event_data is NULL \n");
                break;
            }
            wiced_bt_ble_security_grant(p_event_data->security_request.bd_addr,
                    WICED_BT_SUCCESS);
            break;

        case BTM_BLE_ADVERT_STATE_CHANGED_EVT:
            if (p_event_data == NULL)
            {
                WICED_BT_TRACE("Callback data pointer p_event_data is NULL \n");
                break;
            }
            p_mode = &p_event_data->ble_advert_state_changed;
            WICED_BT_TRACE( "Advertisement State Change: %d\n", *p_mode);
            break;

        case BTM_BLE_CONNECTION_PARAM_UPDATE:
            if (p_event_data == NULL)
            {
                WICED_BT_TRACE("Callback data pointer p_event_data is NULL \n");
                break;
            }
            /* Connection parameters updated */
            if (WICED_SUCCESS == p_event_data->ble_connection_param_update.status) {
                WICED_BT_TRACE("Supervision Time Out = %d\n",
                        (p_event_data->ble_connection_param_update.supervision_timeout
                                * 10));
            }
            break;

        default:
            WICED_BT_TRACE("Unhandled Bluetooth Management Event: 0x%x %s\r\n", event,
            get_bt_event_name(event));
            break;
    }

    return (wiced_result_t)result;
}
/* [] END OF FILE */
