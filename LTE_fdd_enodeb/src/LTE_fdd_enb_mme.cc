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
    09/03/2014    Ben Wojtowicz    Added authentication and security support.
    11/01/2014    Ben Wojtowicz    Added attach accept/complete, ESM info
                                   transfer, and default bearer setup support.

*******************************************************************************/

/*******************************************************************************
                              INCLUDES
*******************************************************************************/

#include "LTE_fdd_enb_mme.h"
#include "LTE_fdd_enb_interface.h"
#include "LTE_fdd_enb_hss.h"
#include "LTE_fdd_enb_user_mgr.h"
#include "liblte_mme.h"
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
    LTE_fdd_enb_cnfg_db *cnfg_db = LTE_fdd_enb_cnfg_db::get_instance();

    started = false;

    cnfg_db->get_param(LTE_FDD_ENB_PARAM_IP_ADDR_START, next_ip_addr);
    cnfg_db->get_param(LTE_FDD_ENB_PARAM_DNS_ADDR, dns_addr);
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
    case LTE_FDD_ENB_MESSAGE_TYPE_MME_RRC_CMD_RESP:
        handle_rrc_cmd_resp(&msg->msg.mme_rrc_cmd_resp);
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
        switch(msg_type)
        {
        case LIBLTE_MME_MSG_TYPE_ATTACH_COMPLETE:
            parse_attach_complete(msg, nas_msg->user, nas_msg->rb);
            break;
        case LIBLTE_MME_MSG_TYPE_ATTACH_REQUEST:
            parse_attach_request(msg, &nas_msg->user, nas_msg->rb);
            break;
        case LIBLTE_MME_MSG_TYPE_AUTHENTICATION_FAILURE:
            parse_authentication_failure(msg, nas_msg->user, nas_msg->rb);
            break;
        case LIBLTE_MME_MSG_TYPE_AUTHENTICATION_RESPONSE:
            parse_authentication_response(msg, nas_msg->user, nas_msg->rb);
            break;
        case LIBLTE_MME_MSG_TYPE_IDENTITY_RESPONSE:
            parse_identity_response(msg, nas_msg->user, nas_msg->rb);
            break;
        case LIBLTE_MME_MSG_TYPE_SECURITY_MODE_COMPLETE:
            parse_security_mode_complete(msg, nas_msg->user, nas_msg->rb);
            break;
        case LIBLTE_MME_MSG_TYPE_SECURITY_MODE_REJECT:
            parse_security_mode_reject(msg, nas_msg->user, nas_msg->rb);
            break;
        case LIBLTE_MME_MSG_TYPE_ESM_INFORMATION_RESPONSE:
            parse_esm_information_response(msg, nas_msg->user, nas_msg->rb);
            break;
        default:
            interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_ERROR,
                                      LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                      __FILE__,
                                      __LINE__,
                                      "Not handling NAS message with MSG_TYPE=%02X",
                                      msg_type);
            break;
        }

        // Increment the uplink NAS count
        nas_msg->user->increment_nas_count_ul();

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
void LTE_fdd_enb_mme::handle_rrc_cmd_resp(LTE_FDD_ENB_MME_RRC_CMD_RESP_MSG_STRUCT *rrc_cmd_resp)
{
    LTE_fdd_enb_interface *interface = LTE_fdd_enb_interface::get_instance();

    switch(rrc_cmd_resp->cmd_resp)
    {
    case LTE_FDD_ENB_MME_RRC_CMD_RESP_SECURITY:
        switch(rrc_cmd_resp->rb->get_mme_procedure())
        {
        case LTE_FDD_ENB_MME_PROC_ATTACH:
            if(rrc_cmd_resp->user->get_esm_info_transfer())
            {
                rrc_cmd_resp->rb->set_mme_state(LTE_FDD_ENB_MME_STATE_ESM_INFO_TRANSFER);
                attach_sm(rrc_cmd_resp->user, rrc_cmd_resp->rb);
            }else{
                rrc_cmd_resp->rb->set_mme_state(LTE_FDD_ENB_MME_STATE_ATTACH_ACCEPT);
                attach_sm(rrc_cmd_resp->user, rrc_cmd_resp->rb);
            }
            break;
        default:
            interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_ERROR,
                                      LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                      __FILE__,
                                      __LINE__,
                                      "MME in invalid procedure %s",
                                      LTE_fdd_enb_mme_proc_text[rrc_cmd_resp->rb->get_mme_procedure()]);
            break;
        }
        break;
    default:
        interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_ERROR,
                                  LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                  __FILE__,
                                  __LINE__,
                                  "Received invalid RRC command response %s",
                                  LTE_fdd_enb_mme_rrc_cmd_resp_text[rrc_cmd_resp->cmd_resp]);
        break;
    }
}

