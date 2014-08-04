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

    File: liblte_rlc.cc

    Description: Contains all the implementations for the LTE Radio Link
                 Control Layer library.

    Revision History
    ----------    -------------    --------------------------------------------
    06/15/2014    Ben Wojtowicz    Created file.
    08/03/2014    Ben Wojtowicz    Added NACK support and using the common
                                   value_2_bits and bits_2_value functions.

*******************************************************************************/

/*******************************************************************************
                              INCLUDES
*******************************************************************************/

#include "liblte_rlc.h"

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
    PDU Type: Unacknowledged Mode Data PDU

    Document Reference: 36.322 v10.0.0 Section 6.2.1.3
*********************************************************************/
// FIXME

/*********************************************************************
    PDU Type: Acknowledged Mode Data PDU

    Document Reference: 36.322 v10.0.0 Sections 6.2.1.4 & 6.2.1.5
*********************************************************************/
LIBLTE_ERROR_ENUM liblte_rlc_pack_amd_pdu(LIBLTE_RLC_AMD_PDU_STRUCT *amd,
                                          LIBLTE_BIT_MSG_STRUCT     *pdu)
{
    return(liblte_rlc_pack_amd_pdu(amd, &amd->data, pdu));
}
LIBLTE_ERROR_ENUM liblte_rlc_pack_amd_pdu(LIBLTE_RLC_AMD_PDU_STRUCT *amd,
                                          LIBLTE_BIT_MSG_STRUCT     *data,
                                          LIBLTE_BIT_MSG_STRUCT     *pdu)
{
    LIBLTE_ERROR_ENUM  err     = LIBLTE_ERROR_INVALID_INPUTS;
    uint8             *pdu_ptr = pdu->msg;

    if(amd  != NULL &&
       data != NULL &&
       pdu  != NULL)
    {
        // Header
        value_2_bits(amd->hdr.dc,                            &pdu_ptr,  1);
        value_2_bits(amd->hdr.rf,                            &pdu_ptr,  1);
        value_2_bits(amd->hdr.p,                             &pdu_ptr,  1);
        value_2_bits(amd->hdr.fi,                            &pdu_ptr,  2);
        value_2_bits(LIBLTE_RLC_E_FIELD_HEADER_NOT_EXTENDED, &pdu_ptr,  1);
        value_2_bits(amd->hdr.sn,                            &pdu_ptr, 10);

        // Data
        memcpy(pdu_ptr, data->msg, data->N_bits);
        pdu_ptr += data->N_bits;

        // Fill in the number of bits used
        pdu->N_bits = pdu_ptr - pdu->msg;

        err = LIBLTE_SUCCESS;
    }

    return(err);
}
LIBLTE_ERROR_ENUM liblte_rlc_unpack_amd_pdu(LIBLTE_BIT_MSG_STRUCT     *pdu,
                                            LIBLTE_RLC_AMD_PDU_STRUCT *amd)
{
    LIBLTE_ERROR_ENUM        err     = LIBLTE_ERROR_INVALID_INPUTS;
    uint8                   *pdu_ptr = pdu->msg;
    LIBLTE_RLC_E_FIELD_ENUM  e;

    if(pdu != NULL &&
       amd != NULL)
    {
        // Header
        amd->hdr.dc = (LIBLTE_RLC_DC_FIELD_ENUM)bits_2_value(&pdu_ptr, 1);

        if(LIBLTE_RLC_DC_FIELD_DATA_PDU == amd->hdr.dc)
        {
            // Header
            amd->hdr.rf = (LIBLTE_RLC_RF_FIELD_ENUM)bits_2_value(&pdu_ptr, 1);
            amd->hdr.p  = (LIBLTE_RLC_P_FIELD_ENUM)bits_2_value(&pdu_ptr, 1);
            amd->hdr.fi = (LIBLTE_RLC_FI_FIELD_ENUM)bits_2_value(&pdu_ptr, 2);
            e           = (LIBLTE_RLC_E_FIELD_ENUM)bits_2_value(&pdu_ptr, 1);
            amd->hdr.sn = bits_2_value(&pdu_ptr, 10);

            if(LIBLTE_RLC_RF_FIELD_AMD_PDU_SEGMENT == amd->hdr.rf)
            {
                // FIXME
                printf("Not handling AMD PDU SEGMENTS\n");
            }

            if(LIBLTE_RLC_E_FIELD_HEADER_EXTENDED == e)
            {
                // FIXME
                printf("Not handling HEADER EXTENSION\n");
            }

            // Data
            amd->data.N_bits = pdu->N_bits - (pdu_ptr - pdu->msg);
            memcpy(amd->data.msg, pdu_ptr, amd->data.N_bits);

            err = LIBLTE_SUCCESS;
        }else{
            // FIXME: Signal that this is a status PDU
        }
    }

    return(err);
}

