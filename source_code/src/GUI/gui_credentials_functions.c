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
/*!  \file     gui_credentials_functions.c
*    \brief    General user interface - credentials functions
*    Created:  22/6/2014
*    Author:   Mathieu Stephan
*/
#include <string.h>
#include "smart_card_higher_level_functions.h"
#include "touch_higher_level_functions.h"
#include "gui_credentials_functions.h"
#include "gui_screen_functions.h"
#include "gui_basic_functions.h"
#include "logic_aes_and_comms.h"
#include "timer_manager.h"
#include "node_mgmt.h"
#include "node_mgmt.h"
#include "defines.h"
#include "oledmp.h"
#include "delays.h"
#include "anim.h"
#include "gui.h"
#include "usb.h"

// Last first matching parent address we saw
uint16_t last_matching_parent_addr = NODE_ADDR_NULL;
// Last number of matching parent address
uint8_t last_matching_parent_number = 0;


/*! \fn     displayCredentialAtSlot(uint8_t slot, const char* text)
*   \brief  Display text at a given slot when choosing between credentials/favorites
*   \param  slot                The slot number
*   \param  text                The text to display
*/
void displayCredentialAtSlot(uint8_t slot, char* text)
{
    char temp_disptext[40];
    int8_t yoffset = 0;
    
    if (slot & 0x08)
    {
        yoffset =  (slot & 0x02)? -10:10;
    }
    
    // Truncate and display string
    strncpy(temp_disptext, text, sizeof(temp_disptext));
    temp_disptext[sizeof(temp_disptext)-1] = 0;
    temp_disptext[INDEX_TRUNCATE_LOGIN_FAV] = 0;
    oledPutstrXY((slot & 0x01)*0xFF, (slot & 0x02)*23 + yoffset, (slot & 0x01)*OLED_RIGHT, temp_disptext);
}

/*! \fn     guiAskForLoginSelect(pNode* p, cNode* c, uint16_t parentNodeAddress)
*   \brief  Ask for user login selection / approval
*   \param  p                   Pointer to a parent node
*   \param  c                   Pointer to a child node
*   \param  parentNodeAddress   Address of the parent node
*   \return Valid child node address or 0 otherwise
*/
uint16_t guiAskForLoginSelect(pNode* p, cNode* c, uint16_t parentNodeAddress)
{
    uint16_t first_child_address, temp_child_address;
    uint16_t picked_child = NODE_ADDR_NULL;
    uint16_t addresses[4];
    uint8_t led_mask;
    int8_t i, j;
    
    // Check parent node address
    if (parentNodeAddress == NODE_ADDR_NULL)
    {
        return NODE_ADDR_NULL;
    }
    
    // Read the parent node
    readParentNode(p, parentNodeAddress);
    
    // Read its first child address
    first_child_address = p->nextChildAddress;
    
    // Check if there are stored credentials
    if (first_child_address == NODE_ADDR_NULL)
    {
        return NODE_ADDR_NULL;
    }
    
    // Read child node
    readChildNode(c, first_child_address);
    
    // Check if there's only one child, that's a confirmation screen
    if (c->nextChildAddress == NODE_ADDR_NULL)
    {
        confirmationText_t temp_conf_text;
        
        // Prepare asking confirmation screen
        temp_conf_text.lines[0] = readStoredStringToBuffer(ID_STRING_CONFACCESSTO);
        temp_conf_text.lines[1] = (char*)p->service;
        temp_conf_text.lines[2] = readStoredStringToBuffer(ID_STRING_WITHTHISLOGIN);
        temp_conf_text.lines[3] = (char*)c->login;
        
        // Prompt user for confirmation
        if(guiAskForConfirmation(4, &temp_conf_text) == RETURN_OK)
        {
            picked_child = first_child_address;
        }
    }
    else
    {
        temp_child_address = first_child_address;
        uint8_t action_chosen = FALSE;
        
        while (action_chosen == FALSE)
        {
            // Draw asking bitmap
            oledClear();
            oledBitmapDrawFlash(0, 0, BITMAP_LOGIN, 0);
            
            // Write domain name on screen
            oledPutstrXY(0, 24, OLED_CENTRE, (char*)p->service);
            
            // Clear led_mask
            led_mask = 0;
            i = 0;
            
            // List logins on screen
            while ((temp_child_address != NODE_ADDR_NULL) && (i != 4))
            {
                // Read child node to get login
                readChildNode(c, temp_child_address);
                
                // Print Login at the correct slot
                displayCredentialAtSlot(i, (char*)c->login);            
                
                // Store address in array, fetch next address
                addresses[i] = temp_child_address;
                temp_child_address = c->nextChildAddress;
                i++;
            }
            
            // If nothing after, hide right arrow
            if ((i != 4) || (c->nextChildAddress == NODE_ADDR_NULL))
            {
                oledFillXY(174, 24, 22, 18, 0x00);
                led_mask |= LED_MASK_RIGHT;
            }
            
            // Light only the available choices
            for (j = i; j < 4; j++)
            {
                led_mask |= (1 << j);
            }
            
            // Display picture
            oledFlipBuffers(0,0);
            
            // Get touched quarter
            j = getTouchedPositionAnswer(led_mask);
            
            // Check its validity, knowing that by default we will return NODE_ADDR_NULL
            if (j == -1)
            {
                // Time out
                action_chosen = TRUE;
            }
            else if (j < i)
            {
                picked_child = addresses[j];
                action_chosen = TRUE;
            }
            else if (j == TOUCHPOS_LEFT)
            {                
                // If there is a previous children, go back 4 indexes
                if (addresses[0] != first_child_address)
                {
                    c->prevChildAddress = addresses[0];
                    for (i = 0; i < 5; i++)
                    {
                        temp_child_address = c->prevChildAddress;
                        readChildNode(c, temp_child_address);
                    }
                }
                else
                {
                    // otherwise, return
                    action_chosen = TRUE;
                }
            }
            else if ((j == TOUCHPOS_RIGHT) && (i == 4) && (c->nextChildAddress != NODE_ADDR_NULL))
            {
                // If there are more nodes to display, let it loop
                // temp_child_address = c->nextChildAddress;
            }
            else
            {
                // Wrong position
                temp_child_address = addresses[0];
            }
        }
    }
        
    return picked_child;
}

