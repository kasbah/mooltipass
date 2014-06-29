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

/* Copyright (c) 2014 Darran Hunt. All rights reserved. */

/*!	\file 	oledmp.c
*	\brief	Mooltipass SSD1322 256x64x16 OLED display library
*	Created: 15/2/2014
*	Author: Darran Hunt
*/


/*
 * Notes
 * -----
 *
 *  - the y index is a index from 0 to 63 and is relative to the current
 *  - oled_offset. Inactive writes have 64 added.
 */


/*
 * Updates to do:
 *
 * - Condsider that the inactive buffer is whatever is not currently being displayed,
 *   regardless of the scroll offset.
 */

#include <avr/pgmspace.h>
#include <util/delay.h>
#include <stdio.h>
#include <ctype.h>

#include "low_level_utils.h"
#include "bitstream.h"
#include "defines.h"
#include "oledmp.h"
#include "utils.h"
#include "flash_mem.h"
#include "usb.h"

// Make sure the USART SPI is selected
#if SPI_OLED != SPI_USART
    #error "SPI not implemented"
#endif

#undef OLED_DEBUG

// OLED specific port and pin definitions
#define OLED_PORT_CS	&PORT_OLED_SS
#define OLED_PORT_DC	&PORT_OLED_DnC
#define OLED_PORT_RESET &PORT_OLED_nR
#define OLED_PORT_POWER &PORT_OLED_POW
#define OLED_CS		(1<<PORTID_OLED_SS)
#define OLED_DC		(1<<PORTID_OLED_DnC)
#define OLED_nRESET	(1<<PORTID_OLED_nR)
#define OLED_POWER	(1<<PORTID_OLED_POW)

#define MIN_SEG 28		// minimum visable OLED 4-pixel segment
#define MAX_SEG 91		// maximum visable OLED 4-pixel segment

#define OLED_Y_MASK   (OLED_HEIGHT*2 - 1)     // Maxium y index into OLED buffer

static int oledFputc(char ch, FILE *stream);

/*
 * Module Globals
 */
// Stream for printf/puts operations via the OLEDMP library
FILE oledStdout = FDEV_SETUP_STREAM(oledFputc, NULL, _FDEV_SETUP_WRITE);


/*
 * Module Local globals
 */

static uint8_t oled_offset=0;                // current display offset in GDDRAM
static uint8_t oled_writeOffset=0;           // offset for writing 
static uint8_t oled_bufHeight;
static uint8_t oled_scroll_delay = 10;   // milliseconds between line scroll
static uint8_t oled_writeBuffer = 0;
static uint8_t oled_displayBuffer = 0;

// pixel buffer to allow merging of adjacent image data.
// To conserve memory, only one GDDRAM word is kept per display line.
// The library assumes the display will be filled left to right, and
// hence it only needs to merge the rightmost data with the next write
// on that line.
// Note that the buffer is twice the OLED height, supporting 
// the second hidden screen buffer.
static struct 
{
    uint8_t xaddr;
    uint16_t pixels;
} gddram[OLED_HEIGHT*2];

static font_t *oled_fontp;      //*< Current font
static uint8_t oled_cur_x[2] = { 0, 0 };
static uint8_t oled_cur_y[2] = { 0, 0 };
static uint8_t oled_foreground;
static uint8_t oled_background;
static bool oled_wrap = false;

/*
 * OLED initialisation sequence
 */
static const uint8_t oled_init[] __attribute__((__progmem__)) = 
{
    CMD_SET_COMMAND_LOCK,            1, 0x12, /* Unlock OLED driver IC*/
    CMD_SET_DISPLAY_OFF,             0,
    CMD_SET_CLOCK_DIVIDER,           1, 0x91,
    CMD_SET_MULTIPLEX_RATIO,         1, 0x3F, /*duty = 1/64*,64 COMS are enabled*/
    CMD_SET_DISPLAY_OFFSET,          1, 0x00,
    CMD_SET_DISPLAY_START_LINE,      1, 0x00, /*set start line position*/
    CMD_SET_REMAP,                   2, 0x14, // Horizontal address increment,
                                              // Disable Column Address Re-map,
                                              // Enable Nibble Re-map,Scan from COM[N-1] to COM0,
                                              // Disable COM Split Odd Even
                                        0x11, // Enable Dual COM mode
    CMD_SET_GPIO,                    1, 0x00,
    CMD_SET_FUNCTION_SELECTION,      1, 0x01, /* selection external VDD */
    CMD_DISPLAY_ENHANCEMENT,         2, 0xA0, /* enables the external VSL*/
                                        0xfd, /* 0xfd,Enhanced low GS display quality;default is 0xb5(normal),*/
    CMD_SET_CONTRAST_CURRENT,        1, 0x7f, /* default is 0x7f*/
    CMD_MASTER_CURRENT_CONTROL,      1, 0x0f,
    /* writeCommand(0xB9); GRAY TABLE,linear Gray Scale*/
    CMD_SET_PHASE_LENGTH,            1, 0xE2,  /*default is 0x74*/
    CMD_DISPLAY_ENHANCEMENT_B,       2, 0x82, 0x20,
#define ALTERNATE_OLED_VERSION
#ifdef ALTERNATE_OLED_VERSION
    CMD_SET_PRECHARGE_VOLTAGE,       1, 0x08, /* 0.3xVcc */
    CMD_SET_SECOND_PRECHARGE_PERIOD, 1, 0x0F, /* 15 clocks */
#else
    CMD_SET_PRECHARGE_VOLTAGE,       1, 0x1F, /* 0.6xVcc */
    CMD_SET_SECOND_PRECHARGE_PERIOD, 1, 0x08, /* default */
#endif
    CMD_SET_VCOMH_VOLTAGE,           1, 0x07, /*0.86xVcc;default is 0x04*/
    CMD_SET_DISPLAY_MODE_NORMAL,     0
};


