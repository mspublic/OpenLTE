#line 2 "LTE_fdd_enb_mme.cc" // Make __FILE__ omit the path
/*******************************************************************************

    Copyright 2013-2014 Ben Wojtowicz

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

    File: LTE_fdd_enb_mme.cc

    Description: Contains all the implementations for the LTE FDD eNodeB
                 mobility management entity layer.

    Revision History
    ----------    -------------    --------------------------------------------
    11/10/2013    Ben Wojtowicz    Created file
    01/18/2014    Ben Wojtowicz    Added level to debug prints.
    06/15/2014    Ben Wojtowicz    Added RRC NAS message handler.
    08/03/2014    Ben Wojtowicz    Added message parsers, state machines, and
                                   message senders.

*******************************************************************************/

/*******************************************************************************
                              INCLUDES
*******************************************************************************/

#include "LTE_fdd_enb_mme.h"
#include "LTE_fdd_enb_interface.h"
#include "LTE_fdd_enb_hss.h"
#include "liblte_mme.h"

/*******************************************************************************
                              DEFINES
*******************************************************************************/


/*******************************************************************************
                              TYPEDEFS
*******************************************************************************/


/*******************************************************************************
                              GLOBAL VARIABLES
*******************************************************************************/

LTE_fdd_enb_mme* LTE_fdd_enb_mme::instance = NULL;
boost::mutex     mme_instance_mutex;

/*******************************************************************************
                              CLASS IMPLEMENTATIONS
*******************************************************************************/

/*******************/
/*    Singleton    */
/*******************/
LTE_fdd_enb_mme* LTE_fdd_enb_mme::get_instance(void)
{
    boost::mutex::scoped_lock lock(mme_instance_mutex);

    if(NULL == instance)
    {
        instance = new LTE_fdd_enb_mme();
    }

    return(instance);
}
void LTE_fdd_enb_mme::cleanup(void)
{
    boost::mutex::scoped_lock lock(mme_instance_mutex);

    if(NULL != instance)
    {
        delete instance;
        instance = NULL;
    }
}

/********************************/
/*    Constructor/Destructor    */
/********************************/
LTE_fdd_enb_mme::LTE_fdd_enb_mme()
{
    started = false;
}
LTE_fdd_enb_mme::~LTE_fdd_enb_mme()
{
    stop();
}

/********************/
/*    Start/Stop    */
/********************/
void LTE_fdd_enb_mme::start(void)
{
    boost::mutex::scoped_lock lock(start_mutex);
    LTE_fdd_enb_msgq_cb       rrc_cb(&LTE_fdd_enb_msgq_cb_wrapper<LTE_fdd_enb_mme, &LTE_fdd_enb_mme::handle_rrc_msg>, this);

    if(!started)
    {
        started       = true;
        rrc_comm_msgq = new LTE_fdd_enb_msgq("rrc_mme_mq",
                                             rrc_cb);
        mme_rrc_mq    = new boost::interprocess::message_queue(boost::interprocess::open_only,
                                                               "mme_rrc_mq");
    }
}
void LTE_fdd_enb_mme::stop(void)
{
    boost::mutex::scoped_lock lock(start_mutex);

    if(started)
    {
        started = false;
        delete rrc_comm_msgq;
    }
}

/***********************/
/*    Communication    */
/***********************/
void LTE_fdd_enb_mme::handle_rrc_msg(LTE_FDD_ENB_MESSAGE_STRUCT *msg)
{
    LTE_fdd_enb_interface *interface = LTE_fdd_enb_interface::get_instance();

    switch(msg->type)
    {
    case LTE_FDD_ENB_MESSAGE_TYPE_MME_NAS_MSG_READY:
        handle_nas_msg(&msg->msg.mme_nas_msg_ready);
        delete msg;
        break;
    default:
        interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_ERROR,
                                  LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                  __FILE__,
                                  __LINE__,
                                  "Received invalid RRC message %s",
                                  LTE_fdd_enb_message_type_text[msg->type]);
        delete msg;
        break;
    }
}

/****************************/
/*    External Interface    */
/****************************/
void LTE_fdd_enb_mme::update_sys_info(void)
{
    LTE_fdd_enb_cnfg_db *cnfg_db = LTE_fdd_enb_cnfg_db::get_instance();

    sys_info_mutex.lock();
    cnfg_db->get_sys_info(sys_info);
    sys_info_mutex.unlock();
}

