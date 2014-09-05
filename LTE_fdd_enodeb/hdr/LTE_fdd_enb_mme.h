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

    File: LTE_fdd_enb_mme.h

    Description: Contains all the definitions for the LTE FDD eNodeB
                 mobility management entity layer.

    Revision History
    ----------    -------------    --------------------------------------------
    11/09/2013    Ben Wojtowicz    Created file
    01/18/2014    Ben Wojtowicz    Added an explicit include for boost mutexes.
    06/15/2014    Ben Wojtowicz    Added RRC NAS message handler.
    08/03/2014    Ben Wojtowicz    Added message parsers, state machines, and
                                   message senders.
    09/03/2014    Ben Wojtowicz    Added authentication and security support.

*******************************************************************************/

#ifndef __LTE_FDD_ENB_MME_H__
#define __LTE_FDD_ENB_MME_H__

/*******************************************************************************
                              INCLUDES
*******************************************************************************/

#include "LTE_fdd_enb_cnfg_db.h"
#include "LTE_fdd_enb_msgq.h"
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/thread/mutex.hpp>

/*******************************************************************************
                              DEFINES
*******************************************************************************/


/*******************************************************************************
                              FORWARD DECLARATIONS
*******************************************************************************/


/*******************************************************************************
                              TYPEDEFS
*******************************************************************************/


/*******************************************************************************
                              CLASS DECLARATIONS
*******************************************************************************/

class LTE_fdd_enb_mme
{
public:
    // Singleton
    static LTE_fdd_enb_mme* get_instance(void);
    static void cleanup(void);

    // Start/Stop
    void start(void);
    void stop(void);

    // External interface
    void update_sys_info(void);

private:
    // Singleton
    static LTE_fdd_enb_mme *instance;
    LTE_fdd_enb_mme();
    ~LTE_fdd_enb_mme();

    // Start/Stop
    boost::mutex start_mutex;
    bool         started;

    // Communication
    void handle_rrc_msg(LTE_FDD_ENB_MESSAGE_STRUCT *msg);
    LTE_fdd_enb_msgq                   *rrc_comm_msgq;
    boost::interprocess::message_queue *mme_rrc_mq;

    // RRC Message Handlers
    void handle_nas_msg(LTE_FDD_ENB_MME_NAS_MSG_READY_MSG_STRUCT *nas_msg);

    // Message Parsers
    void parse_attach_request(LIBLTE_BYTE_MSG_STRUCT *msg, LTE_fdd_enb_user *user, LTE_fdd_enb_rb *rb);
    void parse_authentication_failure(LIBLTE_BYTE_MSG_STRUCT *msg, LTE_fdd_enb_user *user, LTE_fdd_enb_rb *rb);
    void parse_authentication_response(LIBLTE_BYTE_MSG_STRUCT *msg, LTE_fdd_enb_user *user, LTE_fdd_enb_rb *rb);
    void parse_identity_response(LIBLTE_BYTE_MSG_STRUCT *msg, LTE_fdd_enb_user *user, LTE_fdd_enb_rb *rb);
    void parse_security_mode_complete(LIBLTE_BYTE_MSG_STRUCT *msg, LTE_fdd_enb_user *user, LTE_fdd_enb_rb *rb);
    void parse_security_mode_reject(LIBLTE_BYTE_MSG_STRUCT *msg, LTE_fdd_enb_user *user, LTE_fdd_enb_rb *rb);

    // State Machines
    void attach_sm(LTE_fdd_enb_user *user, LTE_fdd_enb_rb *rb);

    // Message Senders
    void send_attach_reject(LTE_fdd_enb_user *user, LTE_fdd_enb_rb *rb, uint8 rej_cause);
    void send_authentication_reject(LTE_fdd_enb_user *user, LTE_fdd_enb_rb *rb);
    void send_authentication_request(LTE_fdd_enb_user *user, LTE_fdd_enb_rb *rb);
    void send_identity_request(LTE_fdd_enb_user *user, LTE_fdd_enb_rb *rb, uint8 id_type);
    void send_security_mode_command(LTE_fdd_enb_user *user, LTE_fdd_enb_rb *rb);

    // Parameters
    boost::mutex                sys_info_mutex;
    LTE_FDD_ENB_SYS_INFO_STRUCT sys_info;
};

#endif /* __LTE_FDD_ENB_MME_H__ */