/**
 * Initialise the OLED controller and prep the display
 * for use.
 */
void oledBegin(uint8_t font)
{
    oled_foreground = 15;
    oled_background = 0;
    oled_offset = 0;
    oled_bufHeight = OLED_HEIGHT;
    oled_fontp = NULL;

    oledSetFont(font);
    
    pinMode(OLED_PORT_CS, OLED_CS, OUTPUT, false);
    pinMode(OLED_PORT_DC, OLED_DC, OUTPUT, false);
    pinMode(OLED_PORT_RESET, OLED_nRESET, OUTPUT, false);
    pinMode(OLED_PORT_POWER, OLED_POWER, OUTPUT, false);
    pinHigh(OLED_PORT_POWER, OLED_POWER);
    pinHigh(OLED_PORT_CS, OLED_CS);

    oledReset();
    oledInit();

    for (uint8_t ind=0; ind<OLED_HEIGHT*2; ind++) 
    {
        gddram[ind].xaddr = 0;
        gddram[ind].pixels = 0;
    }

    oledWriteInactiveBuffer();
    oledClear();
    oledWriteActiveBuffer();
    oledClear();

    // Map stdout to use the OLED display.
    // This means that printf(), printf_P(), puts(), puts_P(), etc 
    // will all output to the OLED display.
    stdout = &oledStdout;
}


/**
 * Set the start line of the displayed buffer in GDDRAM.
 * @param line - line to start displaying from (0-127)
 */
void oledSetDisplayStartLine(uint8_t line)
{
#ifdef OLED_DEBUG
    usbPrintf_P(PSTR("oledSetDisplayStartLine(%d)\n"), line & OLED_Y_MASK);
#endif
    oled_offset = line & OLED_Y_MASK;
    oledWriteCommand(CMD_SET_DISPLAY_START_LINE);
    oledWriteData(oled_offset);
}


/**
 * Move the start line of the displayed buffer in GDDRAM by the specified offset.
 * @param offset - the amount to change the start line
 */
void oledMoveDisplayStartLine(int8_t offset)
{
#ifdef OLED_DEBUG
    usbPrintf_P(PSTR("oledMoveDisplayStartLine(%d)\n"), offset);
#endif
    oled_offset = (oled_offset + offset) & OLED_Y_MASK;
    oledWriteCommand(CMD_SET_DISPLAY_START_LINE);
    oledWriteData(oled_offset);
}

/**
 * Switch the inactive buffer to the active buffer.
 * This displays the content of the inactive buffer and
 * makes it the active buffer.
 */
void oledFlipDisplayedBuffer()
{
    oled_displayBuffer = !oled_displayBuffer;
    oled_writeBuffer = !oled_writeBuffer;
    oled_writeOffset = (oled_writeOffset + OLED_HEIGHT) & OLED_Y_MASK;
    oledMoveDisplayStartLine(OLED_HEIGHT);
}


// set write buffer to the be the inactive (offscreen) buffer
void oledWriteInactiveBuffer()
{
    oled_writeBuffer = !oled_displayBuffer;
    oled_writeOffset = OLED_HEIGHT;
}


// set write buffer to the be the active (onscreen) buffer
void oledWriteActiveBuffer()
{
    oled_writeBuffer = oled_displayBuffer;
    oled_writeOffset = 0;
}


/**
 * Swap the currently displayed buffer with the inactive buffer
 * @param mode - optionally scroll the buffer up or down with
 *               OLED_SCROLL_UP or OLED_SCROLL_DOWN
 * @param delay - number of msecs to wait between scrolling each line.
 *               Set to 0 to use the global scroll speed setting
 */
void oledFlipBuffers(uint8_t mode, uint8_t delay)
{
    if (delay == 0) 
    {
        // 0 -> use global scroll delay
        delay = oled_scroll_delay;
    }
    if (mode == 0) 
    {
        oledMoveDisplayStartLine(OLED_HEIGHT);
    }
    else 
    {
        int8_t offset = (mode == OLED_SCROLL_UP ? 1 : -1);
        for (uint8_t ind=0; ind<OLED_HEIGHT; ind++) 
        {
            oledMoveDisplayStartLine(offset);
            delay_ms(delay);
        }
    }

    oled_displayBuffer = !oled_displayBuffer;
    oled_writeBuffer = !oled_writeBuffer;
}


/**
 * Set the scroll speed for scroll operations
 * @param msecs - number of msecs to wait between each line
 */
void oledSetScrollSpeed(double msecs)
{
    oled_scroll_delay = msecs;
}

/**
 * Set the foreground greyscale colour for text and bitmaps
 * @param colour - 0 to 15
 */
void oledSetColour(uint8_t colour)
{
    oled_foreground = colour & 0x0F;
}


/**
 * Set the contrast (brightness) level for the screen.
 * @param contrast - level from 0 (min) to 255 (max)
 */
void oledSetBackground(uint8_t colour)
{
    oled_background = colour & 0x0F;
}

/**
 * Set the contrast (brightness) level for the screen.
 * @param contrast - level from 0 (min) to 255 (max)
 */