/******************************/
/*    RRC Message Handlers    */
/******************************/
void LTE_fdd_enb_mme::handle_nas_msg(LTE_FDD_ENB_MME_NAS_MSG_READY_MSG_STRUCT *nas_msg)
{
    LTE_fdd_enb_interface  *interface = LTE_fdd_enb_interface::get_instance();
    LIBLTE_BYTE_MSG_STRUCT *msg;
    uint8                   pd;
    uint8                   msg_type;

    if(LTE_FDD_ENB_ERROR_NONE == nas_msg->rb->get_next_mme_nas_msg(&msg))
    {
        interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                                  LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                  __FILE__,
                                  __LINE__,
                                  msg,
                                  "Received NAS message for RNTI=%u and RB=%s",
                                  nas_msg->user->get_c_rnti(),
                                  LTE_fdd_enb_rb_text[nas_msg->rb->get_rb_id()]);

        // Parse the message
        liblte_mme_parse_msg_header(msg, &pd, &msg_type);
        if(LIBLTE_MME_MSG_TYPE_ATTACH_REQUEST == msg_type)
        {
            interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                                      LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                      __FILE__,
                                      __LINE__,
                                      "Received Attach Request for RNTI=%u and RB=%s",
                                      nas_msg->user->get_c_rnti(),
                                      LTE_fdd_enb_rb_text[nas_msg->rb->get_rb_id()]);
            parse_attach_request(msg, nas_msg->user, nas_msg->rb);
        }else if(LIBLTE_MME_MSG_TYPE_IDENTITY_RESPONSE == msg_type){
            interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                                      LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                      __FILE__,
                                      __LINE__,
                                      "Received Identity Response for RNTI=%u and RB=%s",
                                      nas_msg->user->get_c_rnti(),
                                      LTE_fdd_enb_rb_text[nas_msg->rb->get_rb_id()]);
            parse_identity_response(msg, nas_msg->user, nas_msg->rb);
        }else if(LIBLTE_MME_MSG_TYPE_AUTHENTICATION_FAILURE){
            interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                                      LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                      __FILE__,
                                      __LINE__,
                                      "Received Authentication Failure for RNTI=%u and RB=%s",
                                      nas_msg->user->get_c_rnti(),
                                      LTE_fdd_enb_rb_text[nas_msg->rb->get_rb_id()]);
            parse_authentication_failure(msg, nas_msg->user, nas_msg->rb);
        }else{
            interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_ERROR,
                                      LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                      __FILE__,
                                      __LINE__,
                                      "Not handling NAS message with MSG_TYPE=%02X",
                                      msg_type);
        }

        // Delete the NAS message
        nas_msg->rb->delete_next_mme_nas_msg();

        // Call the appropriate state machine
        switch(nas_msg->rb->get_mme_procedure())
        {
        case LTE_FDD_ENB_MME_PROC_ATTACH:
            attach_sm(nas_msg->user, nas_msg->rb);
            break;
        default:
            interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_ERROR,
                                      LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                      __FILE__,
                                      __LINE__,
                                      "MME in invalid procedure %s",
                                      LTE_fdd_enb_mme_proc_text[nas_msg->rb->get_mme_procedure()]);
            break;
        }
    }else{
        interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_ERROR,
                                  LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                  __FILE__,
                                  __LINE__,
                                  "Received NAS message with no message queued");
    }
}