/*! \fn     favoriteSelectionScreen(pNode* p, cNode* c)
*   \brief  Screen displayed to let the user choose a favorite
*   \param  p                   Pointer to a parent node
*   \param  c                   Pointer to a child node
*   \return Valid child node address or 0 otherwise
*/
uint16_t favoriteSelectionScreen(pNode* p, cNode* c)
{
    uint16_t picked_child = NODE_ADDR_NULL;
    uint16_t parentAddresses[USER_MAX_FAV];
    uint16_t childAddresses[USER_MAX_FAV];
    uint16_t tempparaddr, tempchildaddr;
    uint8_t action_chosen = FALSE;
    uint8_t nbFavorites = 0;
    uint8_t offset = 0;
    uint8_t led_mask;
    int8_t i, j;
    
    // Browse through the favorites
    for (i = 0; i < USER_MAX_FAV; i++)
    {
        // Read favorite, check that it is valid
        readFav(i, &tempparaddr, &tempchildaddr);
        
        // If so, store it in our know addresses
        if (tempparaddr != NODE_ADDR_NULL)
        {
            parentAddresses[nbFavorites] = tempparaddr;
            childAddresses[nbFavorites++] = tempchildaddr;
        }
    }    
    
    // If no favorite, return
    if (nbFavorites == 0)
    {
        guiDisplayInformationOnScreen(ID_STRING_NOSTOREDFAV);
        userViewDelay();
        return NODE_ADDR_NULL;
    }
    
    // Loop until the user chooses smth
    while (action_chosen != TRUE)
    {
        // Draw asking bitmap
        oledClear();
        oledBitmapDrawFlash(0, 0, BITMAP_LOGIN, 0);
        
        // Clear led_mask
        led_mask = 0;
        i = 0;
        
        // List logins on screen
        while (((offset + i) < nbFavorites) && (i != 4))
        {
            // Read child node to get login
            readChildNode(c, childAddresses[offset+i]);
            readParentNode(p, parentAddresses[offset+i]);
            
            // Print service / login on screen
            displayCredentialAtSlot(i+((i&0x02)<<2), (char*)c->login);
            displayCredentialAtSlot(i+((~i&0x02)<<2), (char*)p->service);
            
            // Increment i
            i++;
        }
        
        // If nothing after, hide right arrow
        if ((i != 4) || ((offset+i) == nbFavorites))
        {
            oledFillXY(174, 24, 22, 18, 0x00);
            led_mask |= LED_MASK_RIGHT;
        }
        
        // Light only the available choices
        for (j = i; j < 4; j++)
        {
            led_mask |= (1 << j);
        }
        
        // Display picture
        oledFlipBuffers(0,0);
        
        // Get touched quarter
        j = getTouchedPositionAnswer(led_mask);
        
        // Check its validity, knowing that by default we will return NODE_ADDR_NULL
        if (j == -1)
        {
            action_chosen = TRUE;
            // Time out
        }
        else if (j < i)
        {
            // Valid choice
            picked_child = childAddresses[offset+j];
            action_chosen = TRUE;
        }
        else if (j == TOUCHPOS_LEFT)
        {
            if (offset > 0)
            {
                offset -= 4;
            } 
            else
            {
                // User wants to go back
                action_chosen = TRUE;                
            }
        }
        else if ((j == TOUCHPOS_RIGHT) && (i == 4) && ((offset+i) != nbFavorites))
        {
            // If there are more nodes to display
            offset += 4;
        }
    }
    
    // Return selected child
    return picked_child;
}