/*************************/
/*    Message Parsers    */
/*************************/
void LTE_fdd_enb_mme::parse_attach_complete(LIBLTE_BYTE_MSG_STRUCT *msg,
                                            LTE_fdd_enb_user       *user,
                                            LTE_fdd_enb_rb         *rb)
{
    LTE_fdd_enb_interface                 *interface = LTE_fdd_enb_interface::get_instance();
    LIBLTE_MME_ATTACH_COMPLETE_MSG_STRUCT  attach_comp;
    uint8                                  pd;
    uint8                                  msg_type;

    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                              LTE_FDD_ENB_DEBUG_LEVEL_MME,
                              __FILE__,
                              __LINE__,
                              "Received Attach Complete for RNTI=%u and RB=%s",
                              user->get_c_rnti(),
                              LTE_fdd_enb_rb_text[rb->get_rb_id()]);

    // Unpack the message
    liblte_mme_unpack_attach_complete_msg(msg, &attach_comp);

    interface->send_ctrl_info_msg("user fully attached imsi=%s imei=%s",
                                  user->get_imsi_str().c_str(),
                                  user->get_imei_str().c_str());

    rb->set_mme_state(LTE_FDD_ENB_MME_STATE_ATTACHED);

    // Parse the ESM message
    liblte_mme_parse_msg_header(&attach_comp.esm_msg, &pd, &msg_type);
    switch(msg_type)
    {
    case LIBLTE_MME_MSG_TYPE_ACTIVATE_DEFAULT_EPS_BEARER_CONTEXT_ACCEPT:
        parse_activate_default_eps_bearer_context_accept(&attach_comp.esm_msg, user, rb);
        break;
    default:
        interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_ERROR,
                                  LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                  __FILE__,
                                  __LINE__,
                                  "Not handling NAS message with MSG_TYPE=%02X",
                                  msg_type);
        break;
    }
}
void LTE_fdd_enb_mme::parse_attach_request(LIBLTE_BYTE_MSG_STRUCT  *msg,
                                           LTE_fdd_enb_user       **user,
                                           LTE_fdd_enb_rb          *rb)
{
    LTE_fdd_enb_interface                *interface = LTE_fdd_enb_interface::get_instance();
    LTE_fdd_enb_hss                      *hss       = LTE_fdd_enb_hss::get_instance();
    LTE_fdd_enb_user_mgr                 *user_mgr  = LTE_fdd_enb_user_mgr::get_instance();
    LTE_fdd_enb_user                     *act_user;
    LIBLTE_MME_ATTACH_REQUEST_MSG_STRUCT  attach_req;
    uint64                                imsi_num = 0;
    uint64                                imei_num = 0;
    uint32                                i;
    uint8                                 pd;
    uint8                                 msg_type;

    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                              LTE_FDD_ENB_DEBUG_LEVEL_MME,
                              __FILE__,
                              __LINE__,
                              "Received Attach Request for RNTI=%u and RB=%s",
                              (*user)->get_c_rnti(),
                              LTE_fdd_enb_rb_text[rb->get_rb_id()]);

    // Unpack the message
    liblte_mme_unpack_attach_request_msg(msg, &attach_req);

    // Parse the ESM message
    liblte_mme_parse_msg_header(&attach_req.esm_msg, &pd, &msg_type);
    switch(msg_type)
    {
    case LIBLTE_MME_MSG_TYPE_PDN_CONNECTIVITY_REQUEST:
        parse_pdn_connectivity_request(&attach_req.esm_msg, (*user), rb);
        break;
    default:
        interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_ERROR,
                                  LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                  __FILE__,
                                  __LINE__,
                                  "Not handling NAS message with MSG_TYPE=%02X",
                                  msg_type);
        break;
    }

    // Set the procedure
    rb->set_mme_procedure(LTE_FDD_ENB_MME_PROC_ATTACH);

    // Store the attach type
    (*user)->set_attach_type(attach_req.eps_attach_type);

    // Store UE capabilities
    for(i=0; i<8; i++)
    {
        (*user)->set_eea_support(i, attach_req.ue_network_cap.eea[i]);
        (*user)->set_eia_support(i, attach_req.ue_network_cap.eia[i]);
    }
    if(attach_req.ue_network_cap.uea_present)
    {
        for(i=0; i<8; i++)
        {
            (*user)->set_uea_support(i, attach_req.ue_network_cap.uea[i]);
        }
    }
    if(attach_req.ue_network_cap.uia_present)
    {
        for(i=1; i<8; i++)
        {
            (*user)->set_uia_support(i, attach_req.ue_network_cap.uia[i]);
        }
    }
    if(attach_req.ms_network_cap_present)
    {
        for(i=1; i<8; i++)
        {
            (*user)->set_gea_support(i, attach_req.ms_network_cap.gea[i]);
        }
    }

    // Send an info message
    if(LIBLTE_MME_EPS_MOBILE_ID_TYPE_GUTI == attach_req.eps_mobile_id.type_of_id)
    {
        if(LTE_FDD_ENB_ERROR_NONE == user_mgr->find_user(&attach_req.eps_mobile_id.guti, &act_user))
        {
            rb->set_mme_state(LTE_FDD_ENB_MME_STATE_ATTACH_ACCEPT);
        }else{
            if((*user)->is_id_set())
            {
                if((*user)->get_eea_support(0) && (*user)->get_eia_support(2))
                {
                    rb->set_mme_state(LTE_FDD_ENB_MME_STATE_AUTHENTICATE);
                }else{
                    (*user)->set_emm_cause(LIBLTE_MME_EMM_CAUSE_UE_SECURITY_CAPABILITIES_MISMATCH);
                    rb->set_mme_state(LTE_FDD_ENB_MME_STATE_REJECT);
                }
            }else{
                rb->set_mme_state(LTE_FDD_ENB_MME_STATE_ID_REQUEST_IMSI);
            }
        }
    }else if(LIBLTE_MME_EPS_MOBILE_ID_TYPE_IMSI == attach_req.eps_mobile_id.type_of_id){
        for(i=0; i<15; i++)
        {
            imsi_num *= 10;
            imsi_num += attach_req.eps_mobile_id.imsi[i];
        }
        if(hss->is_imsi_allowed(imsi_num))
        {
            if((*user)->get_eea_support(0) && (*user)->get_eia_support(2))
            {
                rb->set_mme_state(LTE_FDD_ENB_MME_STATE_AUTHENTICATE);
                (*user)->set_id(hss->get_user_id_from_imsi(imsi_num));
            }else{
                (*user)->set_emm_cause(LIBLTE_MME_EMM_CAUSE_UE_SECURITY_CAPABILITIES_MISMATCH);
                rb->set_mme_state(LTE_FDD_ENB_MME_STATE_REJECT);
            }
        }else{
            (*user)->set_temp_id(imsi_num);
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
            if((*user)->get_eea_support(0) && (*user)->get_eia_support(2))
            {
                rb->set_mme_state(LTE_FDD_ENB_MME_STATE_AUTHENTICATE);
                (*user)->set_id(hss->get_user_id_from_imei(imei_num));
            }else{
                (*user)->set_emm_cause(LIBLTE_MME_EMM_CAUSE_UE_SECURITY_CAPABILITIES_MISMATCH);
                rb->set_mme_state(LTE_FDD_ENB_MME_STATE_REJECT);
            }
        }else{
            (*user)->set_temp_id(imei_num);
            rb->set_mme_state(LTE_FDD_ENB_MME_STATE_REJECT);
        }
    }
}
void LTE_fdd_enb_mme::parse_authentication_failure(LIBLTE_BYTE_MSG_STRUCT *msg,
                                                   LTE_fdd_enb_user       *user,
                                                   LTE_fdd_enb_rb         *rb)
{
    LTE_fdd_enb_interface                        *interface = LTE_fdd_enb_interface::get_instance();
    LTE_fdd_enb_hss                              *hss       = LTE_fdd_enb_hss::get_instance();
    LIBLTE_MME_AUTHENTICATION_FAILURE_MSG_STRUCT  auth_fail;

    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                              LTE_FDD_ENB_DEBUG_LEVEL_MME,
                              __FILE__,
                              __LINE__,
                              "Received Authentication Failure for RNTI=%u and RB=%s",
                              user->get_c_rnti(),
                              LTE_fdd_enb_rb_text[rb->get_rb_id()]);

    // Unpack the message
    liblte_mme_unpack_authentication_failure_msg(msg, &auth_fail);

    if(LIBLTE_MME_EMM_CAUSE_SYNCH_FAILURE == auth_fail.emm_cause &&
       auth_fail.auth_fail_param_present)
    {
        sys_info_mutex.lock();
        hss->security_resynch(user->get_id(), sys_info.mcc, sys_info.mnc, auth_fail.auth_fail_param);
        sys_info_mutex.unlock();
    }else{
        interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_ERROR,
                                  LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                  __FILE__,
                                  __LINE__,
                                  "Authentication failure cause=%02X, RNTI=%u, RB=%s",
                                  auth_fail.emm_cause,
                                  user->get_c_rnti(),
                                  LTE_fdd_enb_rb_text[rb->get_rb_id()]);
        rb->set_mme_state(LTE_FDD_ENB_MME_STATE_RELEASE);
    }
}
void LTE_fdd_enb_mme::parse_authentication_response(LIBLTE_BYTE_MSG_STRUCT *msg,
                                                    LTE_fdd_enb_user       *user,
                                                    LTE_fdd_enb_rb         *rb)
{
    LTE_fdd_enb_interface                         *interface = LTE_fdd_enb_interface::get_instance();
    LTE_fdd_enb_hss                               *hss       = LTE_fdd_enb_hss::get_instance();
    LTE_FDD_ENB_AUTHENTICATION_VECTOR_STRUCT      *auth_vec  = NULL;
    LIBLTE_MME_AUTHENTICATION_RESPONSE_MSG_STRUCT  auth_resp;
    uint32                                         i;
    bool                                           res_match = true;

    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                              LTE_FDD_ENB_DEBUG_LEVEL_MME,
                              __FILE__,
                              __LINE__,
                              "Received Authentication Response for RNTI=%u and RB=%s",
                              user->get_c_rnti(),
                              LTE_fdd_enb_rb_text[rb->get_rb_id()]);

    // Unpack the message
    liblte_mme_unpack_authentication_response_msg(msg, &auth_resp);

    // Check RES
    auth_vec = hss->get_auth_vec(user->get_id());
    if(NULL != auth_vec)
    {
        res_match = true;
        for(i=0; i<8; i++)
        {
            if(auth_vec->res[i] != auth_resp.res[i])
            {
                res_match = false;
                break;
            }
        }

        if(res_match)
        {
            interface->send_ctrl_info_msg("user authentication successful imsi=%s imei=%s",
                                          user->get_imsi_str().c_str(),
                                          user->get_imei_str().c_str());
            interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                                      LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                      __FILE__,
                                      __LINE__,
                                      "Authentication successful for RNTI=%u and RB=%s",
                                      user->get_c_rnti(),
                                      LTE_fdd_enb_rb_text[rb->get_rb_id()]);
            user->set_auth_vec(auth_vec);
            rb->set_mme_state(LTE_FDD_ENB_MME_STATE_ENABLE_SECURITY);
        }else{
            interface->send_ctrl_info_msg("user authentication rejected (RES MISMATCH) imsi=%s imei=%s",
                                          user->get_imsi_str().c_str(),
                                          user->get_imei_str().c_str());
            interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                                      LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                      __FILE__,
                                      __LINE__,
                                      "Authentication rejected (RES MISMATCH) for RNTI=%u and RB=%s",
                                      user->get_c_rnti(),
                                      LTE_fdd_enb_rb_text[rb->get_rb_id()]);
            rb->set_mme_state(LTE_FDD_ENB_MME_STATE_AUTH_REJECTED);
        }
    }else{
        interface->send_ctrl_info_msg("user authentication rejected (NO AUTH VEC) imsi=%s imei=%s",
                                      user->get_imsi_str().c_str(),
                                      user->get_imei_str().c_str());
        interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                                  LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                  __FILE__,
                                  __LINE__,
                                  "Authentication rejected (NO AUTH VEC) for RNTI=%u and RB=%s",
                                  user->get_c_rnti(),
                                  LTE_fdd_enb_rb_text[rb->get_rb_id()]);
        rb->set_mme_state(LTE_FDD_ENB_MME_STATE_AUTH_REJECTED);
    }
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

    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                              LTE_FDD_ENB_DEBUG_LEVEL_MME,
                              __FILE__,
                              __LINE__,
                              "Received Identity Response for RNTI=%u and RB=%s",
                              user->get_c_rnti(),
                              LTE_fdd_enb_rb_text[rb->get_rb_id()]);

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
            if(user->get_eea_support(0) && user->get_eia_support(2))
            {
                rb->set_mme_state(LTE_FDD_ENB_MME_STATE_AUTHENTICATE);
                user->set_id(hss->get_user_id_from_imsi(imsi_num));
            }else{
                user->set_emm_cause(LIBLTE_MME_EMM_CAUSE_UE_SECURITY_CAPABILITIES_MISMATCH);
                rb->set_mme_state(LTE_FDD_ENB_MME_STATE_REJECT);
            }
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
            if(user->get_eea_support(0) && user->get_eia_support(2))
            {
                rb->set_mme_state(LTE_FDD_ENB_MME_STATE_AUTHENTICATE);
                user->set_id(hss->get_user_id_from_imei(imei_num));
            }else{
                user->set_emm_cause(LIBLTE_MME_EMM_CAUSE_UE_SECURITY_CAPABILITIES_MISMATCH);
                rb->set_mme_state(LTE_FDD_ENB_MME_STATE_REJECT);
            }
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
void LTE_fdd_enb_mme::parse_security_mode_complete(LIBLTE_BYTE_MSG_STRUCT *msg,
                                                   LTE_fdd_enb_user       *user,
                                                   LTE_fdd_enb_rb         *rb)
{
    LTE_fdd_enb_interface                        *interface = LTE_fdd_enb_interface::get_instance();
    LIBLTE_MME_SECURITY_MODE_COMPLETE_MSG_STRUCT  sec_mode_comp;
    uint64                                        imei_num = 0;
    uint32                                        i;

    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                              LTE_FDD_ENB_DEBUG_LEVEL_MME,
                              __FILE__,
                              __LINE__,
                              "Received Security Mode Complete for RNTI=%u and RB=%s",
                              user->get_c_rnti(),
                              LTE_fdd_enb_rb_text[rb->get_rb_id()]);

    // Unpack the message
    liblte_mme_unpack_security_mode_complete_msg(msg, &sec_mode_comp);

    if(sec_mode_comp.imeisv_present)
    {
        if(LIBLTE_MME_MOBILE_ID_TYPE_IMEISV == sec_mode_comp.imeisv.type_of_id)
        {
            for(i=0; i<14; i++)
            {
                imei_num *= 10;
                imei_num += sec_mode_comp.imeisv.imeisv[i];
            }
            if((user->get_id()->imei/10) != imei_num)
            {
                interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_ERROR,
                                          LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                          __FILE__,
                                          __LINE__,
                                          "Received IMEI (%llu) does not match stored IMEI (%llu), RNTI=%u, RB=%s",
                                          imei_num*10,
                                          (user->get_id()->imei/10)*10,
                                          user->get_c_rnti(),
                                          LTE_fdd_enb_rb_text[rb->get_rb_id()]);
            }
        }else{
            interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_ERROR,
                                      LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                      __FILE__,
                                      __LINE__,
                                      "Security Mode Complete received with invalid ID type (%u), RNTI=%u, RB=%s",
                                      sec_mode_comp.imeisv.type_of_id,
                                      user->get_c_rnti(),
                                      LTE_fdd_enb_rb_text[rb->get_rb_id()]);
        }
    }

    rb->set_mme_state(LTE_FDD_ENB_MME_STATE_RRC_SECURITY);
}
void LTE_fdd_enb_mme::parse_security_mode_reject(LIBLTE_BYTE_MSG_STRUCT *msg,
                                                 LTE_fdd_enb_user       *user,
                                                 LTE_fdd_enb_rb         *rb)
{
    LTE_fdd_enb_interface                      *interface = LTE_fdd_enb_interface::get_instance();
    LIBLTE_MME_SECURITY_MODE_REJECT_MSG_STRUCT  sec_mode_rej;

    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                              LTE_FDD_ENB_DEBUG_LEVEL_MME,
                              __FILE__,
                              __LINE__,
                              "Received Security Mode Reject for RNTI=%u and RB=%s",
                              user->get_c_rnti(),
                              LTE_fdd_enb_rb_text[rb->get_rb_id()]);

    // Unpack the message
    liblte_mme_unpack_security_mode_reject_msg(msg, &sec_mode_rej);

    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_ERROR,
                              LTE_FDD_ENB_DEBUG_LEVEL_MME,
                              __FILE__,
                              __LINE__,
                              "Security Mode Rejected cause=%02X, RNTI=%u, RB=%s",
                              sec_mode_rej.emm_cause,
                              user->get_c_rnti(),
                              LTE_fdd_enb_rb_text[rb->get_rb_id()]);
    rb->set_mme_state(LTE_FDD_ENB_MME_STATE_RELEASE);
}
void LTE_fdd_enb_mme::parse_activate_default_eps_bearer_context_accept(LIBLTE_BYTE_MSG_STRUCT *msg,
                                                                       LTE_fdd_enb_user       *user,
                                                                       LTE_fdd_enb_rb         *rb)
{
    LTE_fdd_enb_interface                                            *interface = LTE_fdd_enb_interface::get_instance();
    LIBLTE_MME_ACTIVATE_DEFAULT_EPS_BEARER_CONTEXT_ACCEPT_MSG_STRUCT  act_def_eps_bearer_context_accept;

    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                              LTE_FDD_ENB_DEBUG_LEVEL_MME,
                              __FILE__,
                              __LINE__,
                              "Received Activate Default EPS Bearer Context Accept for RNTI=%u and RB=%s",
                              user->get_c_rnti(),
                              LTE_fdd_enb_rb_text[rb->get_rb_id()]);

    // Unpack the message
    liblte_mme_unpack_activate_default_eps_bearer_context_accept_msg(msg, &act_def_eps_bearer_context_accept);

    interface->send_ctrl_info_msg("default bearer setup for imsi=%s imei=%s",
                                  user->get_imsi_str().c_str(),
                                  user->get_imei_str().c_str());
}
void LTE_fdd_enb_mme::parse_esm_information_response(LIBLTE_BYTE_MSG_STRUCT *msg,
                                                     LTE_fdd_enb_user       *user,
                                                     LTE_fdd_enb_rb         *rb)
{
    LTE_fdd_enb_interface                          *interface = LTE_fdd_enb_interface::get_instance();
    LIBLTE_MME_ESM_INFORMATION_RESPONSE_MSG_STRUCT  esm_info_resp;

    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                              LTE_FDD_ENB_DEBUG_LEVEL_MME,
                              __FILE__,
                              __LINE__,
                              "Received ESM Information Response for RNTI=%u and RB=%s",
                              user->get_c_rnti(),
                              LTE_fdd_enb_rb_text[rb->get_rb_id()]);

    // Unpack the message
    liblte_mme_unpack_esm_information_response_msg(msg, &esm_info_resp);

    // FIXME

    rb->set_mme_state(LTE_FDD_ENB_MME_STATE_ATTACH_ACCEPT);
}
void LTE_fdd_enb_mme::parse_pdn_connectivity_request(LIBLTE_BYTE_MSG_STRUCT *msg,
                                                     LTE_fdd_enb_user       *user,
                                                     LTE_fdd_enb_rb         *rb)
{
    LTE_fdd_enb_interface                          *interface = LTE_fdd_enb_interface::get_instance();
    LIBLTE_MME_PDN_CONNECTIVITY_REQUEST_MSG_STRUCT  pdn_con_req;
    LIBLTE_MME_PROTOCOL_CONFIG_OPTIONS_STRUCT       pco_resp;
    uint32                                          i;

    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                              LTE_FDD_ENB_DEBUG_LEVEL_MME,
                              __FILE__,
                              __LINE__,
                              "Received PDN Connectivity Request for RNTI=%u and RB=%s",
                              user->get_c_rnti(),
                              LTE_fdd_enb_rb_text[rb->get_rb_id()]);

    // Unpack the message
    liblte_mme_unpack_pdn_connectivity_request_msg(msg, &pdn_con_req);

    // Store the EPS Bearer ID
    user->set_eps_bearer_id(pdn_con_req.eps_bearer_id);

    // Store the Procedure Transaction ID
    user->set_proc_transaction_id(pdn_con_req.proc_transaction_id);

    // Store the PDN Type
    user->set_pdn_type(pdn_con_req.pdn_type);

    // Store the ESM Information Transfer Flag
