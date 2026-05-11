//
//  SerialProtocol.h
//  SurfaceSerialHub
//
//  Created by Xavier on 2021/10/29.
//  Copyright © 2021 Xia Shangning. All rights reserved.
//

#ifndef SerialProtocol_h
#define SerialProtocol_h

#include <IOKit/IOLib.h>

/* SSH Protocol Config see https://github.com/linux-surface/surface-aggregator-module/blob/master/doc/requests.txt for reference*/
#define SSH_TC_SAM              0x01    /* Generic system functionality, real-time clock. */
#define SSH_TC_BAT              0x02    /* Battery/power subsystem. */
#define SSH_TC_TMP              0x03    /* Thermal subsystem. */
#define SSH_TC_PMC              0x04
#define SSH_TC_FAN              0x05
#define SSH_CID_FAN_GET_SPEED   0x01
#define SSH_TC_PoM              0x06
#define SSH_TC_DBG              0x07
#define SSH_TC_KBD              0x08    /* Legacy keyboard (Laptop 1/2). */
#define SSH_TC_FWU              0x09
#define SSH_TC_UNI              0x0a
#define SSH_TC_LPC              0x0b
#define SSH_TC_TCL              0x0c
#define SSH_TC_SFL              0x0d
#define SSH_TC_KIP              0x0e
#define SSH_TC_EXT              0x0f
#define SSH_TC_BLD              0x10
#define SSH_TC_BAS              0x11    /* Detachment system (Surface Book 2/3). */
#define SSH_TC_SEN              0x12
#define SSH_TC_SRQ              0x13
#define SSH_TC_MCU              0x14
#define SSH_TC_HID              0x15    /* Generic HID input subsystem. */
#define SSH_TC_TCH              0x16
#define SSH_TC_BKL              0x17
#define SSH_TC_TAM              0x18
#define SSH_TC_ACC              0x19
#define SSH_TC_UFI              0x1a
#define SSH_TC_USC              0x1b
#define SSH_TC_PEN              0x1c
#define SSH_TC_VID              0x1d
#define SSH_TC_AUD              0x1e
#define SSH_TC_SMC              0x1f
#define SSH_TC_KPD              0x20
#define SSH_TC_REG              0x21

#define SSH_TID_PRIMARY         0x01
#define SSH_TID_SECONDARY       0x02

/* TC=0x01 */
#define SSH_CID_SAM_ENABLE_EVENT    0x0B
#define SSH_CID_SAM_DISABLE_EVENT   0x0C
#define SSH_CID_SAM_VERSION         0x13
#define SSH_CID_SAM_DISPLAY_OFF     0x15
#define SSH_CID_SAM_DISPLAY_ON      0x16
#define SSH_CID_SAM_D0_EXIT         0x33
#define SSH_CID_SAM_D0_ENTRY        0x34
/* TC=0x02 */
#define SSH_CID_BAT_STA             0x01
#define SSH_CID_BAT_BIX             0x02
#define SSH_CID_BAT_BST             0x03
#define SSH_CID_BAT_PMAX            0x0B
#define SSH_CID_BAT_PSOC            0x0C
#define SSH_CID_BAT_PSR             0x0D
#define SSH_EVENT_CID_BAT_BIX       0x15
#define SSH_EVENT_CID_BAT_BST       0x16
#define SSH_EVENT_CID_BAT_PSR       0x17
/* TC=0x03 */
#define SSH_CID_TMP_SENSOR          0x01
#define SSH_CID_TMP_GET_PERF        0x02
#define SSH_CID_TMP_SET_PERF        0x03
#define SSH_TEMP_SENSOR_MB1         0x01
#define SSH_TEMP_SENSOR_MB2         0x02
#define SSH_TEMP_SENSOR_MB3         0x03
#define SSH_TEMP_SENSOR_MB4         0x04
#define SSH_TEMP_SENSOR_BAT         0x05
#define SSH_TEMP_SENSOR_GPU         0x06
#define SSH_TEMP_SENSOR_SSD         0x07
#define SSH_TEMP_SENSOR_SOC         0x08
/* TC=0x08 */
#define SSH_CID_KBD_GET_DESCRIPTOR      0x00
#define SSH_CID_KBD_SET_CAPS_LED        0x01
#define SSH_CID_KBD_GET_FEAT_REPORT     0x0b
#define SSH_EVENT_CID_KBD_INPUT_GENERIC 0x03
#define SSH_EVENT_CID_KBD_INPUT_HOTKEYS 0x04
/* TC=0x15 */
#define SSH_CID_HID_OUT_REPORT      0x01
#define SSH_CID_HID_GET_FEAT_REPORT 0x02
#define SSH_CID_HID_SET_FEAT_REPORT 0x03
#define SSH_CID_HID_GET_DESCRIPTOR  0x04
#define SSH_EVENT_CID_HID_INPUT     0x00

#define SSH_EVENT_FLAG_SEQUENCED    BIT(0)

#define SSH_SYN_BYTE_1  0xAA
#define SSH_SYN_BYTE_2  0x55
#define SSH_SYN_BYTES   0x55AA

#define SSH_FRAME_TYPE_NAK          0x04    /* Sent on error in previously received message. */
#define SSH_FRAME_TYPE_ACK          0x40    /* Sent to acknowledge receival of DATA frame. */
#define SSH_FRAME_TYPE_DATA_SEQ     0x80    /* Sent to transfer data. Sequenced. */
#define SSH_FRAME_TYPE_DATA_NSQ     0x00    /* Same as DATA_SEQ, but does not need to be ACKed. */

#define SSH_COMMAND_TYPE            0x80

#define SSH_DATA_OFFSET         sizeof(SurfaceSerialMessage)+sizeof(SurfaceSerialCommand)
#define SSH_PAYLOAD_OFFSET      sizeof(SurfaceSerialMessage)

#ifndef PACKED
#define PACKED __attribute__((packed))
#endif

struct PACKED SurfaceSerialFrame {
    UInt8 type;
    UInt16 length;
    UInt8 seq_id;
};

struct PACKED SurfaceSerialCommand {
    UInt8 type;             /* always 0x80 */
    UInt8 target_category;
    UInt8 target_id_out;    /* to SAM(request) */
    UInt8 target_id_in;     /* from SAM(response) */
    UInt8 instance_id;
    UInt16 request_id; /* request<->response, events->reserved id set by command `enable event source`*/
    UInt8 command_id;
};

struct PACKED SurfaceSerialMessage {
    UInt16 syn;
    SurfaceSerialFrame frame;
    UInt16 frame_crc;
};

struct PACKED SurfaceEventPayload {
    UInt8 target_category;
    UInt8 flags;
    UInt16 request_id;  /* the request id used for the event*/
    UInt8 instance_id;
};

#define CRC_INITIAL     0xFFFF

extern UInt16 const crc_ccitt_false_table[256];

static inline UInt16 crc_ccitt_false_byte(UInt16 crc, const UInt8 c)
{
    return (crc << 8) ^ crc_ccitt_false_table[(crc >> 8) ^ c];
}

inline UInt16 crc_ccitt_false(UInt16 crc, UInt8 const* buffer, size_t len)
{
    while (len--)
        crc = crc_ccitt_false_byte(crc, *buffer++);
    return crc;
}

#endif /* SerialProtocol_h */
