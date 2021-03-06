/* CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at src/license_cddl-1.0.txt
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at src/license_cddl-1.0.txt
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*! \file   logic_fwflash_storage.h
 *  \brief  Logic for storing/getting fw data in the dedicated flash storage
 *  Copyright [2014] [Mathieu Stephan]
 */


#ifndef LOGIC_FWFLASH_STORAGE_H_
#define LOGIC_FWFLASH_STORAGE_H_

#include "defines.h"

// Defines
#define BITMAP_ID_OFFSET        64
#define MEDIA_TYPE_LENGTH       2

// String IDs
#define ID_STRING_PROCESSING    0
#define ID_STRING_CARD_BLOCKED  1
#define ID_STRING_PB_CARD       2
#define ID_STRING_WRONG_PIN     3
#define ID_STRING_REMOVE_CARD   4
#define ID_STRING_INSERT_OTHER  5
#define ID_STRING_FAILED        6
#define ID_STRING_PIN_CHANGED   7
#define ID_STRING_PIN_NCGHANGED 8
#define ID_STRING_USER_ADDED    9
#define ID_STRING_USER_NADDED   10
#define ID_STRING_CARD_UNLOCKED 11
#define ID_STRING_CARDID_NFOUND 12
#define ID_STRING_INSERT_NCARD  13
#define ID_STRING_DONE          14
#define ID_STRING_CARD_REMOVED  15
#define ID_STRING_AREYOUSURE    16
#define ID_STRING_AREYOURLSURE  17
#define ID_STRING_OTHECARDFUSER 18
#define ID_STRING_ENTERLOGINQ   19
#define ID_STRING_ENTERPASSQ    20
#define ID_STRING_APPROVEMEMOP  21
#define ID_STRING_INSERT_PIN    22
#define ID_STRING_NEW_PINQ      23
#define ID_STRING_CONF_PIN      24
#define ID_STRING_CONFACCESSTO  25
#define ID_STRING_WITHTHISLOGIN 26
#define ID_STRING_CONF_NEWCREDS 27
#define ID_STRING_ADDUSERNAME   28
#define ID_STRING_ON            29
#define ID_STRING_CHANGEPASSFOR 30
#define ID_STRING_WRONGPIN1LEFT 31
#define ID_STRING_WRONGPIN2LEFT 32
#define ID_STRING_WRONGPIN3LEFT 33
#define ID_STRING_NEWMP_USER    34
#define ID_STRING_GOINGTOSLEEP  35
#define ID_STRING_MEMORYMGMTQ   36
#define ID_STRING_MEMORYMGMT    37
#define ID_STRING_CLOSEMEMMGMT  38
#define ID_STRING_CREATEDBYPLUG 39
#define ID_STRING_NOSTOREDFAV   40
#define ID_STRING_SEND_SMC_PASS 41
#define ID_STRING_SET_SMC_LOGIN 42
#define ID_STRING_SET_SMC_PASS  43
#define ID_STRING_YOUR_USERNAME 44
#define ID_STRING_SHOW_LOGINQ   45
#define ID_STRING_SHOW_PASSQ    46
#define ID_STRING_TEST_FLASH_OK 47
#define ID_STRING_TEST_FLASH_PB 48
#define ID_STRING_TEST_TOUCH_OK 49
#define ID_STRING_TEST_TOUCH_PB 50
#define ID_STRING_TEST_INST_TCH 51
#define ID_STRING_TEST_DET      52
#define ID_STRING_TEST_LEFT     53
#define ID_STRING_TEST_WHEEL    54
#define ID_STRING_TEST_RIGHT    55
#define ID_STRING_TEST_CARD_INS 56
#define ID_STRING_TEST_CARD_OK  57
#define ID_STRING_TEST_CARD_PB  58
#define ID_STRING_TEST_LEDS_CH  59
#define ID_STRING_TEST_OK       60
#define ID_STRING_TEST_NOK      61

// Keyboard LUTs
// Changes: 
// - ID_KEYB_MX_MX_LUT removed (same as ID_KEYB_ES_AR_LUT)
// - ID_KEYB_SW_SW_LUT removed (same as ID_KEYB_FI_FI_LUT)
// - ID_KEYB_DE_AU_LUT removed (same as ID_KEYB_DE_DE_LUT)
// - ID_KEYB_ZH_HK_LUT ID_KEYB_JP_JP_LUT ID_KEYB_KO_KO_LUT ID_KEYB_EN_NZ_LUT ID_KEYB_EN_SG_LUT ID_KEYB_EN_SA_LUT removed (same as ID_KEYB_EN_AU_LUT)
#define ID_KEYB_EN_US_LUT       BITMAP_ID_OFFSET+18
#define ID_KEYB_FR_FR_LUT       BITMAP_ID_OFFSET+19
#define ID_KEYB_ES_ES_LUT       BITMAP_ID_OFFSET+20
#define ID_KEYB_DE_DE_LUT       BITMAP_ID_OFFSET+21
#define ID_KEYB_ES_AR_LUT       BITMAP_ID_OFFSET+22
#define ID_KEYB_EN_AU_LUT       BITMAP_ID_OFFSET+23
#define ID_KEYB_FR_BE_LUT       BITMAP_ID_OFFSET+24
#define ID_KEYB_PO_BR_LUT       BITMAP_ID_OFFSET+25
#define ID_KEYB_EN_CA_LUT       BITMAP_ID_OFFSET+26
#define ID_KEYB_CZ_CZ_LUT       BITMAP_ID_OFFSET+27
#define ID_KEYB_DA_DK_LUT       BITMAP_ID_OFFSET+28
#define ID_KEYB_FI_FI_LUT       BITMAP_ID_OFFSET+29
#define ID_KEYB_HU_HU_LUT       BITMAP_ID_OFFSET+30
#define ID_KEYB_IS_IS_LUT       BITMAP_ID_OFFSET+31
#define ID_KEYB_IT_IT_LUT       BITMAP_ID_OFFSET+32  // So it seems Italian keyboards don't have ~`{}
#define ID_KEYB_NL_NL_LUT       BITMAP_ID_OFFSET+33
#define ID_KEYB_NO_NO_LUT       BITMAP_ID_OFFSET+34
#define ID_KEYB_PO_PO_LUT       BITMAP_ID_OFFSET+35  // Polish keyboards don't have []|
#define ID_KEYB_RO_RO_LUT       BITMAP_ID_OFFSET+36
#define ID_KEYB_SL_SL_LUT       BITMAP_ID_OFFSET+37
#define ID_KEYB_FRDE_CH_LUT     BITMAP_ID_OFFSET+38
#define ID_KEYB_EN_UK_LUT       BITMAP_ID_OFFSET+39
#define FIRST_KEYB_LUT          ID_KEYB_EN_US_LUT
#define LAST_KEYB_LUT           ID_KEYB_EN_UK_LUT

// Prototypes
uint8_t getKeybLutEntryForLayout(uint8_t layout, uint8_t ascii_char);
RET_TYPE getStoredFileAddr(uint16_t fileId, uint16_t* addr);
char* readStoredStringToBuffer(uint8_t stringID);

#endif /* LOGIC_FWFLASH_STORAGE_H_ */