//    if(pdn_con_req.esm_info_transfer_flag_present &&
//       LIBLTE_MME_ESM_INFO_TRANSFER_FLAG_REQUIRED == pdn_con_req.esm_info_transfer_flag)
//    {
//        user->set_esm_info_transfer(true);
//    }else{
        user->set_esm_info_transfer(false);
//    }

    if(pdn_con_req.protocol_cnfg_opts_present)
    {
        pco_resp.N_opts = 0;
        for(i=0; i<pdn_con_req.protocol_cnfg_opts.N_opts; i++)
        {
            if(LIBLTE_MME_CONFIGURATION_PROTOCOL_OPTIONS_IPCP == pdn_con_req.protocol_cnfg_opts.opt[i].id)
            {
                if(0x01 == pdn_con_req.protocol_cnfg_opts.opt[i].contents[0] &&
                   0x81 == pdn_con_req.protocol_cnfg_opts.opt[i].contents[4] &&
                   0x83 == pdn_con_req.protocol_cnfg_opts.opt[i].contents[10])
                {
                    pco_resp.opt[pco_resp.N_opts].id           = LIBLTE_MME_CONFIGURATION_PROTOCOL_OPTIONS_IPCP;
                    pco_resp.opt[pco_resp.N_opts].len          = 16;
                    pco_resp.opt[pco_resp.N_opts].contents[0]  = 0x03;
                    pco_resp.opt[pco_resp.N_opts].contents[1]  = pdn_con_req.protocol_cnfg_opts.opt[i].contents[1];
                    pco_resp.opt[pco_resp.N_opts].contents[2]  = 0x00;
                    pco_resp.opt[pco_resp.N_opts].contents[3]  = 0x10;
                    pco_resp.opt[pco_resp.N_opts].contents[4]  = 0x81;
                    pco_resp.opt[pco_resp.N_opts].contents[5]  = 0x06;
                    pco_resp.opt[pco_resp.N_opts].contents[6]  = (dns_addr >> 24) & 0xFF;
                    pco_resp.opt[pco_resp.N_opts].contents[7]  = (dns_addr >> 16) & 0xFF;
                    pco_resp.opt[pco_resp.N_opts].contents[8]  = (dns_addr >> 8) & 0xFF;
                    pco_resp.opt[pco_resp.N_opts].contents[9]  = dns_addr & 0xFF;
                    pco_resp.opt[pco_resp.N_opts].contents[10] = 0x83;
                    pco_resp.opt[pco_resp.N_opts].contents[11] = 0x06;
                    pco_resp.opt[pco_resp.N_opts].contents[12] = (dns_addr >> 24) & 0xFF;
                    pco_resp.opt[pco_resp.N_opts].contents[13] = (dns_addr >> 16) & 0xFF;
                    pco_resp.opt[pco_resp.N_opts].contents[14] = (dns_addr >> 8) & 0xFF;
                    pco_resp.opt[pco_resp.N_opts].contents[15] = dns_addr & 0xFF;
                    pco_resp.N_opts++;
                }else{
                    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_ERROR,
                                              LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                              __FILE__,
                                              __LINE__,
                                              "Unknown PCO");
                }
            }else if(LIBLTE_MME_ADDITIONAL_PARAMETERS_UL_DNS_SERVER_IPV4_ADDRESS_REQUEST == pdn_con_req.protocol_cnfg_opts.opt[i].id){
                pco_resp.opt[pco_resp.N_opts].id          = LIBLTE_MME_ADDITIONAL_PARAMETERS_DL_DNS_SERVER_IPV4_ADDRESS;
                pco_resp.opt[pco_resp.N_opts].len         = 4;
                pco_resp.opt[pco_resp.N_opts].contents[0] = (dns_addr >> 24) & 0xFF;
                pco_resp.opt[pco_resp.N_opts].contents[1] = (dns_addr >> 16) & 0xFF;
                pco_resp.opt[pco_resp.N_opts].contents[2] = (dns_addr >> 8) & 0xFF;
                pco_resp.opt[pco_resp.N_opts].contents[3] = dns_addr & 0xFF;
                pco_resp.N_opts++;
            }else if(LIBLTE_MME_ADDITIONAL_PARAMETERS_UL_IP_ADDRESS_ALLOCATION_VIA_NAS_SIGNALLING == pdn_con_req.protocol_cnfg_opts.opt[i].id){
                // Nothing to do
            }else{
                interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_ERROR,
                                          LTE_FDD_ENB_DEBUG_LEVEL_MME,
                                          __FILE__,
                                          __LINE__,
                                          "Invalid PCO ID (%04X)",
                                          pdn_con_req.protocol_cnfg_opts.opt[i].id);
            }
        }
        user->set_protocol_cnfg_opts(&pco_resp);
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
        user->set_delete_at_idle(true);
        send_attach_reject(user, rb);
        break;
    case LTE_FDD_ENB_MME_STATE_AUTHENTICATE:
        send_authentication_request(user, rb);
        break;
    case LTE_FDD_ENB_MME_STATE_AUTH_REJECTED:
        send_authentication_reject(user, rb);
        break;
    case LTE_FDD_ENB_MME_STATE_ENABLE_SECURITY:
        send_security_mode_command(user, rb);
        break;
    case LTE_FDD_ENB_MME_STATE_RELEASE:
        send_rrc_command(user, rb, LTE_FDD_ENB_RRC_CMD_RELEASE);
        break;
    case LTE_FDD_ENB_MME_STATE_RRC_SECURITY:
        send_rrc_command(user, rb, LTE_FDD_ENB_RRC_CMD_SECURITY);
        break;
    case LTE_FDD_ENB_MME_STATE_ESM_INFO_TRANSFER:
        send_esm_information_request(user, rb);
        break;
    case LTE_FDD_ENB_MME_STATE_ATTACH_ACCEPT:
        send_attach_accept(user, rb);
        break;
    case LTE_FDD_ENB_MME_STATE_ATTACHED:
        send_rrc_command(user, rb, LTE_FDD_ENB_RRC_CMD_RELEASE);
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
void LTE_fdd_enb_mme::send_attach_accept(LTE_fdd_enb_user *user,
                                         LTE_fdd_enb_rb   *rb)
{
    LTE_fdd_enb_interface                                             *interface = LTE_fdd_enb_interface::get_instance();
    LTE_fdd_enb_user_mgr                                              *user_mgr  = LTE_fdd_enb_user_mgr::get_instance();
    LTE_FDD_ENB_RRC_CMD_READY_MSG_STRUCT                               cmd_ready;
    LIBLTE_MME_ATTACH_ACCEPT_MSG_STRUCT                                attach_accept;
    LIBLTE_MME_ACTIVATE_DEFAULT_EPS_BEARER_CONTEXT_REQUEST_MSG_STRUCT  act_def_eps_bearer_context_req;
    LIBLTE_MME_PROTOCOL_CONFIG_OPTIONS_STRUCT                         *pco = user->get_protocol_cnfg_opts();
    LIBLTE_BYTE_MSG_STRUCT                                             msg;
    uint32                                                             ip_addr;

    // Assign IP address to user
    user->set_ip_addr(get_next_ip_addr());
    ip_addr = user->get_ip_addr();

    if(0 == user->get_eps_bearer_id())
    {
        act_def_eps_bearer_context_req.eps_bearer_id = 5;
        user->set_eps_bearer_id(5);
    }else{
        act_def_eps_bearer_context_req.eps_bearer_id = user->get_eps_bearer_id();
    }
    if(0 == user->get_proc_transaction_id())
    {
        act_def_eps_bearer_context_req.proc_transaction_id = 1;
        user->set_proc_transaction_id(1);
    }else{
        act_def_eps_bearer_context_req.proc_transaction_id = user->get_proc_transaction_id();
    }
    act_def_eps_bearer_context_req.eps_qos.qci            = 9;
    act_def_eps_bearer_context_req.eps_qos.br_present     = false;
    act_def_eps_bearer_context_req.eps_qos.br_ext_present = false;
    act_def_eps_bearer_context_req.apn.apn                = "www.openLTE.com";
    act_def_eps_bearer_context_req.pdn_addr.pdn_type      = LIBLTE_MME_PDN_TYPE_IPV4;
    act_def_eps_bearer_context_req.pdn_addr.addr[0]       = (ip_addr >> 24) & 0xFF;
    act_def_eps_bearer_context_req.pdn_addr.addr[1]       = (ip_addr >> 16) & 0xFF;
    act_def_eps_bearer_context_req.pdn_addr.addr[2]       = (ip_addr >> 8) & 0xFF;
    act_def_eps_bearer_context_req.pdn_addr.addr[3]       = ip_addr & 0xFF;
    act_def_eps_bearer_context_req.transaction_id_present = false;
    act_def_eps_bearer_context_req.negotiated_qos_present = false;
    act_def_eps_bearer_context_req.llc_sapi_present       = false;
    act_def_eps_bearer_context_req.radio_prio_present     = false;
    act_def_eps_bearer_context_req.packet_flow_id_present = false;
    act_def_eps_bearer_context_req.apn_ambr_present       = false;
    if(LIBLTE_MME_PDN_TYPE_IPV4 == user->get_pdn_type())
    {
        act_def_eps_bearer_context_req.esm_cause_present = false;
    }else{
        act_def_eps_bearer_context_req.esm_cause_present = true;
        act_def_eps_bearer_context_req.esm_cause         = LIBLTE_MME_ESM_CAUSE_PDN_TYPE_IPV4_ONLY_ALLOWED;
    }
    if(0 != pco->N_opts)
    {
        act_def_eps_bearer_context_req.protocol_cnfg_opts_present = true;
        memcpy(&act_def_eps_bearer_context_req.protocol_cnfg_opts, pco, sizeof(LIBLTE_MME_PROTOCOL_CONFIG_OPTIONS_STRUCT));
    }else{
        act_def_eps_bearer_context_req.protocol_cnfg_opts_present = false;
    }
    act_def_eps_bearer_context_req.connectivity_type_present = false;
    liblte_mme_pack_activate_default_eps_bearer_context_request_msg(&act_def_eps_bearer_context_req,
                                                                    &attach_accept.esm_msg);

    sys_info_mutex.lock();
    attach_accept.eps_attach_result                   = user->get_attach_type();
    attach_accept.t3412.unit                          = LIBLTE_MME_GPRS_TIMER_DEACTIVATED;
    attach_accept.tai_list.N_tais                     = 1;
    attach_accept.tai_list.tai[0].mcc                 = sys_info.mcc;
    attach_accept.tai_list.tai[0].mnc                 = sys_info.mnc;
    attach_accept.tai_list.tai[0].tac                 = sys_info.sib1.tracking_area_code;
    attach_accept.guti_present                        = true;
    attach_accept.guti.type_of_id                     = LIBLTE_MME_EPS_MOBILE_ID_TYPE_GUTI;
    attach_accept.guti.guti.mcc                       = sys_info.mcc;
    attach_accept.guti.guti.mnc                       = sys_info.mnc;
    attach_accept.guti.guti.mme_group_id              = 0;
    attach_accept.guti.guti.mme_code                  = 0;
    attach_accept.guti.guti.m_tmsi                    = user_mgr->get_next_m_tmsi();
    attach_accept.lai_present                         = false;
    attach_accept.ms_id_present                       = false;
    attach_accept.emm_cause_present                   = false;
    attach_accept.t3402_present                       = false;
    attach_accept.t3423_present                       = false;
    attach_accept.equivalent_plmns_present            = false;
    attach_accept.emerg_num_list_present              = false;
    attach_accept.eps_network_feature_support_present = false;
    attach_accept.additional_update_result_present    = false;
    attach_accept.t3412_ext_present                   = false;
    sys_info_mutex.unlock();
    user->set_guti(&attach_accept.guti.guti);
    liblte_mme_pack_attach_accept_msg(&attach_accept,
                                      LIBLTE_MME_SECURITY_HDR_TYPE_INTEGRITY_AND_CIPHERED,
                                      user->get_auth_vec()->k_nas_int,
                                      user->get_auth_vec()->nas_count_dl,
                                      LIBLTE_SECURITY_DIRECTION_DOWNLINK,
                                      rb->get_rb_id()-1,
                                      &msg);
    user->increment_nas_count_dl();
    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                              LTE_FDD_ENB_DEBUG_LEVEL_MME,
                              __FILE__,
                              __LINE__,
                              &msg,
                              "Sending Attach Accept for RNTI=%u, RB=%s",
                              user->get_c_rnti(),
                              LTE_fdd_enb_rb_text[rb->get_rb_id()]);

    // Queue the NAS message for RRC
    rb->queue_rrc_nas_msg(&msg);

    // Signal RRC for NAS message
    cmd_ready.user = user;
    cmd_ready.rb   = rb;
    cmd_ready.cmd  = LTE_FDD_ENB_RRC_CMD_SETUP_SRB2;
    LTE_fdd_enb_msgq::send(mme_rrc_mq,
                           LTE_FDD_ENB_MESSAGE_TYPE_RRC_CMD_READY,
                           LTE_FDD_ENB_DEST_LAYER_RRC,
                           (LTE_FDD_ENB_MESSAGE_UNION *)&cmd_ready,
                           sizeof(LTE_FDD_ENB_RRC_CMD_READY_MSG_STRUCT));
}
void LTE_fdd_enb_mme::send_attach_reject(LTE_fdd_enb_user *user,
                                         LTE_fdd_enb_rb   *rb)
{
    LTE_fdd_enb_interface                    *interface = LTE_fdd_enb_interface::get_instance();
    LTE_FDD_ENB_RRC_NAS_MSG_READY_MSG_STRUCT  nas_msg_ready;
    LIBLTE_MME_ATTACH_REJECT_MSG_STRUCT       attach_rej;
    LIBLTE_BYTE_MSG_STRUCT                    msg;

    attach_rej.emm_cause           = user->get_emm_cause();
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

    send_rrc_command(user, rb, LTE_FDD_ENB_RRC_CMD_RELEASE);
}
void LTE_fdd_enb_mme::send_authentication_reject(LTE_fdd_enb_user *user,
                                                 LTE_fdd_enb_rb   *rb)
{
    LTE_fdd_enb_interface                       *interface = LTE_fdd_enb_interface::get_instance();
    LTE_FDD_ENB_RRC_NAS_MSG_READY_MSG_STRUCT     nas_msg_ready;
    LIBLTE_MME_AUTHENTICATION_REJECT_MSG_STRUCT  auth_rej;
    LIBLTE_BYTE_MSG_STRUCT                       msg;

    liblte_mme_pack_authentication_reject_msg(&auth_rej, &msg);
    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                              LTE_FDD_ENB_DEBUG_LEVEL_MME,
                              __FILE__,
                              __LINE__,
                              &msg,
                              "Sending Authentication Reject for RNTI=%u, RB=%s",
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

    send_rrc_command(user, rb, LTE_FDD_ENB_RRC_CMD_RELEASE);
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
    hss->generate_security_data(user->get_id(), sys_info.mcc, sys_info.mnc);
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
void LTE_fdd_enb_mme::send_security_mode_command(LTE_fdd_enb_user *user,
                                                 LTE_fdd_enb_rb   *rb)
{
    LTE_fdd_enb_interface                       *interface = LTE_fdd_enb_interface::get_instance();
    LTE_FDD_ENB_RRC_NAS_MSG_READY_MSG_STRUCT     nas_msg_ready;
    LIBLTE_MME_SECURITY_MODE_COMMAND_MSG_STRUCT  sec_mode_cmd;
    LIBLTE_BYTE_MSG_STRUCT                       msg;
    uint32                                       i;

    sec_mode_cmd.selected_nas_sec_algs.type_of_eea = LIBLTE_MME_TYPE_OF_CIPHERING_ALGORITHM_EEA0;
    sec_mode_cmd.selected_nas_sec_algs.type_of_eia = LIBLTE_MME_TYPE_OF_INTEGRITY_ALGORITHM_128_EIA2;
    sec_mode_cmd.nas_ksi.tsc_flag                  = LIBLTE_MME_TYPE_OF_SECURITY_CONTEXT_FLAG_NATIVE;
    sec_mode_cmd.nas_ksi.nas_ksi                   = 0;
    for(i=0; i<8; i++)
    {
        sec_mode_cmd.ue_security_cap.eea[i] = user->get_eea_support(i);
        sec_mode_cmd.ue_security_cap.eia[i] = user->get_eia_support(i);
        sec_mode_cmd.ue_security_cap.uea[i] = user->get_uea_support(i);
        sec_mode_cmd.ue_security_cap.uia[i] = user->get_uia_support(i);
        sec_mode_cmd.ue_security_cap.gea[i] = user->get_gea_support(i);
    }
    if(user->is_uea_set())
    {
        sec_mode_cmd.ue_security_cap.uea_present = true;
    }else{
        sec_mode_cmd.ue_security_cap.uea_present = false;
    }
    if(user->is_uia_set())
    {
        sec_mode_cmd.ue_security_cap.uia_present = true;
    }else{
        sec_mode_cmd.ue_security_cap.uia_present = false;
    }
    if(user->is_gea_set())
    {
        sec_mode_cmd.ue_security_cap.gea_present = true;
    }else{
        sec_mode_cmd.ue_security_cap.gea_present = false;
    }
    sec_mode_cmd.imeisv_req         = LIBLTE_MME_IMEISV_REQUESTED;
    sec_mode_cmd.imeisv_req_present = true;
    sec_mode_cmd.nonce_ue_present   = false;
    sec_mode_cmd.nonce_mme_present  = false;
    liblte_mme_pack_security_mode_command_msg(&sec_mode_cmd,
                                              LIBLTE_MME_SECURITY_HDR_TYPE_INTEGRITY_WITH_NEW_EPS_SECURITY_CONTEXT,
                                              user->get_auth_vec()->k_nas_int,
                                              user->get_auth_vec()->nas_count_dl,
                                              LIBLTE_SECURITY_DIRECTION_DOWNLINK,
                                              rb->get_rb_id()-1,
                                              &msg);
    user->increment_nas_count_dl();
    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                              LTE_FDD_ENB_DEBUG_LEVEL_MME,
                              __FILE__,
                              __LINE__,
                              &msg,
                              "Sending Security Mode Command for RNTI=%u, RB=%s",
                              user->get_c_rnti(),
                              LTE_fdd_enb_rb_text[rb->get_rb_id()]);

    // Queue the message for RRC
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
void LTE_fdd_enb_mme::send_esm_information_request(LTE_fdd_enb_user *user,
                                                   LTE_fdd_enb_rb   *rb)
{
    LTE_fdd_enb_interface                         *interface = LTE_fdd_enb_interface::get_instance();
    LTE_FDD_ENB_RRC_NAS_MSG_READY_MSG_STRUCT       nas_msg_ready;
    LIBLTE_MME_ESM_INFORMATION_REQUEST_MSG_STRUCT  esm_info_req;
    LIBLTE_BYTE_MSG_STRUCT                         msg;
    LIBLTE_BYTE_MSG_STRUCT                         sec_msg;

    esm_info_req.eps_bearer_id = 0;
    if(0 == user->get_proc_transaction_id())
    {
        esm_info_req.proc_transaction_id = 1;
        user->set_proc_transaction_id(1);
    }else{
        esm_info_req.proc_transaction_id = user->get_proc_transaction_id();
    }
    liblte_mme_pack_esm_information_request_msg(&esm_info_req, &msg);
    liblte_mme_pack_security_protected_nas_msg(&msg,
                                               LIBLTE_MME_SECURITY_HDR_TYPE_INTEGRITY_AND_CIPHERED,
                                               user->get_auth_vec()->k_nas_int,
                                               user->get_auth_vec()->nas_count_dl,
                                               LIBLTE_SECURITY_DIRECTION_DOWNLINK,
                                               rb->get_rb_id()-1,
                                               &sec_msg);
    interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                              LTE_FDD_ENB_DEBUG_LEVEL_MME,
                              __FILE__,
                              __LINE__,
                              &sec_msg,
                              "Sending ESM Info Request for RNTI=%u, RB=%s",
                              user->get_c_rnti(),
                              LTE_fdd_enb_rb_text[rb->get_rb_id()]);

    // Queue the NAS message for RRC
    rb->queue_rrc_nas_msg(&sec_msg);

    // Signal RRC
    nas_msg_ready.user = user;
    nas_msg_ready.rb   = rb;
    LTE_fdd_enb_msgq::send(mme_rrc_mq,
                           LTE_FDD_ENB_MESSAGE_TYPE_RRC_NAS_MSG_READY,
                           LTE_FDD_ENB_DEST_LAYER_RRC,
                           (LTE_FDD_ENB_MESSAGE_UNION *)&nas_msg_ready,
                           sizeof(LTE_FDD_ENB_RRC_NAS_MSG_READY_MSG_STRUCT));
}
void LTE_fdd_enb_mme::send_rrc_command(LTE_fdd_enb_user         *user,
                                       LTE_fdd_enb_rb           *rb,
                                       LTE_FDD_ENB_RRC_CMD_ENUM  cmd)
{
    LTE_FDD_ENB_RRC_CMD_READY_MSG_STRUCT cmd_ready;

    // Signal RRC for command
    cmd_ready.user = user;
    cmd_ready.rb   = rb;
    cmd_ready.cmd  = cmd;
    LTE_fdd_enb_msgq::send(mme_rrc_mq,
                           LTE_FDD_ENB_MESSAGE_TYPE_RRC_CMD_READY,
                           LTE_FDD_ENB_DEST_LAYER_RRC,
                           (LTE_FDD_ENB_MESSAGE_UNION *)&cmd_ready,
                           sizeof(LTE_FDD_ENB_RRC_CMD_READY_MSG_STRUCT));
}

/*****************/
/*    Helpers    */
/*****************/
uint32 LTE_fdd_enb_mme::get_next_ip_addr(void)
{
    uint32 ip_addr = next_ip_addr;

    next_ip_addr++;
    if((next_ip_addr & 0xFF) == 0xFF)
    {
        next_ip_addr++;
    }

    return(ip_addr);
}
