/*******************************************************************************

    Copyright 2014 Ben Wojtowicz

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*******************************************************************************

    File: liblte_pdcp.cc

    Description: Contains all the implementations for the LTE Packet Data
                 Convergence Protocol Layer library.

    Revision History
    ----------    -------------    --------------------------------------------
    08/03/2014    Ben Wojtowicz    Created file.
    11/01/2014    Ben Wojtowicz    Added integrity protection of messages.

*******************************************************************************/

/*******************************************************************************
                              INCLUDES
*******************************************************************************/

#include "liblte_pdcp.h"
#include "liblte_security.h"

/*******************************************************************************
                              DEFINES
*******************************************************************************/


/*******************************************************************************
                              TYPEDEFS
*******************************************************************************/


/*******************************************************************************
                              GLOBAL VARIABLES
*******************************************************************************/


/*******************************************************************************
                              PDU FUNCTIONS
*******************************************************************************/

/*********************************************************************
    PDU Type: Control Plane PDCP Data PDU

    Document Reference: 36.323 v10.1.0 Section 6.2.2
*********************************************************************/
LIBLTE_ERROR_ENUM liblte_pdcp_pack_control_pdu(LIBLTE_PDCP_CONTROL_PDU_STRUCT *contents,
                                               LIBLTE_BIT_MSG_STRUCT          *pdu)
{
    return(liblte_pdcp_pack_control_pdu(contents, &contents->data, pdu));
}
LIBLTE_ERROR_ENUM liblte_pdcp_pack_control_pdu(LIBLTE_PDCP_CONTROL_PDU_STRUCT *contents,
                                               LIBLTE_BIT_MSG_STRUCT          *data,
                                               LIBLTE_BIT_MSG_STRUCT          *pdu)
{
    return(liblte_pdcp_pack_control_pdu(contents, data, NULL, 0, 0, pdu));
}
LIBLTE_ERROR_ENUM liblte_pdcp_pack_control_pdu(LIBLTE_PDCP_CONTROL_PDU_STRUCT *contents,
                                               uint8                          *key_256,
                                               uint8                           direction,
                                               uint8                           rb_id,
                                               LIBLTE_BIT_MSG_STRUCT          *pdu)
{
    return(liblte_pdcp_pack_control_pdu(contents, &contents->data, key_256, direction, rb_id, pdu));
}
LIBLTE_ERROR_ENUM liblte_pdcp_pack_control_pdu(LIBLTE_PDCP_CONTROL_PDU_STRUCT *contents,
                                               LIBLTE_BIT_MSG_STRUCT          *data,
                                               uint8                          *key_256,
                                               uint8                           direction,
                                               uint8                           rb_id,
                                               LIBLTE_BIT_MSG_STRUCT          *pdu)
{
    LIBLTE_ERROR_ENUM  err     = LIBLTE_ERROR_INVALID_INPUTS;
    uint8             *pdu_ptr = pdu->msg;
    uint8              mac[4];

    if(contents != NULL &&
       data     != NULL &&
       pdu      != NULL)
    {
        // Header
        value_2_bits(0,               &pdu_ptr, 3);
        value_2_bits(contents->count, &pdu_ptr, 5);

        // Data
        memcpy(pdu_ptr, data->msg, data->N_bits);
        pdu_ptr += data->N_bits;

        // Byte align
        if(((pdu_ptr - pdu->msg) % 8) != 0)
        {
            value_2_bits(0, &pdu_ptr, 8-((pdu_ptr - pdu->msg) % 8));
        }

        // MAC
        if(NULL == key_256)
        {
            value_2_bits(LIBLTE_PDCP_CONTROL_MAC_I, &pdu_ptr, 32);
        }else{
            pdu->N_bits = pdu_ptr - pdu->msg;
            liblte_security_128_eia2(&key_256[16],
                                     contents->count,
                                     rb_id,
                                     direction,
                                     pdu,
                                     mac);
            value_2_bits(mac[0], &pdu_ptr, 8);
            value_2_bits(mac[1], &pdu_ptr, 8);
            value_2_bits(mac[2], &pdu_ptr, 8);
            value_2_bits(mac[3], &pdu_ptr, 8);
        }

        // Fill in the number of bits used
        pdu->N_bits = pdu_ptr - pdu->msg;

        err = LIBLTE_SUCCESS;
    }

    return(err);
}
LIBLTE_ERROR_ENUM liblte_pdcp_unpack_control_pdu(LIBLTE_BIT_MSG_STRUCT          *pdu,
                                                 LIBLTE_PDCP_CONTROL_PDU_STRUCT *contents)
{
    LIBLTE_ERROR_ENUM  err     = LIBLTE_ERROR_INVALID_INPUTS;
    uint8             *pdu_ptr = pdu->msg;

    if(pdu      != NULL &&
       contents != NULL)
    {
        // Header
        bits_2_value(&pdu_ptr, 3);
        contents->count = bits_2_value(&pdu_ptr, 5);

        // Data
        memcpy(contents->data.msg, pdu_ptr, pdu->N_bits - 40);
        contents->data.N_bits = pdu->N_bits - 40;

        err = LIBLTE_SUCCESS;
    }

    return(err);
}

/*********************************************************************
    PDU Type: User Plane PDCP Data PDU with long PDCP SN

    Document Reference: 36.323 v10.1.0 Section 6.2.3
*********************************************************************/
// FIXME

/*********************************************************************
    PDU Type: User Plane PDCP Data PDU with short PDCP SN

    Document Reference: 36.323 v10.1.0 Section 6.2.4
*********************************************************************/
// FIXME

/*********************************************************************
    PDU Type: PDCP Control PDU for interspersed ROHC feedback packet

    Document Reference: 36.323 v10.1.0 Section 6.2.5
*********************************************************************/
// FIXME

/*********************************************************************
    PDU Type: PDCP Control PDU for PDCP status report

    Document Reference: 36.323 v10.1.0 Section 6.2.6
*********************************************************************/
// FIXME

/*********************************************************************
    PDU Type: RN User Plane PDCP Data PDU with integrity protection

    Document Reference: 36.323 v10.1.0 Section 6.2.8
*********************************************************************/
// FIXME
