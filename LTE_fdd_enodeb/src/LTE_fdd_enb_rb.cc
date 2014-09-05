#line 2 "LTE_fdd_enb_rb.cc" // Make __FILE__ omit the path
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

    File: LTE_fdd_enb_rb.cc

    Description: Contains all the implementations for the LTE FDD eNodeB
                 radio bearer class.

    Revision History
    ----------    -------------    --------------------------------------------
    05/04/2014    Ben Wojtowicz    Created file
    06/15/2014    Ben Wojtowicz    Added more states and procedures, QoS, MME,
                                   RLC, and uplink scheduling functionality.
    08/03/2014    Ben Wojtowicz    Added MME procedures/states, RRC NAS support,
                                   RRC transaction id, PDCP sequence numbers,
                                   and RLC transmit variables.
    09/03/2014    Ben Wojtowicz    Added ability to store the contetion
                                   resolution identity and fixed an issue with
                                   t_poll_retransmit.

*******************************************************************************/

/*******************************************************************************
                              INCLUDES
*******************************************************************************/

#include "LTE_fdd_enb_rb.h"
#include "LTE_fdd_enb_timer_mgr.h"
#include "LTE_fdd_enb_user.h"
#include "LTE_fdd_enb_rlc.h"
#include "LTE_fdd_enb_mac.h"

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
                              CLASS IMPLEMENTATIONS
*******************************************************************************/

/********************************/
/*    Constructor/Destructor    */
/********************************/
LTE_fdd_enb_rb::LTE_fdd_enb_rb(LTE_FDD_ENB_RB_ENUM  _rb,
                               LTE_fdd_enb_user    *_user)
{
    rb   = _rb;
    user = _user;

    ul_sched_timer_id          = LTE_FDD_ENB_INVALID_TIMER_ID;
    t_poll_retransmit_timer_id = LTE_FDD_ENB_INVALID_TIMER_ID;

    if(LTE_FDD_ENB_RB_SRB0 == rb)
    {
        mme_procedure = LTE_FDD_ENB_MME_PROC_IDLE;
        mme_state     = LTE_FDD_ENB_MME_STATE_IDLE;
        rrc_procedure = LTE_FDD_ENB_RRC_PROC_IDLE;
        rrc_state     = LTE_FDD_ENB_RRC_STATE_IDLE;
        pdcp_config   = LTE_FDD_ENB_PDCP_CONFIG_N_A;
        rlc_config    = LTE_FDD_ENB_RLC_CONFIG_TM;
        mac_config    = LTE_FDD_ENB_MAC_CONFIG_TM;
    }else if(LTE_FDD_ENB_RB_SRB1 == rb){
        mme_procedure = LTE_FDD_ENB_MME_PROC_IDLE;
        mme_state     = LTE_FDD_ENB_MME_STATE_IDLE;
        rrc_procedure = LTE_FDD_ENB_RRC_PROC_IDLE;
        rrc_state     = LTE_FDD_ENB_RRC_STATE_IDLE;
        pdcp_config   = LTE_FDD_ENB_PDCP_CONFIG_N_A;
        rlc_config    = LTE_FDD_ENB_RLC_CONFIG_AM;
        mac_config    = LTE_FDD_ENB_MAC_CONFIG_TM;
    }

    // RRC
    rrc_transaction_id = 0;

    // PDCP
    pdcp_rx_sn = 0;
    pdcp_tx_sn = 0;

    // RLC
    rlc_reception_buffer.clear();
    rlc_transmission_buffer.clear();
    rlc_vrr              = 0;
    rlc_vrmr             = rlc_vrr + LIBLTE_RLC_AM_WINDOW_SIZE;
    rlc_vrh              = 0;
    rlc_first_segment_sn = 0xFFFF;
    rlc_last_segment_sn  = 0xFFFF;
    rlc_vta              = 0;
    rlc_vtms             = rlc_vta + LIBLTE_RLC_AM_WINDOW_SIZE;
    rlc_vts              = 0;

    // MAC
    mac_con_res_id = 0;

    // Setup the QoS
    avail_qos[0] = (LTE_FDD_ENB_QOS_STRUCT){ 0,   0};
    avail_qos[1] = (LTE_FDD_ENB_QOS_STRUCT){20, 176};
    qos          = LTE_FDD_ENB_QOS_NONE;
}
LTE_fdd_enb_rb::~LTE_fdd_enb_rb()
{
    LTE_fdd_enb_timer_mgr *timer_mgr = LTE_fdd_enb_timer_mgr::get_instance();

    timer_mgr->stop_timer(ul_sched_timer_id);
}