/*************************/
/*    Message Parsers    */
/*************************/
void LTE_fdd_enb_mme::parse_attach_request(LIBLTE_BYTE_MSG_STRUCT *msg,
                                           LTE_fdd_enb_user       *user,
                                           LTE_fdd_enb_rb         *rb)
{
    LTE_fdd_enb_interface                *interface = LTE_fdd_enb_interface::get_instance();
    LTE_fdd_enb_hss                      *hss       = LTE_fdd_enb_hss::get_instance();
    LIBLTE_MME_ATTACH_REQUEST_MSG_STRUCT  attach_req;
    uint64                                imsi_num = 0;
    uint64                                imei_num = 0;
    uint32                                i;

    // Unpack the message
    liblte_mme_unpack_attach_request_msg(msg, &attach_req);

    // FIXME: ESM Message

    // Set the procedure
    rb->set_mme_procedure(LTE_FDD_ENB_MME_PROC_ATTACH);

    // Send an info message
    if(LIBLTE_MME_EPS_MOBILE_ID_TYPE_GUTI == attach_req.eps_mobile_id.type_of_id)
    {
        if(user->is_id_set())
        {
            rb->set_mme_state(LTE_FDD_ENB_MME_STATE_AUTHENTICATE);
        }else{
            rb->set_mme_state(LTE_FDD_ENB_MME_STATE_ID_REQUEST_IMSI);
        }
    }else if(LIBLTE_MME_EPS_MOBILE_ID_TYPE_IMSI == attach_req.eps_mobile_id.type_of_id){
        for(i=0; i<15; i++)
        {
            imsi_num *= 10;
            imsi_num += attach_req.eps_mobile_id.imsi[i];
        }
        if(hss->is_imsi_allowed(imsi_num))
        {
            rb->set_mme_state(LTE_FDD_ENB_MME_STATE_AUTHENTICATE);
            user->set_id(hss->get_user_id_from_imsi(imsi_num));
        }else{
            user->set_temp_id(imsi_num);
            rb->set_mme_state(LTE_FDD_ENB_MME_STATE_REJECT);
        }
    }else{
        for(i=0; i<15; i++)
        {
            imei_num *= 10;
            imei_num += attach_req.eps_mobile_id.imei[i];
        }
        if(hss->is_imei_allowed(imei_num))
        {
            rb->set_mme_state(LTE_FDD_ENB_MME_STATE_AUTHENTICATE);
            user->set_id(hss->get_user_id_from_imei(imei_num));
        }else{
            user->set_temp_id(imei_num);
            rb->set_mme_state(LTE_FDD_ENB_MME_STATE_REJECT);
        }
    }
}
void LTE_fdd_enb_mme::parse_authentication_failure(LIBLTE_BYTE_MSG_STRUCT *msg,
                                                   LTE_fdd_enb_user       *user,
                                                   LTE_fdd_enb_rb         *rb)
{
    // FIXME
}
void LTE_fdd_enb_mme::parse_identity_response(LIBLTE_BYTE_MSG_STRUCT *msg,
                                              LTE_fdd_enb_user       *user,
                                              LTE_fdd_enb_rb         *rb)
{
    LTE_fdd_enb_interface             *interface = LTE_fdd_enb_interface::get_instance();
    LTE_fdd_enb_hss                   *hss       = LTE_fdd_enb_hss::get_instance();
    LIBLTE_MME_ID_RESPONSE_MSG_STRUCT  id_resp;
    uint64                             imsi_num = 0;
    uint64                             imei_num = 0;
    uint32                             i;

    // Unpack the message
    liblte_mme_unpack_identity_response_msg(msg, &id_resp);

    // Store the ID
    if(LIBLTE_MME_MOBILE_ID_TYPE_IMSI == id_resp.mobile_id.type_of_id)
    {
        for(i=0; i<15; i++)
        {
            imsi_num *= 10;
            imsi_num += id_resp.mobile_id.imsi[i];
        }
        if(hss->is_imsi_allowed(imsi_num))
        {
            rb->set_mme_state(LTE_FDD_ENB_MME_STATE_AUTHENTICATE);
            user->set_id(hss->get_user_id_from_imsi(imsi_num));
        }else{
            user->set_temp_id(imsi_num);
            rb->set_mme_state(LTE_FDD_ENB_MME_STATE_REJECT);
        }
    }else if(LIBLTE_MME_MOBILE_ID_TYPE_IMEI == id_resp.mobile_id.type_of_id){
        for(i=0; i<15; i++)
        {
            imei_num *= 10;
            imei_num += id_resp.mobile_id.imei[i];
        }
        if(hss->is_imei_allowed(imei_num))
        {
            rb->set_mme_state(LTE_FDD_ENB_MME_STATE_AUTHENTICATE);
            user->set_id(hss->get_user_id_from_imei(imei_num));
        }else{
            user->set_temp_id(imei_num);
            rb->set_mme_state(LTE_FDD_ENB_MME_STATE_REJECT);
        }
    }else{
        interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_ERROR,
                                  LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                  __FILE__,
                                  __LINE__,
                                  "Invalid ID_TYPE=%u",
                                  id_resp.mobile_id.type_of_id);
    }
}