/*! \fn     displayServiceAtGivenSlot(uint8_t slot, const char* text)
*   \brief  Display text at a given slot when choosing between services
*   \param  slot                The slot number
*   \param  text                The text to display
*/
void displayServiceAtGivenSlot(uint8_t slot, const char* text)
{
    char temp_disptext[40];
    
    // Truncate string and display it
    strncpy(temp_disptext, text, sizeof(temp_disptext));
    temp_disptext[sizeof(temp_disptext)-1] = 0;
    temp_disptext[INDEX_TRUNCATE_SERVICE_SEARCH] = 0;
    oledPutstrXY((slot & 0x01)*0xFF, 4 + (slot & 0x02)*20, (slot & 0x01)*OLED_RIGHT, temp_disptext);    
}

/*! \fn     displayCurrentSearchLoginTexts(char* text)
*   \brief  Display current search login text
*   \param  text            Text to be displayed
*   \param  resultsarray    Pointer to the array in which to store the addresses
*   \return Number of matching parents we displayed
*/
uint8_t displayCurrentSearchLoginTexts(char* text, uint16_t* resultsarray)
{
    uint16_t tempNodeAddr;
    pNode temp_pnode;
    uint8_t i;
    
    // Set font for search text
    oledSetFont(FONT_PROFONT_18);
    
    // Clear current text
    oledFillXY(100, 14, 50, 24, 0x00);
    
    // Display new search text
    oledPutstrXY(148, 14, OLED_RIGHT, text);
    
    // Set default font
    oledSetFont(FONT_DEFAULT);
    
    // Find the address of the first match
    tempNodeAddr = searchForServiceName((uint8_t*)text, COMPARE_MODE_COMPARE);
    
    if (tempNodeAddr != last_matching_parent_addr)
    {
        last_matching_parent_addr = tempNodeAddr;
        
        for (i = 0; i < 4; i++)
        {
            oledFillXY((i&1)*175, 6+(i&2)*19, 81, 14, 0x00);
        }
        
        // Print the next 4 services
        i = 0;
        while ((tempNodeAddr != NODE_ADDR_NULL) && (i != 4))
        {
            resultsarray[i] = tempNodeAddr;
            readParentNode(&temp_pnode, tempNodeAddr);
            displayServiceAtGivenSlot(i, (const char*)temp_pnode.service);
            tempNodeAddr = temp_pnode.nextParentAddress;
            i++;
        }
        
        // Store and return number of children
        last_matching_parent_number = i;
    }
    
    return last_matching_parent_number;
}

