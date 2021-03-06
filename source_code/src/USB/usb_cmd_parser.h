#ifndef _USB_CMD_PARSER_H_
#define _USB_CMD_PARSER_H_

#include "flash_mem.h"
#include <stdint.h>
#include "usb.h"

/* USB mooltipass hid commands */
#define CMD_DEBUG               0x01
#define CMD_PING                0x02
#define CMD_VERSION             0x03
#define CMD_CONTEXT             0x04
#define CMD_GET_LOGIN           0x05
#define CMD_GET_PASSWORD        0x06
#define CMD_SET_LOGIN           0x07
#define CMD_SET_PASSWORD        0x08
#define CMD_CHECK_PASSWORD      0x09
#define CMD_ADD_CONTEXT         0x0A
#define CMD_EXPORT_FLASH        0x30    // resp: 0x30 packets until 0x31
#define CMD_EXPORT_FLASH_END    0x31
#define CMD_IMPORT_FLASH_BEGIN  0x32    // confirmed by 0x32,0x01
#define CMD_IMPORT_FLASH        0x33    // send 4x60 byte + 1x24 byte packets, acked with 0x33,0x01
#define CMD_IMPORT_FLASH_END    0x34
#define CMD_EXPORT_EEPROM       0x35    // resp: 0x35 packets until 0x36
#define CMD_EXPORT_EEPROM_END   0x36
#define CMD_IMPORT_EEPROM_BEGIN 0x37    // confirmed by 0x37,0x01
#define CMD_IMPORT_EEPROM       0x38    // send packet, acked with 0x38,0x01
#define CMD_IMPORT_EEPROM_END   0x39
#define CMD_ERASE_EEPROM        0x40
#define CMD_ERASE_FLASH         0x41
#define CMD_ERASE_SMC           0x42
#define CMD_DRAW_BITMAP         0x43
#define CMD_SET_FONT            0x44
#define CMD_EXPORT_FLASH_START  0x45
#define CMD_EXPORT_EEPROM_START 0x46
#define CMD_SET_BOOTLOADER_PWD  0x47
#define CMD_JUMP_TO_BOOTLOADER  0x48
#define CMD_CLONE_SMARTCARD     0x49
#define CMD_STACK_FREE          0x4A
#define CMD_GET_RANDOM_NUMBER   0x4B
#define CMD_START_MEMORYMGMT    0x50
#define CMD_END_MEMORYMGMT      0x51
#define CMD_IMPORT_MEDIA_START  0x52
#define CMD_IMPORT_MEDIA        0x53
#define CMD_IMPORT_MEDIA_END    0x54
#define CMD_READ_FLASH_NODE     0x55
#define CMD_WRITE_FLASH_NODE    0x56
#define CMD_SET_FAVORITE        0x57
#define CMD_SET_STARTINGPARENT  0x58
#define CMD_SET_CTRVALUE        0x59
#define CMD_ADD_CARD_CPZ_CTR    0x5A
#define CMD_GET_CARD_CPZ_CTR    0x5B
#define CMD_CARD_CPZ_CTR_PACKET 0x5C
#define CMD_SET_MOOLTIPASS_PARM 0x5D
#define CMD_GET_MOOLTIPASS_PARM 0x5E
#define CMD_GET_FAVORITE        0x5F
#define CMD_RESET_CARD          0x60
#define CMD_READ_CARD_LOGIN     0x61
#define CMD_READ_CARD_PASS      0x62
#define CMD_SET_CARD_LOGIN      0x63
#define CMD_SET_CARD_PASS       0x64
#define CMD_GET_FREE_SLOT_ADDR  0x65
#define CMD_GET_STARTING_PARENT 0x66
#define CMD_GET_CTRVALUE        0x67
#define CMD_ADD_UNKNOWN_CARD    0x68
#define CMD_USB_KEYBOARD_PRESS  0x69
#define CMD_MOOLTIPASS_STATUS   0x70
#define CMD_FUNCTIONAL_TEST_RES 0x71

/* Packet format defines     */
#define HID_LEN_FIELD       0x00
#define HID_TYPE_FIELD      0x01
#define HID_DATA_START      0x02

/* Packet answers */
#define PLUGIN_BYTE_ERROR   0x00
#define PLUGIN_BYTE_OK      0x01
#define PLUGIN_BYTE_NA      0x02
#define PLUGIN_BYTE_NOCARD  0x03

/* Packet defines */
#define PACKET_EXPORT_SIZE  (RAWHID_TX_SIZE-HID_DATA_START)

/* function caller IDs */
#define USB_CALLER_MAIN     0x00
#define USB_CALLER_PIN      0x01

/*** MACROS ***/
#ifdef CMD_PARSER_USB_DEBUG_OUTPUT
    #define USBPARSERDEBUGPRINTF_P(args...) usbPrintf_P(args)
#else
    #define USBPARSERDEBUGPRINTF_P(args...)
#endif

/*** STRUCTS ***/
typedef struct
{
    uint8_t len;
    uint8_t cmd;
    union
    {
        uint8_t data[RAWHID_RX_SIZE-HID_DATA_START];
        uint32_t addr;
    } body;
} usbMsg_t;

/*** PROTOTYPES ***/
void usbProcessIncoming(uint8_t caller_id);
void leaveMemoryManagementMode(void);

#endif