void oledSetContrast(uint8_t contrast)
{
    oledWriteCommand(CMD_SET_CONTRAST_CURRENT);
    oledWriteData(contrast);
}


/**
 * Set remap mode. This defines how the internal
 * display RAM (GDDRAM) is mapped onto the physical display pixels.
 * @param mode - level from 0 (min) to 255 (max)
 *        bit[1] 0 = column address segments left to right
 *               1 = column address segments right to left
 *        bit[2] 0 = nibbles direct access
 *               1 = nibbles endian swapped
 *        bit[4] 0 = scan rows from top to bottm
 *               1 = scan rows from bottom to top
 * @note bit 0, and 5-7 are masked out by the function
 *       as changing these would damage the operation
 *       of the library.
 */
void oledSetRemap(uint8_t mode)
{
    oledWriteCommand(CMD_SET_REMAP);
    oledWriteData(mode & 0x1E);
    oledWriteData(0x11);    // Dual COM mode

}


/**
 * print an character on the screen at the current X and
 * Y position. X and Y position is updated after the print
 * operation, with X wrapping if necessary.
 * @param ch - the character to print
 * @note '\n' is will increment the row position based
 *       on the current font height, and also reset x to 0.
 *       '\r' will reset x to 0.
 */
void oledPutch(char ch)
{
    uint8_t glyphHeight = oledGlyphHeight();
#ifdef OLED_DEBUG
    if (isprint(ch)) 
    {
        usbPrintf_P(PSTR("oledPutch('%c') x=%d, y=%d, oled_offset=%d, buf=%d\n"), 
                ch, oled_cur_x[oled_writeBuffer], oled_cur_y[oled_writeBuffer], oled_offset, oled_writeBuffer);
    }
    else 
    {
        usbPrintf_P(PSTR("oledPutch('0x%02x') x=%d, y=%d, oled_offset=%d, buf=%d\n"),
                ch, oled_cur_x[oled_writeBuffer], oled_cur_y[oled_writeBuffer], oled_offset, oled_writeBuffer);
    }
#endif

    // see if the current offset is offscreen
    if ((oled_cur_y[oled_writeBuffer] + glyphHeight) > OLED_HEIGHT) 
    {
        if (oled_wrap || oled_writeBuffer != oled_displayBuffer)
        {
            // Wrap back to the top of the display
            oled_cur_y[oled_writeBuffer] = 0;
        }
        else
        {
#ifdef OLED_DEBUG
            usbPrintf_P(PSTR("oledPutch('0x%02x') scroll x=%d, y=%d, oled_offset=%d, buf=%d\n"),
                    ch, oled_cur_x[oled_writeBuffer], oled_cur_y[oled_writeBuffer], oled_offset, oled_writeBuffer);
#endif            
            oledScrollUp(0, true);
            oled_cur_y[oled_writeBuffer] -= glyphHeight;
#ifdef OLED_DEBUG
            usbPrintf_P(PSTR("oledPutch('0x%02x') scroll completed y=%d, oled_offset=%d, gHeight=%d\n"),
                    ch, oled_cur_y[oled_writeBuffer], oled_offset, glyphHeight);
#endif            
        } 
    }

    if (ch == '\n') 
    {
        oled_cur_x[oled_writeBuffer] = 0;
        oled_cur_y[oled_writeBuffer] += glyphHeight;
    }
    else if (ch == '\r') 
    {
        oled_cur_x[oled_writeBuffer] = 0;
    }
    else 
    {
        uint8_t width = oledGlyphWidth(ch);
#ifdef OLED_DEBUG
        usbPrintf_P(PSTR("oled_putch('%c')\n"), ch);
#endif
        if ((oled_cur_x[oled_writeBuffer] + width) > OLED_WIDTH) 
        {
#ifdef OLED_DEBUG
            usbPrintf_P(PSTR("wrap at y=%d x=%d, '%c' width %d\n"),oled_cur_y,oled_cur_x[oled_writeBuffer],ch,width);
#endif
            oled_cur_y[oled_writeBuffer] += glyphHeight;
            oled_cur_x[oled_writeBuffer] = 0;
        }
        oled_cur_x[oled_writeBuffer] += oledGlyphDraw(oled_cur_x[oled_writeBuffer], oled_cur_y[oled_writeBuffer], ch, oled_foreground, oled_background);
    }
}


/**
 * Stream putchar implementation.  This is used to implement
 * a FILE stream for stdout enabling printf() and printf_P()
 * to output to the OLED display
 * @param ch - the character to print
 * @param stream - stream to print on (not used).
 * @note this is aninternal function
 */
int oledFputc(char ch, FILE *stream)
{
    oledPutch((char)ch);

    return ch;
}


/**
 * Set the current X and Y position in the display
 * @param col - X (column position)
 * @param row - Y (row position)
 */
void oledSetXY(uint8_t x, uint8_t y)
{
    if (y >= OLED_HEIGHT*2) 
    {
        y = OLED_HEIGHT*2 - 1;
    }

    oled_cur_x[oled_writeBuffer] = x;
    oled_cur_y[oled_writeBuffer] = y;
}

/**
 * Set the current X position in the display
 * @param col - X (column position)
 */
void oledSetX(uint8_t x)
{
    oled_cur_x[oled_writeBuffer] = x;
}


/**
 * Initialise the OLED hardware and get it ready for use.
 */