/*! \fn     loginSelectionScreen(pNode* p, cNode* c)
*   \brief  Screen displayed to let the user choose/find a login
*   \param  p                   Pointer to a parent node
*   \param  c                   Pointer to a child node
*   \return Valid child node address or 0 otherwise
*/
uint16_t loginSelectionScreen(pNode* p, cNode* c)
{
    char currentText[SEARCHTEXT_MAX_LENGTH+1];
    uint8_t displayRefreshNeeded = TRUE;
    uint16_t ret_val = NODE_ADDR_NULL;
    uint8_t wasWheelReleased = TRUE;
    uint16_t tempParentAddresses[4];
    uint8_t currentStringIndex = 0;
    uint8_t nbMatchedParents= 0;
    uint8_t finished = FALSE;
    RET_TYPE temp_rettype;
    uint8_t led_mask = 0;
    int8_t temp_int8;
    
    // Set current text to a
    last_matching_parent_addr = NODE_ADDR_NULL;
    last_matching_parent_number = 0;
    strcpy(currentText, "a");
    
    // Draw bitmap, display it and write active buffer
    oledClear();
    oledBitmapDrawFlash(0, 0, BITMAP_LOGIN_FIND, 0);
    oledFlipBuffers(0,0);
    oledWriteActiveBuffer();
    
    // Clear possible remaining detection
    touchWaitForButtonsReleased();
    touchClearCurrentDetections();
    
    // Arm interaction timer
    activateTimer(TIMER_USERINT, SELECT_TIMER_DEL);
    
    // While the user hasn't chosen a credential
    while(!finished)
    {
        if (displayRefreshNeeded == TRUE)
        {
            nbMatchedParents = displayCurrentSearchLoginTexts(currentText, tempParentAddresses);
            displayRefreshNeeded = FALSE;
            
            // Light only the available choices
            led_mask = 0;
            for (temp_int8 = nbMatchedParents; temp_int8 < 4; temp_int8++)
            {
                led_mask |= (1 << temp_int8);
            }
        }
        
        // Detect key touches
        temp_rettype = touchDetectionRoutine(led_mask);
        
        // If something happened, rearm timer
        if (temp_rettype != RETURN_NO_CHANGE)
        {
            activateTimer(TIMER_USERINT, SELECT_TIMER_DEL);
        }
        
        // Algo to differentiate a tap from a scroll
        if ((temp_rettype & RETURN_WHEEL_PRESSED) && (wasWheelReleased == TRUE))
        {
            // We use the timer dedicated to wait functions for min
            activateTimer(TIMER_WAIT_FUNCTS, TAP_MIN_DEL);
            // We use the timer dedicated to caps detect for max
            activateTimer(TIMER_CAPS, TAP_MAX_DEL);
            // This works because we use the same clearing flags 
            wasWheelReleased = FALSE;
        }
        else if (temp_rettype & RETURN_WHEEL_RELEASED)
        {
            // Check if it's a tap and that the selected domain is valid
            if ((hasTimerExpired(TIMER_CAPS, FALSE) == TIMER_RUNNING) && (hasTimerExpired(TIMER_WAIT_FUNCTS, TRUE) == TIMER_EXPIRED) && (getWheelTouchDetectionQuarter() < nbMatchedParents))
            {
                ret_val = tempParentAddresses[getWheelTouchDetectionQuarter()];
                finished = TRUE;
            }
            wasWheelReleased = TRUE;
        }
        
        // Send it to the touch wheel interface logic
        temp_int8 = touchWheelIntefaceLogic(temp_rettype);

        // Position increment / decrement
        if (temp_int8 != 0)
        {
            if ((currentText[currentStringIndex] == 0x7A) && (temp_int8 == 1))
            {
                // z->0 wrap
                currentText[currentStringIndex] = 0x2F;
            }
            if ((currentText[currentStringIndex] == 0x39) && (temp_int8 == 1))
            {
                // 9->a wrap
                currentText[currentStringIndex] = 0x60;
            }
            else if ((currentText[currentStringIndex] == 0x30) && (temp_int8 == -1))
            {
                // 0->z wrap
                currentText[currentStringIndex] = 0x7B;
            }
            else if ((currentText[currentStringIndex] == 0x61) && (temp_int8 == -1))
            {
                // a->9 wrap
                currentText[currentStringIndex] = 0x3A;
            }
            currentText[currentStringIndex] += temp_int8;
            displayRefreshNeeded = TRUE;
        }
        
        if ((isSmartCardAbsent() == RETURN_OK) || (hasTimerExpired(TIMER_USERINT, TRUE) == TIMER_EXPIRED))
        {
            // Smartcard removed or no interaction for a while
            finished = TRUE;
        }
        else if (temp_rettype & RETURN_LEFT_PRESSED)
        {
            // Change search index
            if (currentStringIndex > 0)
            {
                currentText[currentStringIndex--] = 0;
                displayRefreshNeeded = TRUE;
            } 
            else
            {
                finished = TRUE;
            }
        }
        else if (temp_rettype & RETURN_RIGHT_PRESSED)
        {
            // Change search index
            if (currentStringIndex < SEARCHTEXT_MAX_LENGTH-1)
            {
                currentText[++currentStringIndex] = 'a';
                currentText[currentStringIndex + 1] = 0;
                displayRefreshNeeded = TRUE;
            }
        }
    }
    
    // Set inactive buffer write by default
    oledWriteInactiveBuffer();
    
    return ret_val;
}    