/************************/
/*    State Machines    */
/************************/
void LTE_fdd_enb_mme::attach_sm(LTE_fdd_enb_user *user,
                                LTE_fdd_enb_rb   *rb)
{
    LTE_fdd_enb_interface *interface = LTE_fdd_enb_interface::get_instance();
    LTE_fdd_enb_hss       *hss       = LTE_fdd_enb_hss::get_instance();

    switch(rb->get_mme_state())
    {
    case LTE_FDD_ENB_MME_STATE_ID_REQUEST_IMSI:
        send_identity_request(user, rb, LIBLTE_MME_ID_TYPE_2_IMSI);
        break;
    case LTE_FDD_ENB_MME_STATE_REJECT:
        interface->send_ctrl_info_msg("rejecting user id=%llu",
                                      user->get_temp_id());
        user->set_delete_at_idle(true);
        send_attach_reject(user, rb, LIBLTE_MME_EMM_CAUSE_IMSI_UNKNOWN_IN_HSS);
        break;
    case LTE_FDD_ENB_MME_STATE_AUTHENTICATE:
        interface->send_ctrl_info_msg("authenticating user imsi=%s imei=%s",
                                      user->get_imsi_str().c_str(),
                                      user->get_imei_str().c_str());
        send_authentication_request(user, rb);
        break;
    default:
        interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_ERROR,
                                  LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                  __FILE__,
                                  __LINE__,
                                  "ATTACH state machine invalid state %s",
                                  LTE_fdd_enb_mme_state_text[rb->get_mme_state()]);
        break;
    }
}