void oledInit()
{

    for (uint16_t ind=0; ind < sizeof(oled_init); ) 
    {
        oledWriteCommand(pgm_read_byte(&oled_init[ind++]));
        uint8_t dataSize = pgm_read_byte(&oled_init[ind++]);
        while (dataSize--) 
        {
            oledWriteData(pgm_read_byte(&oled_init[ind++]));
        }
    }

    pinLow(OLED_PORT_POWER, OLED_POWER);	 // 12V power on
    oledWriteCommand(CMD_SET_DISPLAY_ON);
}

/**
 * Write a command or register address to the display
 * @param reg - the command or register to write
 */
void oledWriteCommand(uint8_t reg)
{
    pinLow(OLED_PORT_CS, OLED_CS);
    pinLow(OLED_PORT_DC, OLED_DC);
    spiUsartTransfer(reg);
    pinHigh(OLED_PORT_CS, OLED_CS);
}

/**
 * Write a byte of data to the display
 * @param data - data to write
 */
void oledWriteData(uint8_t data)
{
    pinLow(OLED_PORT_CS, OLED_CS);
    pinHigh(OLED_PORT_DC, OLED_DC);
    spiUsartTransfer(data);
    pinHigh(OLED_PORT_CS, OLED_CS);
}

/**
 * Set the current pixel data column start and end address
 * @param start - start column
 * @param end - end column
 */
void oledSetColumnAddr(uint8_t start, uint8_t end)
{
    oledWriteCommand(CMD_SET_COLUMN_ADDR);
    oledWriteData(start);
    oledWriteData(end);
}

/**
 * Set the current pixel data row start and end address
 * @param start - start row
 * @param end - end row
 */
void oledSetRowAddr(uint8_t start, uint8_t end)
{
#ifdef OLED_DEBUG
    usbPrintf_P(PSTR("    setRowAddr(start=%d,end=%d)\n"), start & OLED_Y_MASK, end & OLED_Y_MASK);
#endif

    oledWriteCommand(CMD_SET_ROW_ADDR);
    oledWriteData(start & OLED_Y_MASK);
    oledWriteData(end & OLED_Y_MASK);
}

/**
 * Set the current pixel data window.
 * Data writes will update only this section of the display.
 * @param x - start row
 * @param y - start column
 * @param xend - end row
 * @param yend - end column
 */
void oledSetWindow(uint8_t x, uint8_t y, uint16_t xend, uint8_t yend)
{
#ifdef OLED_DEBUG
    usbPrintf_P(PSTR("    setColAddr(min=%d,max=%d)\n"), MIN_SEG + x / 4, MIN_SEG + xend / 4);
#endif
    oledSetColumnAddr(MIN_SEG + x / 4, MIN_SEG + xend / 4);
    oledSetRowAddr(y, yend);
}

/**
 * Set the display pixel row offset.  Can be used to scroll the display.
 * Effectively moves y=0 to the offset y row.  The display wraps around to y=63.
 * @param offset - set y origin to this offset
 */
void oledSetOffset(uint8_t offset)
{
    oledWriteCommand(CMD_SET_DISPLAY_OFFSET);
    oledWriteData(offset);
}

/**
 * Get the current display offset (y origin).
 * @returns current y offset
 */
uint8_t oledGetOffset(void)
{
    return oled_offset;
}


/**
 * Set the font to use
 * @param font - new font to use
 */
void oledSetFont(uint8_t font)
{
    oled_fontp = &fontsHQ[font];
}


/**
 * Turn the display off
 */
void oledOff(void)
{
    //oledWriteCommand(CMD_SET_DISPLAY_OFF);
    //pinHigh(OLED_PORT_POWER, OLED_POWER);	 // 12V power off
}


/**
 * Turn the display on
 */
void oledOn(void)
{
    oledWriteCommand(CMD_SET_DISPLAY_ON);
    pinLow(OLED_PORT_POWER, OLED_POWER);	 // 12V power on
}


/**
 * Fill the display with the specified colour by setting
 * every pixel to the colour.
 * @param colour - fill the display with this colour.
 */
void oledFill(uint8_t colour)
{
    uint8_t x,y;
    oledSetColumnAddr(MIN_SEG, MAX_SEG);	// SEG0 - SEG479
    oledSetRowAddr(oled_writeOffset+oled_offset, oled_writeOffset+oled_offset+(OLED_HEIGHT-1));	

    colour = (colour & 0x0F) | (colour << 4);;

    oledWriteCommand(CMD_WRITE_RAM);
    for (y=0; y<64; y++) 
    {
        for (x=0; x<64; x++) 
        {
            oledWriteData(colour);
            oledWriteData(colour);
        }
    }
}


/**
 * Fill a rectangular section of the screen with a specific colour.
 * @param x starting X coordinate
 * @param y starting Y coordinate
 * @param width width of rectangle in pixels
 * @param height height of rectangle in pixels
 * @param colour the shade to fill with (0 to 15)
 */