/******************/
/*    Identity    */
/******************/
LTE_FDD_ENB_RB_ENUM LTE_fdd_enb_rb::get_rb_id(void)
{
    return(rb);
}

/*************/
/*    MME    */
/*************/
void LTE_fdd_enb_rb::queue_mme_nas_msg(LIBLTE_BYTE_MSG_STRUCT *nas_msg)
{
    queue_msg(nas_msg, &mme_nas_msg_queue_mutex, &mme_nas_msg_queue);
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::get_next_mme_nas_msg(LIBLTE_BYTE_MSG_STRUCT **nas_msg)
{
    return(get_next_msg(&mme_nas_msg_queue_mutex, &mme_nas_msg_queue, nas_msg));
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::delete_next_mme_nas_msg(void)
{
    return(delete_next_msg(&mme_nas_msg_queue_mutex, &mme_nas_msg_queue));
}
void LTE_fdd_enb_rb::set_mme_procedure(LTE_FDD_ENB_MME_PROC_ENUM procedure)
{
    LTE_fdd_enb_interface *interface = LTE_fdd_enb_interface::get_instance();

    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                              LTE_FDD_ENB_DEBUG_LEVEL_RB,
                              __FILE__,
                              __LINE__,
                              "%s MME procedure moving from %s to %s for RNTI=%u",
                              LTE_fdd_enb_rb_text[rb],
                              LTE_fdd_enb_mme_proc_text[mme_procedure],
                              LTE_fdd_enb_mme_proc_text[procedure],
                              user->get_c_rnti());

    mme_procedure = procedure;
}
LTE_FDD_ENB_MME_PROC_ENUM LTE_fdd_enb_rb::get_mme_procedure(void)
{
    return(mme_procedure);
}
void LTE_fdd_enb_rb::set_mme_state(LTE_FDD_ENB_MME_STATE_ENUM state)
{
    LTE_fdd_enb_interface *interface = LTE_fdd_enb_interface::get_instance();

    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                              LTE_FDD_ENB_DEBUG_LEVEL_RB,
                              __FILE__,
                              __LINE__,
                              "%s MME state moving from %s to %s for RNTI=%u",
                              LTE_fdd_enb_rb_text[rb],
                              LTE_fdd_enb_mme_state_text[mme_state],
                              LTE_fdd_enb_mme_state_text[state],
                              user->get_c_rnti());

    mme_state = state;
}
LTE_FDD_ENB_MME_STATE_ENUM LTE_fdd_enb_rb::get_mme_state(void)
{
    return(mme_state);
}