/*************************/
/*    Message Senders    */
/*************************/
void LTE_fdd_enb_mme::send_attach_reject(LTE_fdd_enb_user *user,
                                         LTE_fdd_enb_rb   *rb,
                                         uint8             rej_cause)
{
    LTE_fdd_enb_interface                    *interface = LTE_fdd_enb_interface::get_instance();
    LTE_FDD_ENB_RRC_NAS_MSG_READY_MSG_STRUCT  nas_msg_ready;
    LTE_FDD_ENB_RRC_CMD_READY_MSG_STRUCT      cmd_ready;
    LIBLTE_MME_ATTACH_REJECT_MSG_STRUCT       attach_rej;
    LIBLTE_BYTE_MSG_STRUCT                    msg;

    attach_rej.emm_cause           = rej_cause;
    attach_rej.esm_msg_present     = false;
    attach_rej.t3446_value_present = false;
    liblte_mme_pack_attach_reject_msg(&attach_rej, &msg);
    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                              LTE_FDD_ENB_DEBUG_LEVEL_MME,
                              __FILE__,
                              __LINE__,
                              &msg,
                              "Sending Attach Reject for RNTI=%u, RB=%s",
                              user->get_c_rnti(),
                              LTE_fdd_enb_rb_text[rb->get_rb_id()]);

    // Queue the NAS message for RRC
    rb->queue_rrc_nas_msg(&msg);

    // Signal RRC for NAS message
    nas_msg_ready.user = user;
    nas_msg_ready.rb   = rb;
    LTE_fdd_enb_msgq::send(mme_rrc_mq,
                           LTE_FDD_ENB_MESSAGE_TYPE_RRC_NAS_MSG_READY,
                           LTE_FDD_ENB_DEST_LAYER_RRC,
                           (LTE_FDD_ENB_MESSAGE_UNION *)&nas_msg_ready,
                           sizeof(LTE_FDD_ENB_RRC_NAS_MSG_READY_MSG_STRUCT));

    // Signal RRC for command
    cmd_ready.user = user;
    cmd_ready.rb   = rb;
    cmd_ready.cmd  = LTE_FDD_ENB_RRC_CMD_RELEASE;
    LTE_fdd_enb_msgq::send(mme_rrc_mq,
                           LTE_FDD_ENB_MESSAGE_TYPE_RRC_CMD_READY,
                           LTE_FDD_ENB_DEST_LAYER_RRC,
                           (LTE_FDD_ENB_MESSAGE_UNION *)&cmd_ready,
                           sizeof(LTE_FDD_ENB_RRC_CMD_READY_MSG_STRUCT));
}
void LTE_fdd_enb_mme::send_authentication_request(LTE_fdd_enb_user *user,
                                                  LTE_fdd_enb_rb   *rb)
{
    LTE_fdd_enb_interface                        *interface = LTE_fdd_enb_interface::get_instance();
    LTE_fdd_enb_hss                              *hss       = LTE_fdd_enb_hss::get_instance();
    LTE_FDD_ENB_AUTHENTICATION_VECTOR_STRUCT     *auth_vec  = NULL;
    LTE_FDD_ENB_RRC_NAS_MSG_READY_MSG_STRUCT      nas_msg_ready;
    LIBLTE_MME_AUTHENTICATION_REQUEST_MSG_STRUCT  auth_req;
    LIBLTE_BYTE_MSG_STRUCT                        msg;
    uint32                                        i;

    sys_info_mutex.lock();
    hss->generate_security_data(user->get_id(), sys_info.sib1.plmn_id[0].id.mcc, sys_info.sib1.plmn_id[0].id.mnc);
    sys_info_mutex.unlock();
    auth_vec = hss->get_auth_vec(user->get_id());
    if(NULL != auth_vec)
    {
        for(i=0; i<16; i++)
        {
            auth_req.autn[i] = auth_vec->autn[i];
            auth_req.rand[i] = auth_vec->rand[i];
        }
        auth_req.nas_ksi.tsc_flag = LIBLTE_MME_TYPE_OF_SECURITY_CONTEXT_FLAG_NATIVE;
        auth_req.nas_ksi.nas_ksi  = 0;
        liblte_mme_pack_authentication_request_msg(&auth_req, &msg);
        interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                                  LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                  __FILE__,
                                  __LINE__,
                                  &msg,
                                  "Sending Authentication Request for RNTI=%u, RB=%s",
                                  user->get_c_rnti(),
                                  LTE_fdd_enb_rb_text[rb->get_rb_id()]);

        // Queue the NAS message for RRC
        rb->queue_rrc_nas_msg(&msg);

        // Signal RRC
        nas_msg_ready.user = user;
        nas_msg_ready.rb   = rb;
        LTE_fdd_enb_msgq::send(mme_rrc_mq,
                               LTE_FDD_ENB_MESSAGE_TYPE_RRC_NAS_MSG_READY,
                               LTE_FDD_ENB_DEST_LAYER_RRC,
                               (LTE_FDD_ENB_MESSAGE_UNION *)&nas_msg_ready,
                               sizeof(LTE_FDD_ENB_RRC_NAS_MSG_READY_MSG_STRUCT));
    }
}
void LTE_fdd_enb_mme::send_identity_request(LTE_fdd_enb_user *user,
                                            LTE_fdd_enb_rb   *rb,
                                            uint8             id_type)
{
    LTE_fdd_enb_interface                    *interface = LTE_fdd_enb_interface::get_instance();
    LTE_FDD_ENB_RRC_NAS_MSG_READY_MSG_STRUCT  nas_msg_ready;
    LIBLTE_MME_ID_REQUEST_MSG_STRUCT          id_req;
    LIBLTE_BYTE_MSG_STRUCT                    msg;

    id_req.id_type = id_type;
    liblte_mme_pack_identity_request_msg(&id_req, &msg);
    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                              LTE_FDD_ENB_DEBUG_LEVEL_MME,
                              __FILE__,
                              __LINE__,
                              &msg,
                              "Sending ID Request for RNTI=%u, RB=%s",
                              user->get_c_rnti(),
                              LTE_fdd_enb_rb_text[rb->get_rb_id()]);

    // Queue the NAS message for RRC
    rb->queue_rrc_nas_msg(&msg);

    // Signal RRC
    nas_msg_ready.user = user;
    nas_msg_ready.rb   = rb;
    LTE_fdd_enb_msgq::send(mme_rrc_mq,
                           LTE_FDD_ENB_MESSAGE_TYPE_RRC_NAS_MSG_READY,
                           LTE_FDD_ENB_DEST_LAYER_RRC,
                           (LTE_FDD_ENB_MESSAGE_UNION *)&nas_msg_ready,
                           sizeof(LTE_FDD_ENB_RRC_NAS_MSG_READY_MSG_STRUCT));
}