void oledFillXY(uint8_t x, int16_t y, uint16_t width, uint8_t height, uint8_t colour)
{
    int16_t y_actual = y + oled_offset;
#ifdef OLED_DEBUG
    usbPrintf_P(PSTR("fillXY() x=%u, y=%d, width=%u, height=%u, colour=%u\n"), x, y, width, height, colour);
#endif

    colour = (colour & 0x0F) | (colour << 4);;

    if (width + x > OLED_WIDTH) 
    {
        // overlap on X, split it
        oledFillXY(x, y, OLED_WIDTH-x, height, colour);
        width -= OLED_WIDTH - x;
        x = 0;
    }

    if (y_actual < 0) 
    {
        y_actual += OLED_HEIGHT*2;
    }
    y_actual &= OLED_Y_MASK;
    if (y_actual+height > OLED_HEIGHT*2) 
    {
        // fill area overlaps end of GDDRAM, so two fills needed
        
        // Fill to the end
        oledFillXY(x, y_actual-oled_offset, width, OLED_HEIGHT*2 - y_actual, colour);

        // Now fill the rest from the start of the buffer
        height -= (OLED_HEIGHT*2 - y_actual);
        y_actual = 0;
    }

#ifdef OLED_DEBUG
    usbPrintf_P(PSTR("fill y=%u, to %u\n"), y_actual, y_actual+height-1);
#endif
    oledSetWindow(x, y_actual, x+width-1, y_actual+height-1);
    oledWriteCommand(CMD_WRITE_RAM);
    for (; height > 0; height--) 
    {
        // XXX TODO: handle X non-multiple of 4
        for (uint8_t xind=0; xind<(width+3)/4; xind++) 
        {
            // fill four pixels
            oledWriteData(colour);
            oledWriteData(colour);
        }
        gddram[y_actual].xaddr = 0;
        gddram[y_actual].pixels = 0;
    }
#ifdef OLED_DEBUG
        usbPrintf_P(PSTR("fill done"));
#endif
}


/**
 * Clear the display by setting every pixel to the background colour.
 */
void oledClear()
{
    oledFill(oled_background);

    for (uint8_t ind=0; ind<OLED_HEIGHT; ind++)
    {
        gddram[(oled_offset + oled_writeOffset + ind) & OLED_Y_MASK].xaddr = 0;
        gddram[(oled_offset + oled_writeOffset + ind) & OLED_Y_MASK].pixels = 0;
    }

    oled_cur_x[oled_writeBuffer] = 0;
    oled_cur_y[oled_writeBuffer] = 0;
}

/**
 * Clear the specified line.  This function sets all the pixels in the
 * current text line to the background colour.
 * @param y first row of the text line to clear
 */
void oledClearLine(int16_t y)
{
    uint8_t gheight = oledGlyphHeight();

#ifdef OLED_DEBUG
    uint16_t cur_y = (oled_writeOffset + oled_offset + y) & OLED_Y_MASK;
    usbPrintf_P(PSTR("clearLine() y=%u (%u), oled_offset=%u, oled_writeOffset=%u, gh=%u\n"), y, cur_y, oled_offset, oled_writeOffset, gheight);
#endif
    oledFillXY(0, y, OLED_WIDTH, gheight, oled_background);
}

/**
 * Clear the display by setting every pixel to the background colour.
 * @param options - scroll options:
 *                OLED_SCROLL_UP - scroll up to clear
 *                OLED_SCROLL_DOWN - scroll down to clear
 */
void oledScrollClear(uint8_t options)
{
    uint8_t pixels = oled_background << 4 | oled_background;

    if (options & OLED_SCROLL_DOWN) 
    {
        for (int8_t y=OLED_HEIGHT-1; y>=0; y--) 
        {
            oledSetWindow(0, y+oled_offset, OLED_WIDTH-1, y+oled_offset);
            oledWriteCommand(CMD_WRITE_RAM);
            for (uint8_t x=0; x<(OLED_WIDTH/4); x++) 
            {
                oledWriteData(pixels);
                oledWriteData(pixels);
            }
            oledMoveDisplayStartLine(-1);
            gddram[(y+oled_offset) & OLED_Y_MASK].xaddr = 0;
            gddram[(y+oled_offset) & OLED_Y_MASK].pixels = 0;
            delay_ms(oled_scroll_delay);
        }
    }
    else 
    {
        for (uint8_t y=0; y<OLED_HEIGHT; y++) 
        {
            oledSetWindow(0, y+oled_offset, OLED_WIDTH-1, y+oled_offset);
            oledWriteCommand(CMD_WRITE_RAM);
            for (uint8_t x=0; x<(OLED_WIDTH/4); x++) 
            {
                oledWriteData(pixels);
                oledWriteData(pixels);
            }
            //_delay_us(1000);
            oledMoveDisplayStartLine(1);
            gddram[(y+oled_offset) & OLED_Y_MASK].xaddr = 0;
            gddram[(y+oled_offset) & OLED_Y_MASK].pixels = 0;
            delay_ms(oled_scroll_delay);
        }
    }

    oled_cur_x[oled_writeBuffer] = 0;
    oled_cur_y[oled_writeBuffer] = 0;
}

/**
 * Scroll the display up
 * @param lines the number of pixel lines to scroll
 *        Set to 0 for current font height.
 * @param clear if true then clear the lines that have
 *        scrolled offscreen
 */
void oledScrollUp(uint8_t lines, bool clear)
{
    if (lines == 0) 
    {
        // defaults to the height of the current font
        lines = oledGlyphHeight();
    }
    oledMoveDisplayStartLine(lines);
    if (clear) 
    {
        // clear the lines that are now offscreen
        oledClearLine(-lines);
    }
}

/**
 * Reset the OLED display.
 */
void oledReset()
{
    pinLow(OLED_PORT_RESET, OLED_nRESET);
    _delay_ms(100);
    pinHigh(OLED_PORT_RESET, OLED_nRESET);
    _delay_ms(10);
}


/**
 * Return the width of a printf formatted string in pixels.
 * @param fmt - pointer to the printf format string in RAM
 * @returns the pixel width of the string after parameter substituation
 * @note maxium string length is limited to 64 characters
 */