/*********************************************************************
    PDU Type: Status PDU

    Document Reference: 36.322 v10.0.0 Section 6.2.1.6
*********************************************************************/
LIBLTE_ERROR_ENUM liblte_rlc_pack_status_pdu(LIBLTE_RLC_STATUS_PDU_STRUCT *status,
                                             LIBLTE_BIT_MSG_STRUCT        *pdu)
{
    LIBLTE_ERROR_ENUM  err     = LIBLTE_ERROR_INVALID_INPUTS;
    uint8             *pdu_ptr = pdu->msg;
    uint32             i;

    if(status != NULL &&
       pdu    != NULL)
    {
        // D/C Field
        value_2_bits(LIBLTE_RLC_DC_FIELD_CONTROL_PDU, &pdu_ptr, 1);

        // CPT Field
        value_2_bits(LIBLTE_RLC_CPT_FIELD_STATUS_PDU, &pdu_ptr, 3);

        // ACK SN
        value_2_bits(status->ack_sn, &pdu_ptr, 10);

        // E1
        if(status->N_nack == 0)
        {
            value_2_bits(LIBLTE_RLC_E1_FIELD_NOT_EXTENDED, &pdu_ptr, 1);
        }else{
            value_2_bits(LIBLTE_RLC_E1_FIELD_EXTENDED, &pdu_ptr, 1);
        }

        for(i=0; i<status->N_nack; i++)
        {
            // NACK SN
            value_2_bits(status->nack_sn[i], &pdu_ptr, 10);

            // E1
            if(i == (status->N_nack-1))
            {
                value_2_bits(LIBLTE_RLC_E1_FIELD_NOT_EXTENDED, &pdu_ptr, 1);
            }else{
                value_2_bits(LIBLTE_RLC_E1_FIELD_EXTENDED, &pdu_ptr, 1);
            }

            // E2
            value_2_bits(LIBLTE_RLC_E2_FIELD_NOT_EXTENDED, &pdu_ptr, 1);

            // FIXME: Skipping SOstart and SOend
        }

        pdu->N_bits = pdu_ptr - pdu->msg;

        // Padding
        if((pdu->N_bits % 8) != 0)
        {
            for(i=0; i<(8 - (pdu->N_bits % 8)); i++)
            {
                value_2_bits(0, &pdu_ptr, 1);
            }
            pdu->N_bits = pdu_ptr - pdu->msg;
        }

        err = LIBLTE_SUCCESS;
    }

    return(err);
}
LIBLTE_ERROR_ENUM liblte_rlc_unpack_status_pdu(LIBLTE_BIT_MSG_STRUCT        *pdu,
                                               LIBLTE_RLC_STATUS_PDU_STRUCT *status)
{
    LIBLTE_ERROR_ENUM         err     = LIBLTE_ERROR_INVALID_INPUTS;
    uint8                    *pdu_ptr = pdu->msg;
    LIBLTE_RLC_DC_FIELD_ENUM  dc;
    LIBLTE_RLC_E1_FIELD_ENUM  e;
    uint8                     cpt;

    if(pdu    != NULL &&
       status != NULL)
    {
        // D/C Field
        dc = (LIBLTE_RLC_DC_FIELD_ENUM)bits_2_value(&pdu_ptr, 1);

        if(LIBLTE_RLC_DC_FIELD_CONTROL_PDU == dc)
        {
            cpt = bits_2_value(&pdu_ptr, 3);

            if(LIBLTE_RLC_CPT_FIELD_STATUS_PDU == cpt)
            {
                status->ack_sn = bits_2_value(&pdu_ptr, 10);
                e              = (LIBLTE_RLC_E1_FIELD_ENUM)bits_2_value(&pdu_ptr, 1);
                status->N_nack = 0;
                while(LIBLTE_RLC_E1_FIELD_EXTENDED == e)
                {
                    status->nack_sn[status->N_nack++] = bits_2_value(&pdu_ptr, 10);
                    e                                 = (LIBLTE_RLC_E1_FIELD_ENUM)bits_2_value(&pdu_ptr, 1);
                    if(LIBLTE_RLC_E2_FIELD_EXTENDED == bits_2_value(&pdu_ptr, 1))
                    {
                        // FIXME: Skipping SOstart and SOend
                        bits_2_value(&pdu_ptr, 29);
                    }
                }
            }
        }
    }
}