/*************/
/*    RRC    */
/*************/
void LTE_fdd_enb_rb::queue_rrc_pdu(LIBLTE_BIT_MSG_STRUCT *pdu)
{
    queue_msg(pdu, &rrc_pdu_queue_mutex, &rrc_pdu_queue);
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::get_next_rrc_pdu(LIBLTE_BIT_MSG_STRUCT **pdu)
{
    return(get_next_msg(&rrc_pdu_queue_mutex, &rrc_pdu_queue, pdu));
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::delete_next_rrc_pdu(void)
{
    return(delete_next_msg(&rrc_pdu_queue_mutex, &rrc_pdu_queue));
}
void LTE_fdd_enb_rb::queue_rrc_nas_msg(LIBLTE_BYTE_MSG_STRUCT *nas_msg)
{
    queue_msg(nas_msg, &rrc_nas_msg_queue_mutex, &rrc_nas_msg_queue);
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::get_next_rrc_nas_msg(LIBLTE_BYTE_MSG_STRUCT **nas_msg)
{
    return(get_next_msg(&rrc_nas_msg_queue_mutex, &rrc_nas_msg_queue, nas_msg));
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::delete_next_rrc_nas_msg(void)
{
    return(delete_next_msg(&rrc_nas_msg_queue_mutex, &rrc_nas_msg_queue));
}
void LTE_fdd_enb_rb::set_rrc_procedure(LTE_FDD_ENB_RRC_PROC_ENUM procedure)
{
    LTE_fdd_enb_interface *interface = LTE_fdd_enb_interface::get_instance();

    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                              LTE_FDD_ENB_DEBUG_LEVEL_RB,
                              __FILE__,
                              __LINE__,
                              "%s RRC procedure moving from %s to %s for RNTI=%u",
                              LTE_fdd_enb_rb_text[rb],
                              LTE_fdd_enb_rrc_proc_text[rrc_procedure],
                              LTE_fdd_enb_rrc_proc_text[procedure],
                              user->get_c_rnti());
    rrc_procedure = procedure;
}
LTE_FDD_ENB_RRC_PROC_ENUM LTE_fdd_enb_rb::get_rrc_procedure(void)
{
    return(rrc_procedure);
}
void LTE_fdd_enb_rb::set_rrc_state(LTE_FDD_ENB_RRC_STATE_ENUM state)
{
    LTE_fdd_enb_interface *interface = LTE_fdd_enb_interface::get_instance();

    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                              LTE_FDD_ENB_DEBUG_LEVEL_RB,
                              __FILE__,
                              __LINE__,
                              "%s RRC state moving from %s to %s for RNTI=%u",
                              LTE_fdd_enb_rb_text[rb],
                              LTE_fdd_enb_rrc_state_text[rrc_state],
                              LTE_fdd_enb_rrc_state_text[state],
                              user->get_c_rnti());
    rrc_state = state;
}
LTE_FDD_ENB_RRC_STATE_ENUM LTE_fdd_enb_rb::get_rrc_state(void)
{
    return(rrc_state);
}
uint8 LTE_fdd_enb_rb::get_rrc_transaction_id(void)
{
    return(rrc_transaction_id);
}
void LTE_fdd_enb_rb::set_rrc_transaction_id(uint8 transaction_id)
{
    rrc_transaction_id = transaction_id;
}

/**************/
/*    PDCP    */
/**************/
void LTE_fdd_enb_rb::queue_pdcp_pdu(LIBLTE_BIT_MSG_STRUCT *pdu)
{
    queue_msg(pdu, &pdcp_pdu_queue_mutex, &pdcp_pdu_queue);
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::get_next_pdcp_pdu(LIBLTE_BIT_MSG_STRUCT **pdu)
{
    return(get_next_msg(&pdcp_pdu_queue_mutex, &pdcp_pdu_queue, pdu));
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::delete_next_pdcp_pdu(void)
{
    return(delete_next_msg(&pdcp_pdu_queue_mutex, &pdcp_pdu_queue));
}
void LTE_fdd_enb_rb::queue_pdcp_sdu(LIBLTE_BIT_MSG_STRUCT *sdu)
{
    queue_msg(sdu, &pdcp_sdu_queue_mutex, &pdcp_sdu_queue);
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::get_next_pdcp_sdu(LIBLTE_BIT_MSG_STRUCT **sdu)
{
    return(get_next_msg(&pdcp_sdu_queue_mutex, &pdcp_sdu_queue, sdu));
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::delete_next_pdcp_sdu(void)
{
    return(delete_next_msg(&pdcp_sdu_queue_mutex, &pdcp_sdu_queue));
}
LTE_FDD_ENB_PDCP_CONFIG_ENUM LTE_fdd_enb_rb::get_pdcp_config(void)
{
    return(pdcp_config);
}
uint16 LTE_fdd_enb_rb::get_pdcp_rx_sn(void)
{
    return(pdcp_rx_sn);
}
void LTE_fdd_enb_rb::set_pdcp_rx_sn(uint16 rx_sn)
{
    pdcp_rx_sn = rx_sn;
}
uint16 LTE_fdd_enb_rb::get_pdcp_tx_sn(void)
{
    return(pdcp_tx_sn);
}
void LTE_fdd_enb_rb::set_pdcp_tx_sn(uint16 tx_sn)
{
    pdcp_tx_sn = tx_sn;
}

/*************/
/*    RLC    */
/*************/
void LTE_fdd_enb_rb::queue_rlc_pdu(LIBLTE_BIT_MSG_STRUCT *pdu)
{
    queue_msg(pdu, &rlc_pdu_queue_mutex, &rlc_pdu_queue);
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::get_next_rlc_pdu(LIBLTE_BIT_MSG_STRUCT **pdu)
{
    return(get_next_msg(&rlc_pdu_queue_mutex, &rlc_pdu_queue, pdu));
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::delete_next_rlc_pdu(void)
{
    return(delete_next_msg(&rlc_pdu_queue_mutex, &rlc_pdu_queue));
}
void LTE_fdd_enb_rb::queue_rlc_sdu(LIBLTE_BIT_MSG_STRUCT *sdu)
{
    queue_msg(sdu, &rlc_sdu_queue_mutex, &rlc_sdu_queue);
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::get_next_rlc_sdu(LIBLTE_BIT_MSG_STRUCT **sdu)
{
    return(get_next_msg(&rlc_sdu_queue_mutex, &rlc_sdu_queue, sdu));
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::delete_next_rlc_sdu(void)
{
    return(delete_next_msg(&rlc_sdu_queue_mutex, &rlc_sdu_queue));
}
LTE_FDD_ENB_RLC_CONFIG_ENUM LTE_fdd_enb_rb::get_rlc_config(void)
{
    return(rlc_config);
}
uint16 LTE_fdd_enb_rb::get_rlc_vrr(void)
{
    return(rlc_vrr);
}
void LTE_fdd_enb_rb::set_rlc_vrr(uint16 vrr)
{
    rlc_vrr  = vrr;
    rlc_vrmr = rlc_vrr + LIBLTE_RLC_AM_WINDOW_SIZE;
}
uint16 LTE_fdd_enb_rb::get_rlc_vrmr(void)
{
    return(rlc_vrmr);
}
uint16 LTE_fdd_enb_rb::get_rlc_vrh(void)
{
    return(rlc_vrh);
}
void LTE_fdd_enb_rb::set_rlc_vrh(uint16 vrh)
{
    rlc_vrh = vrh;
}
void LTE_fdd_enb_rb::rlc_add_to_reception_buffer(LIBLTE_RLC_AMD_PDU_STRUCT *amd_pdu)
{
    std::map<uint16, LIBLTE_BIT_MSG_STRUCT *>::iterator  iter;
    LIBLTE_BIT_MSG_STRUCT                               *new_pdu = NULL;

    new_pdu = new LIBLTE_BIT_MSG_STRUCT;

    if(NULL != new_pdu)
    {
        iter = rlc_reception_buffer.find(amd_pdu->hdr.sn);
        if(rlc_reception_buffer.end() == iter)
        {
            memcpy(new_pdu, &amd_pdu->data, sizeof(LIBLTE_BIT_MSG_STRUCT));
            rlc_reception_buffer[amd_pdu->hdr.sn] = new_pdu;

            if(LIBLTE_RLC_FI_FIELD_FULL_SDU == amd_pdu->hdr.fi)
            {
                rlc_first_segment_sn = amd_pdu->hdr.sn;
                rlc_last_segment_sn  = amd_pdu->hdr.sn;
            }else if(LIBLTE_RLC_FI_FIELD_FIRST_SDU_SEGMENT == amd_pdu->hdr.fi){
                rlc_first_segment_sn = amd_pdu->hdr.sn;
            }else if(LIBLTE_RLC_FI_FIELD_LAST_SDU_SEGMENT == amd_pdu->hdr.fi){
                rlc_last_segment_sn = amd_pdu->hdr.sn;
            }
        }
    }
}
void LTE_fdd_enb_rb::rlc_get_reception_buffer_status(LIBLTE_RLC_STATUS_PDU_STRUCT *status)
{
    std::map<uint16, LIBLTE_BIT_MSG_STRUCT *>::iterator iter;
    uint32                                              i;

    // Fill in the ACK_SN
    status->ack_sn = rlc_vrh;

    // Determine if any NACK_SNs are needed
    status->N_nack = 0;
    if(rlc_vrh != rlc_vrr)
    {
        for(i=rlc_vrr; i<rlc_vrh; i++)
        {
            if(i > 0x3FFFF)
            {
                i -= 0x3FFFF;
            }

            iter = rlc_reception_buffer.find(i);
            if(rlc_reception_buffer.end() == iter)
            {
                status->nack_sn[status->N_nack++] = i;
            }
        }

        // Update VR(R) if there are no missing frames
        if(0 == status->N_nack)
        {
            set_rlc_vrr(rlc_vrh);
        }
    }
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::rlc_reassemble(LIBLTE_BIT_MSG_STRUCT *sdu)
{
    std::map<uint16, LIBLTE_BIT_MSG_STRUCT *>::iterator iter;
    LTE_FDD_ENB_ERROR_ENUM                              err = LTE_FDD_ENB_ERROR_CANT_REASSEMBLE_SDU;
    uint32                                              i;
    bool                                                reassemble = true;

    if(0xFFFF != rlc_first_segment_sn &&
       0xFFFF != rlc_last_segment_sn)
    {
        // Make sure all segments are available
        for(i=rlc_first_segment_sn; i<=rlc_last_segment_sn; i++)
        {
            iter = rlc_reception_buffer.find(i);
            if(rlc_reception_buffer.end() == iter)
            {
                reassemble = false;
            }
        }

        if(reassemble)
        {
            // Reorder and reassemble the SDU
            sdu->N_bits = 0;
            for(i=rlc_first_segment_sn; i<=rlc_last_segment_sn; i++)
            {
                iter = rlc_reception_buffer.find(i);
                memcpy(&sdu->msg[sdu->N_bits], (*iter).second->msg, (*iter).second->N_bits);
                sdu->N_bits += (*iter).second->N_bits;
                delete (*iter).second;
                rlc_reception_buffer.erase(iter);
            }

            // Clear the first/last segment SNs
            rlc_first_segment_sn = 0xFFFF;
            rlc_last_segment_sn  = 0xFFFF;

            err = LTE_FDD_ENB_ERROR_NONE;
        }
    }

    return(err);
}
uint16 LTE_fdd_enb_rb::get_rlc_vta(void)
{
    return(rlc_vta);
}
void LTE_fdd_enb_rb::set_rlc_vta(uint16 vta)
{
    rlc_vta  = vta;
    rlc_vtms = rlc_vta + LIBLTE_RLC_AM_WINDOW_SIZE;
}
uint16 LTE_fdd_enb_rb::get_rlc_vtms(void)
{
    return(rlc_vtms);
}
uint16 LTE_fdd_enb_rb::get_rlc_vts(void)
{
    return(rlc_vts);
}
void LTE_fdd_enb_rb::set_rlc_vts(uint16 vts)
{
    rlc_vts = vts;
}
void LTE_fdd_enb_rb::rlc_add_to_transmission_buffer(LIBLTE_RLC_AMD_PDU_STRUCT *amd_pdu)
{
    LIBLTE_RLC_AMD_PDU_STRUCT *new_pdu = NULL;

    new_pdu = new LIBLTE_RLC_AMD_PDU_STRUCT;

    if(NULL != new_pdu)
    {
        memcpy(new_pdu, amd_pdu, sizeof(LIBLTE_RLC_AMD_PDU_STRUCT));
        rlc_transmission_buffer[amd_pdu->hdr.sn] = new_pdu;
    }
}
void LTE_fdd_enb_rb::rlc_update_transmission_buffer(uint32 ack_sn)
{
    std::map<uint16, LIBLTE_RLC_AMD_PDU_STRUCT *>::iterator iter;
    uint32                                                  i          = rlc_vta;
    bool                                                    update_vta = true;

    while(i != ack_sn)
    {
        iter = rlc_transmission_buffer.find(i);
        if(rlc_transmission_buffer.end() != iter)
        {
            delete (*iter).second;
            rlc_transmission_buffer.erase(iter);
            if(update_vta)
            {
                set_rlc_vta(i+1);
            }
        }else{
            update_vta = false;
        }
        i++;
        if(i >= 1024)
        {
            i = 0;
        }
    }

    if(rlc_transmission_buffer.size() == 0)
    {
        rlc_stop_t_poll_retransmit();
    }
}
void LTE_fdd_enb_rb::rlc_start_t_poll_retransmit(void)
{
    LTE_fdd_enb_timer_mgr *timer_mgr = LTE_fdd_enb_timer_mgr::get_instance();
    LTE_fdd_enb_timer_cb   timer_expiry_cb(&LTE_fdd_enb_timer_cb_wrapper<LTE_fdd_enb_rb, &LTE_fdd_enb_rb::handle_t_poll_retransmit_timer_expiry>, this);

    if(LTE_FDD_ENB_INVALID_TIMER_ID == t_poll_retransmit_timer_id)
    {
        timer_mgr->start_timer(45, timer_expiry_cb, &t_poll_retransmit_timer_id);
    }
}
void LTE_fdd_enb_rb::rlc_stop_t_poll_retransmit(void)
{
    LTE_fdd_enb_timer_mgr *timer_mgr = LTE_fdd_enb_timer_mgr::get_instance();

    timer_mgr->stop_timer(t_poll_retransmit_timer_id);
    t_poll_retransmit_timer_id = LTE_FDD_ENB_INVALID_TIMER_ID;
}
void LTE_fdd_enb_rb::handle_t_poll_retransmit_timer_expiry(uint32 timer_id)
{
    LTE_fdd_enb_rlc                                         *rlc  = LTE_fdd_enb_rlc::get_instance();
    std::map<uint16, LIBLTE_RLC_AMD_PDU_STRUCT *>::iterator  iter = rlc_transmission_buffer.find(rlc_vta);

    t_poll_retransmit_timer_id = LTE_FDD_ENB_INVALID_TIMER_ID;

    if(rlc_transmission_buffer.end() != iter)
    {
        rlc->handle_retransmit((*iter).second, user, this);
    }
}

/*************/
/*    MAC    */
/*************/
void LTE_fdd_enb_rb::queue_mac_sdu(LIBLTE_BIT_MSG_STRUCT *sdu)
{
    queue_msg(sdu, &mac_sdu_queue_mutex, &mac_sdu_queue);
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::get_next_mac_sdu(LIBLTE_BIT_MSG_STRUCT **sdu)
{
    return(get_next_msg(&mac_sdu_queue_mutex, &mac_sdu_queue, sdu));
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::delete_next_mac_sdu(void)
{
    return(delete_next_msg(&mac_sdu_queue_mutex, &mac_sdu_queue));
}
LTE_FDD_ENB_MAC_CONFIG_ENUM LTE_fdd_enb_rb::get_mac_config(void)
{
    return(mac_config);
}
void LTE_fdd_enb_rb::start_ul_sched_timer(uint32 m_seconds)
{
    LTE_fdd_enb_timer_mgr *timer_mgr = LTE_fdd_enb_timer_mgr::get_instance();
    LTE_fdd_enb_timer_cb   timer_expiry_cb(&LTE_fdd_enb_timer_cb_wrapper<LTE_fdd_enb_rb, &LTE_fdd_enb_rb::handle_ul_sched_timer_expiry>, this);

    ul_sched_timer_m_seconds = m_seconds;
    timer_mgr->start_timer(ul_sched_timer_m_seconds, timer_expiry_cb, &ul_sched_timer_id);
}
void LTE_fdd_enb_rb::handle_ul_sched_timer_expiry(uint32 timer_id)
{
    LTE_fdd_enb_mac *mac = LTE_fdd_enb_mac::get_instance();

    mac->sched_ul(user, avail_qos[qos].bits_per_subfn);
    if(LTE_FDD_ENB_RRC_PROC_IDLE  != rrc_procedure &&
       LTE_FDD_ENB_RRC_STATE_IDLE != rrc_state)
    {
        start_ul_sched_timer(ul_sched_timer_m_seconds);
    }
}
void LTE_fdd_enb_rb::set_con_res_id(uint64 con_res_id)
{
    mac_con_res_id = con_res_id;
}
uint64 LTE_fdd_enb_rb::get_con_res_id(void)
{
    return(mac_con_res_id);
}
void LTE_fdd_enb_rb::set_send_con_res_id(bool send_con_res_id)
{
    mac_send_con_res_id = send_con_res_id;
}
bool LTE_fdd_enb_rb::get_send_con_res_id(void)
{
    return(mac_send_con_res_id);
}

/*****************/
/*    Generic    */
/*****************/
void LTE_fdd_enb_rb::queue_msg(LIBLTE_BIT_MSG_STRUCT              *msg,
                               boost::mutex                       *mutex,
                               std::list<LIBLTE_BIT_MSG_STRUCT *> *queue)
{
    boost::mutex::scoped_lock  lock(*mutex);
    LIBLTE_BIT_MSG_STRUCT     *loc_msg;

    loc_msg = new LIBLTE_BIT_MSG_STRUCT;
    memcpy(loc_msg, msg, sizeof(LIBLTE_BIT_MSG_STRUCT));

    queue->push_back(loc_msg);
}
void LTE_fdd_enb_rb::queue_msg(LIBLTE_BYTE_MSG_STRUCT              *msg,
                               boost::mutex                        *mutex,
                               std::list<LIBLTE_BYTE_MSG_STRUCT *> *queue)
{
    boost::mutex::scoped_lock  lock(*mutex);
    LIBLTE_BYTE_MSG_STRUCT    *loc_msg;

    loc_msg = new LIBLTE_BYTE_MSG_STRUCT;
    memcpy(loc_msg, msg, sizeof(LIBLTE_BYTE_MSG_STRUCT));

    queue->push_back(loc_msg);
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::get_next_msg(boost::mutex                        *mutex,
                                                    std::list<LIBLTE_BIT_MSG_STRUCT *>  *queue,
                                                    LIBLTE_BIT_MSG_STRUCT              **msg)
{
    boost::mutex::scoped_lock lock(*mutex);
    LTE_FDD_ENB_ERROR_ENUM    err = LTE_FDD_ENB_ERROR_NO_MSG_IN_QUEUE;

    if(0 != queue->size())
    {
        *msg = queue->front();
        err  = LTE_FDD_ENB_ERROR_NONE;
    }

    return(err);
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::get_next_msg(boost::mutex                         *mutex,
                                                    std::list<LIBLTE_BYTE_MSG_STRUCT *>  *queue,
                                                    LIBLTE_BYTE_MSG_STRUCT              **msg)
{
    boost::mutex::scoped_lock lock(*mutex);
    LTE_FDD_ENB_ERROR_ENUM    err = LTE_FDD_ENB_ERROR_NO_MSG_IN_QUEUE;

    if(0 != queue->size())
    {
        *msg = queue->front();
        err  = LTE_FDD_ENB_ERROR_NONE;
    }

    return(err);
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::delete_next_msg(boost::mutex                       *mutex,
                                                       std::list<LIBLTE_BIT_MSG_STRUCT *> *queue)
{
    boost::mutex::scoped_lock  lock(*mutex);
    LTE_FDD_ENB_ERROR_ENUM     err = LTE_FDD_ENB_ERROR_NO_MSG_IN_QUEUE;
    LIBLTE_BIT_MSG_STRUCT     *msg;

    if(0 != queue->size())
    {
        msg = queue->front();
        queue->pop_front();
        delete msg;
        err = LTE_FDD_ENB_ERROR_NONE;
    }

    return(err);
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_rb::delete_next_msg(boost::mutex                        *mutex,
                                                       std::list<LIBLTE_BYTE_MSG_STRUCT *> *queue)
{
    boost::mutex::scoped_lock  lock(*mutex);
    LTE_FDD_ENB_ERROR_ENUM     err = LTE_FDD_ENB_ERROR_NO_MSG_IN_QUEUE;
    LIBLTE_BYTE_MSG_STRUCT    *msg;

    if(0 != queue->size())
    {
        msg = queue->front();
        queue->pop_front();
        delete msg;
        err = LTE_FDD_ENB_ERROR_NONE;
    }

    return(err);
}
void LTE_fdd_enb_rb::set_qos(LTE_FDD_ENB_QOS_ENUM _qos)
{
    qos = _qos;
    if(qos != LTE_FDD_ENB_QOS_NONE)
    {
        start_ul_sched_timer(avail_qos[qos].tti_frequency-1);
    }
}
LTE_FDD_ENB_QOS_ENUM LTE_fdd_enb_rb::get_qos(void)
{
    return(qos);
}
uint32 LTE_fdd_enb_rb::get_qos_tti_freq(void)
{
    return(avail_qos[qos].tti_frequency);
}
uint32 LTE_fdd_enb_rb::get_qos_bits_per_subfn(void)
{
    return(avail_qos[qos].bits_per_subfn);
}