uint16_t oledGetTextWidth(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[64];
    uint16_t width = 0;

    if (vsnprintf(buf, sizeof(buf), fmt, ap) > 0) 
    {
        for (uint8_t ind=0; buf[ind] != 0; ind++) 
        {
            width += oledGlyphWidth(buf[ind]);
        }
    }

    return width;
} 


/**
 * Return the width of a printf formatted flash string in pixels.
 * @param fmt - pointer to the printf format string in progmem
 * @returns the pixel width of the string after parameter substituation
 * @note maxium string length is limited to 64 characters
 */
uint16_t oledGetTextWidth_P(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[64];
    uint16_t width = 0;

    if (vsnprintf_P(buf, sizeof(buf), fmt, ap) > 0) 
    {
        for (uint8_t ind=0; buf[ind] != 0; ind++) 
        {
            width += oledGlyphWidth(buf[ind]);
        }
    }

    return width;
} 


/**
 * Return the width of the specified character in the current font.
 * @param ch - return the width of this character
 * @returns width of the glyph
 */
uint8_t oledGlyphWidth(char ch)
{
    if (oled_fontp) 
    {
        uint8_t width = oled_fontp->fixedWidth;
        if (width) 
        {
            return width;
        }
        else 
        {
            uint8_t gind;
            if (ch >= ' ' && ch <= 0x7f) 
            {
                gind = pgm_read_byte(&oled_fontp->map[ch - ' ']);
            }
            else 
            {
                // default to a space
                gind = 0;
            }
            return (uint8_t)pgm_read_byte(&(oled_fontp->fontData.glyphs[gind].width));
        }
    }
    else 
    {
        return 0;
    }
}

/**
 * Return the height of the current font. All characters in a font are the same height.
 * @returns height of the glyph font
 */
uint8_t oledGlyphHeight()
{
    if (oled_fontp) 
    {
        return oled_fontp->height + (oled_fontp->fixedWidth ?  1 : 0);
    }
    else 
    {
        return 0;
    }
}


/**
 * Draw a character glyph on the screen at x,y.
 * @param x - x position to start glyph (x=0 for left, x=256-glyphWidth() for right)
 * @param y - y position to start glyph (y=0 for top, y=64-glyphHeight() for bottom)
 * @param ch - the character to draw
 * @param colour - colour [deprecated]
 * @param bg - background colour [deprecated]
 * @returns width of the glyph
 * @note to conserve memory the pixel buffer kept in RAM is only 16 bits wide by 64.  It
 *       keeps track of the right-most GDDRAM cell written.
 *       This means the buffer will only work when writing new graphical data to the
 *       right of the last data written (e.g. when drawing a line of text).
 */
uint8_t oledGlyphDraw(int16_t x, int16_t y, char ch, uint16_t colour, uint16_t bg)
{
    //glyph_t glyphDef;
    uint8_t xoff;
    const uint8_t *glyph;
    uint8_t glyph_width;
    uint8_t glyph_height;
    uint8_t glyph_depth;
    int8_t glyph_offset = 0;
    int8_t glyph_shift;
    uint8_t gind;
    uint16_t pixel_scale;

#ifdef OLED_DEBUG
    usbPrintf_P(PSTR("oled_glyphDraw(x=%d,y=%d,ch='%c')\n"), x, y, ch);
#endif

    if (oled_fontp == NULL) 
    {
        return 0;
    }

    if (colour == bg) 
    {
        bg = 0;
    }


    // get glyph index
    if (ch >= ' ' && ch <= 0x7f)   // XXX replace with size of asciimap
    {
        gind = pgm_read_byte(&oled_fontp->map[ch - ' ']);
        if (gind == 255) 
        {
            // no character, use space
            gind = 0;
        }
    }
    else 
    {
        // default to a space
        gind = 0;
    }

    glyph_depth = oled_fontp->depth;
    glyph_shift = 8 - glyph_depth;
    glyph_width = oled_fontp->fixedWidth;
    pixel_scale = (oled_foreground<<1) / ((1<<glyph_depth)-1);
    if (glyph_width) 
    {
        // Fixed width font
        glyph_height = oled_fontp->height;
        glyph = oled_fontp->fontData.bitmaps + gind*((glyph_width+7)/8) * glyph_height;
#ifdef OLED_DEBUG
        usbPrintf_P(PSTR("    glyph 0x%04x index %d\n"), 
                (uint16_t)oled_fontp->fontData.bitmaps, gind*((glyph_width+7)/8) * glyph_height);
#endif
    }
    else 
    {
        //memcpy_PF(&glyphDef, &oled_fontp->fontData.glyphs[gind]);
        // proportional width font
        glyph = (const uint8_t *)pgm_read_word(&oled_fontp->fontData.glyphs[gind].glyph);
        if (glyph == NULL) 
        {
            // space character, just fill in the gddram buffer and output background pixels
            glyph_width = (uint8_t)pgm_read_byte(&(oled_fontp->fontData.glyphs[gind].width));
            glyph_height = oled_fontp->height;
        }
        else 
        {
            glyph_width = (uint8_t)pgm_read_byte(&(oled_fontp->fontData.glyphs[gind].xrect));
            glyph_height = (uint8_t)pgm_read_byte(&(oled_fontp->fontData.glyphs[gind].yrect));
            glyph_offset = (int8_t)pgm_read_byte(&(oled_fontp->fontData.glyphs[gind].xoffset));
            x += glyph_offset;
            y += (int8_t) pgm_read_byte(&(oled_fontp->fontData.glyphs[gind].yoffset));
            if (x < 0) 
            {
                x = 0;
            }
            if (y < 0) 
            {
                y = 0;
            }
        }
    }
    xoff = x % 4;
#ifdef OLED_DEBUG
    usbPrintf_P(PSTR("    glyph width %d height %d depth %d xoff %d\n"), glyph_width, glyph_height, glyph_depth, xoff);
#endif

#if 0
    if ((y+glyph_height) > OLED_HEIGHT) 
    {
        glyph_height = OLED_HEIGHT-y+1;
    }
#endif

    // adjust y for the current write buffer target
    y += (oled_offset + oled_writeOffset) & OLED_Y_MASK;
#ifdef OLED_DEBUG
    usbPrintf_P(PSTR("    window(x1=%d,y1=%d,x2=%d,y2=%d)\n"), x, y, x+glyph_width-1, y+glyph_height-1);
#endif

    // XXX todo: fill unused character space with background
    for (uint8_t yind=0; yind < glyph_height; yind++) 
    {
        uint16_t xind = 0;
        uint16_t pixels = 0;
        uint8_t xcount = 0;
        uint8_t glyph_pixels = 0;
        uint8_t glyph_byte = 0;
        uint8_t y_actual = (y + yind) & OLED_Y_MASK;
        oledSetWindow(x, y_actual, x+glyph_width-1, y_actual);
        oledWriteCommand(CMD_WRITE_RAM);
        if (xoff != 0) 
        {
            // fill the rest of the 4-pixel word from the bitmap
            for (; xind < 4-xoff; xind++) 
            {
                if (glyph_pixels <= 0) 
                {
                    glyph_byte = glyph ? pgm_read_byte(glyph++) : 0;
                    glyph_pixels = 8 / glyph_depth;
#ifdef OLED_DEBUG
                    usbPrintf_P(PSTR("    byte 0x%02x pixels %d\n"), glyph_byte, glyph_pixels);
#endif
                }
                pixels = pixels << 4 | (((glyph_byte >> glyph_shift) * pixel_scale) >> 1);
                glyph_byte <<= glyph_depth;
                glyph_pixels--;
            }

            // Fill existing pixels if available
            if ((x/4) == gddram[y_actual].xaddr) 
            {
#ifdef OLED_DEBUG
                usbPrintf_P(PSTR("    pixel 0x%04x | gddram[%d][%d] 0x%04x\n"), 
                        pixels, y_actual, gddram[y_actual].xaddr, gddram[y_actual].pixels);
#endif
                pixels |= gddram[y_actual].pixels;
            }
            oledWriteData((uint8_t)(pixels >> 8));
            oledWriteData((uint8_t)pixels);
#ifdef OLED_DEBUG
            usbPrintf_P(PSTR("    pixels = 0x%04x, xoff=%d\n"), pixels, xoff);
#endif
            if (pixels != 0) 
            {
                gddram[y_actual].pixels = pixels;
                gddram[y_actual].xaddr = (x/4)+xcount;
            }
            xcount++;
        }
        for (; xind < glyph_width; xind+=4) 
        {
            for (uint8_t pind=0; pind<4; pind++) 
            {
                pixels <<= 4;
                if (xind+pind < glyph_width) 
                {
                    if (glyph_pixels <= 0) 
                    {
                        glyph_byte = glyph ? pgm_read_byte(glyph++) : 0;
                        glyph_pixels = 8 / glyph_depth;
#ifdef OLED_DEBUG
                    usbPrintf_P(PSTR("    byte 0x%02x pixels %d\n"), glyph_byte, glyph_pixels);
#endif
                    }
                    pixels |= ((glyph_byte >> glyph_shift) * pixel_scale) >> 1;
                    glyph_byte <<= glyph_depth;
                    glyph_pixels--;
                }
            }
            oledWriteData((uint8_t)(pixels >> 8));
            oledWriteData((uint8_t)pixels);
#ifdef OLED_DEBUG
            usbPrintf_P(PSTR("    pixels = 0x%04x, yind=%d (%d), xind=%d\n"), pixels, yind, y_actual, xind);
#endif
            if (pixels != 0) 
            {
                gddram[y_actual].pixels = pixels;
                gddram[y_actual].xaddr = (x/4)+xcount;
            }
            xcount++;
        }
    }

    return (uint8_t)(glyph_width + glyph_offset) + 1;
}


/**
 * Draw a rectangular bitmap on the screen at x,y.
 * @param x - x position for the bitmap
 * @param y - y position for the bitmap (0=top, 63=bottom)
 * @param width - width of bitmap in pixels
 * @param height - height of bitmap in pixels
 * @param depth - number of bits per pixel (1 to 4)
 * @param flags - 1 = RLE compressed bitmap
 * @param image - pointer to the image data in flash
 * @param options - display options:
 *                OLED_SCROLL_UP - scroll bitmap up
 *                OLED_SCROLL_DOWN - scroll bitmap up
 *                OLED_RAM_BITMAP - bitmap is in RAM, not flash
 *                0 - don't make bitmap active (unless already drawing to active buffer)
 * @note to conserve memory the pixel buffer kept in RAM is only 16 bits wide by 64.  It
 *       keeps track of the right-most GDDRAM cell written.
 *       This means the buffer will only work when writing new graphical data to the
 *       right of the last data written (e.g. when drawing a line of text).
 */
void oledBitmapDrawRaw(
    uint8_t x,
    uint8_t y,
    bitstream_t *bs,
    uint8_t options)
{
    uint16_t width = bs->width;
    uint8_t height = bs->height;
    uint8_t xoff = x - (x / 4) * 4;

    usbPrintf_P(PSTR("oled: draw x=%u y=%u width=%u height=%u\n"), x, y, width, height);

    y = (y + oled_offset + oled_writeOffset) & OLED_Y_MASK;

    if (!(options & (OLED_SCROLL_UP | OLED_SCROLL_DOWN))) 
    {
        oledSetWindow(x, y, x+bs->width-1, y+height-1);
        oledWriteCommand(CMD_WRITE_RAM);
    }

    for (uint8_t yind=0; yind < height; yind++) 
    {
        uint16_t xind = 0;
        uint16_t pixels = 0;
        uint8_t xcount = 0;

        if (options & OLED_SCROLL_UP) 
        {
            oledSetWindow(x, y+yind, x+width-1, y+yind);
            oledWriteCommand(CMD_WRITE_RAM);
        }

        if (xoff != 0) 
        {
            // fill the rest of the 4-pixel word from the bitmap
            pixels = bsRead(bs, 4-xoff);

            // Fill existing pixels if available
            if ((x/4) == gddram[(y+yind) & OLED_Y_MASK].xaddr) 
            {
                pixels |= gddram[(y+yind) & OLED_Y_MASK].pixels;
            }

            oledWriteData((uint8_t)(pixels >> 8));
            oledWriteData((uint8_t)pixels);
            if (pixels != 0) 
            {
                gddram[(y+yind) & OLED_Y_MASK].pixels = pixels;
                gddram[(y+yind) & OLED_Y_MASK].xaddr = (x/4)+xcount;
            }
            xcount++;
        }
        for (; xind < width; xind+=4) 
        {
            if (xind+4 < width) 
            {
                pixels = bsRead(bs,4);
            }
            else 
            {
                pixels = bsRead(bs,width-xind);
            }
            oledWriteData((uint8_t)(pixels >> 8));
            oledWriteData((uint8_t)pixels);
            if (pixels != 0) 
            {
                gddram[(y+yind) & OLED_Y_MASK].pixels = pixels;
                gddram[(y+yind) & OLED_Y_MASK].xaddr = (x/4)+xcount;
            }
            xcount++;
        }

        if (options & OLED_SCROLL_UP) 
        {
            oledMoveDisplayStartLine(1);
            delay_ms(oled_scroll_delay);
        }
    }
    if (options & OLED_SCROLL_UP) 
    {
        // alternte buffer is now active
        //oledFlipBuffers(0,0);
        oled_displayBuffer = !oled_displayBuffer;
        oled_writeBuffer = !oled_writeBuffer;
    }
}

/**
 * Draw a rectangular bitmap on the screen at x,y.
 * @param x - x position for the bitmap
 * @param y - y position for the bitmap (0=top, 63=bottom)
 * @param image - pointer to a bitmap_t image data structure
 * @param options - display options:
 *                OLED_SCROLL_UP - scroll bitmap up
 *                OLED_SCROLL_DOWN - scroll bitmap up
 *                0 - don't make bitmap active (unless already drawing to active buffer)
 */
void oledBitmapDraw(uint8_t x, uint8_t y, const void *image, uint8_t options)
{
    const bitmap_t *bitmap = (const bitmap_t *)image;
    bitstream_t bs;

    bsInit(&bs, pgm_read_word(&bitmap->depth), pgm_read_byte(&bitmap->flags), 
            bitmap->data, pgm_read_byte(&bitmap->height), pgm_read_byte(&bitmap->depth),
            !(options & OLED_RAM_BITMAP), 0);

    oledBitmapDrawRaw(x, y, &bs, options);
}

/**
 * Draw a bitmap from a Flash storage slot.
 * @param x - x position for the bitmap
 * @param y - y position for the bitmap (0=top, 63=bottom)
 * @param addr - address of the bitmap in flash
 * @param options - display options:
 *                OLED_SCROLL_UP - scroll bitmap up
 *                OLED_SCROLL_DOWN - scroll bitmap up
 *                0 - don't make bitmap active (unless already drawing to active buffer)
 */
int8_t oledBitmapDrawFlash(uint8_t x, uint8_t y, uint8_t fileId, uint8_t options)
{
    bitstream_t bs;
    bitmap_t bitmap;

    uint32_t fileCount, addr;
    flashRawRead((uint8_t *)&fileCount, FLASH_PAGE_MAPPING_GFX_START*BYTES_PER_PAGE, sizeof(fileCount));
    usbPrintf_P(PSTR("oled: File count is %u, fileId %u\n"), fileCount, fileId);

    if (fileId >= fileCount)
    {
        // invalid file index
        usbPrintf_P(PSTR("File index %u is invalid. File count is %u\n"), fileId, fileCount);
        return -1;
    }

    flashRawRead((uint8_t *)&addr,
            FLASH_PAGE_MAPPING_GFX_START*BYTES_PER_PAGE + fileCount * sizeof(uint32_t) + sizeof(uint32_t),
            sizeof(fileCount));

    usbPrintf_P(PSTR("oled: fileId %u address 0x%04x\n"), fileId, addr);

    flashRawRead((uint8_t *)&bitmap, addr, sizeof(bitmap));
    bsInit(&bs, bitmap.depth, bitmap.flags, (uint16_t *)sizeof(bitmap), bitmap.height, bitmap.depth,
            !(options & OLED_RAM_BITMAP), addr+sizeof(bitmap));

    oledBitmapDrawRaw(x, y, &bs, options);
    return 0;
}
