#line 2 "LTE_fdd_enb_user_mgr.cc" // Make __FILE__ omit the path
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

    File: LTE_fdd_enb_user_mgr.cc

    Description: Contains all the implementations for the LTE FDD eNodeB
                 user manager.

    Revision History
    ----------    -------------    --------------------------------------------
    11/10/2013    Ben Wojtowicz    Created file
    01/18/2014    Ben Wojtowicz    Added level to debug prints.
    05/04/2014    Ben Wojtowicz    Added C-RNTI timeout timers.
    06/15/2014    Ben Wojtowicz    Deleting user on C-RNTI expiration.
    08/03/2014    Ben Wojtowicz    Refactored add_user.
    11/01/2014    Ben Wojtowicz    Added M-TMSI assignment.

*******************************************************************************/

/*******************************************************************************
                              INCLUDES
*******************************************************************************/

#include "LTE_fdd_enb_user_mgr.h"
#include "LTE_fdd_enb_timer_mgr.h"
#include "liblte_mac.h"
#include <boost/lexical_cast.hpp>

/*******************************************************************************
                              DEFINES
*******************************************************************************/


/*******************************************************************************
                              TYPEDEFS
*******************************************************************************/


/*******************************************************************************
                              GLOBAL VARIABLES
*******************************************************************************/

LTE_fdd_enb_user_mgr* LTE_fdd_enb_user_mgr::instance = NULL;
boost::mutex          user_mgr_instance_mutex;

/*******************************************************************************
                              CLASS IMPLEMENTATIONS
*******************************************************************************/

/*******************/
/*    Singleton    */
/*******************/
LTE_fdd_enb_user_mgr* LTE_fdd_enb_user_mgr::get_instance(void)
{
    boost::mutex::scoped_lock lock(user_mgr_instance_mutex);

    if(NULL == instance)
    {
        instance = new LTE_fdd_enb_user_mgr();
    }

    return(instance);
}
void LTE_fdd_enb_user_mgr::cleanup(void)
{
    boost::mutex::scoped_lock lock(user_mgr_instance_mutex);

    if(NULL != instance)
    {
        delete instance;
        instance = NULL;
    }
}

/********************************/
/*    Constructor/Destructor    */
/********************************/
LTE_fdd_enb_user_mgr::LTE_fdd_enb_user_mgr()
{
    next_m_tmsi = 1;
    next_c_rnti = LIBLTE_MAC_C_RNTI_START;
}
LTE_fdd_enb_user_mgr::~LTE_fdd_enb_user_mgr()
{
}

/****************************/
/*    External Interface    */
/****************************/
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_user_mgr::get_free_c_rnti(uint16 *c_rnti)
{
    boost::mutex::scoped_lock                     lock(c_rnti_mutex);
    std::map<uint16, LTE_fdd_enb_user*>::iterator iter;
    LTE_FDD_ENB_ERROR_ENUM                        err          = LTE_FDD_ENB_ERROR_NO_FREE_C_RNTI;
    uint16                                        start_c_rnti = next_c_rnti;

    do
    {
        iter = c_rnti_map.find(next_c_rnti++);

        if(LIBLTE_MAC_C_RNTI_END < next_c_rnti)
        {
            next_c_rnti = LIBLTE_MAC_C_RNTI_START;
        }
        if(next_c_rnti == start_c_rnti)
        {
            break;
        }
    }while(c_rnti_map.end() != iter);

    if(next_c_rnti != start_c_rnti)
    {
        *c_rnti = next_c_rnti-1;
        err     = LTE_FDD_ENB_ERROR_NONE;
    }

    return(err);
}
void LTE_fdd_enb_user_mgr::assign_c_rnti(uint16            c_rnti,
                                         LTE_fdd_enb_user *user)
{
    boost::mutex::scoped_lock lock(c_rnti_mutex);

    c_rnti_map[c_rnti] = user;
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_user_mgr::free_c_rnti(uint16 c_rnti)
{
    boost::mutex::scoped_lock                     lock(c_rnti_mutex);
    std::map<uint16, LTE_fdd_enb_user*>::iterator iter = c_rnti_map.find(c_rnti);
    LTE_FDD_ENB_ERROR_ENUM                        err  = LTE_FDD_ENB_ERROR_C_RNTI_NOT_FOUND;

    if(c_rnti_map.end() != iter)
    {
        c_rnti_map.erase(iter);
        if((*iter).second->is_id_set())
        {
            (*iter).second->init();
        }else{
            c_rnti_mutex.unlock();
            del_user(c_rnti);
        }
        err = LTE_FDD_ENB_ERROR_NONE;
    }

    return(err);
}
uint32 LTE_fdd_enb_user_mgr::get_next_m_tmsi(void)
{
    return(next_m_tmsi++);
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_user_mgr::add_user(uint16 c_rnti)
{
    LTE_fdd_enb_interface  *interface = LTE_fdd_enb_interface::get_instance();
    LTE_fdd_enb_timer_mgr  *timer_mgr = LTE_fdd_enb_timer_mgr::get_instance();
    LTE_fdd_enb_user       *new_user  = NULL;
    LTE_fdd_enb_timer_cb    timer_expiry_cb(&LTE_fdd_enb_timer_cb_wrapper<LTE_fdd_enb_user_mgr, &LTE_fdd_enb_user_mgr::handle_c_rnti_timer_expiry>, this);
    LTE_FDD_ENB_ERROR_ENUM  err       = LTE_FDD_ENB_ERROR_NONE;
    uint64                  fake_imsi = 0xF000000000000000UL;
    uint32                  timer_id;

    new_user = new LTE_fdd_enb_user(c_rnti);

    if(NULL != new_user)
    {
        fake_imsi += c_rnti;

        // Allocate new user
        user_mutex.lock();
        user_map[fake_imsi] = new_user;
        user_mutex.unlock();

        // Start a C-RNTI reservation timer
        timer_mgr->start_timer(5000, timer_expiry_cb, &timer_id);
        timer_id_mutex.lock();
        timer_id_map[timer_id] = c_rnti;
        timer_id_mutex.unlock();

        // Assign C-RNTI
        assign_c_rnti(c_rnti, new_user);
    }else{
        err = LTE_FDD_ENB_ERROR_BAD_ALLOC;
    }

    return(err);
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_user_mgr::find_user(std::string        imsi,
                                                       LTE_fdd_enb_user **user)
{
    boost::mutex::scoped_lock                      lock(user_mutex);
    std::map<uint64, LTE_fdd_enb_user*>::iterator  iter;
    LTE_FDD_ENB_ERROR_ENUM                         err      = LTE_FDD_ENB_ERROR_USER_NOT_FOUND;
    const char                                    *imsi_str = imsi.c_str();
    uint64                                         imsi_num = 0;
    uint32                                         i;

    if(imsi.length() == 15)
    {
        for(i=0; i<15; i++)
        {
            imsi_num *= 10;
            imsi_num += imsi_str[i] - '0';
        }

        iter = user_map.find(imsi_num);

        if(user_map.end() != iter)
        {
            *user = (*iter).second;
            err   = LTE_FDD_ENB_ERROR_NONE;
        }
    }

    return(err);
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_user_mgr::find_user(uint16             c_rnti,
                                                       LTE_fdd_enb_user **user)
{
    boost::mutex::scoped_lock                     lock(c_rnti_mutex);
    std::map<uint16, LTE_fdd_enb_user*>::iterator iter = c_rnti_map.find(c_rnti);
    LTE_FDD_ENB_ERROR_ENUM                        err  = LTE_FDD_ENB_ERROR_USER_NOT_FOUND;

    if(c_rnti_map.end() != iter &&
       NULL             != (*iter).second)
    {
        *user = (*iter).second;
        err   = LTE_FDD_ENB_ERROR_NONE;
    }

    return(err);
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_user_mgr::find_user(LIBLTE_MME_EPS_MOBILE_ID_GUTI_STRUCT  *guti,
                                                       LTE_fdd_enb_user                     **user)
{
    return(LTE_FDD_ENB_ERROR_USER_NOT_FOUND);
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_user_mgr::del_user(std::string imsi)
{
    boost::mutex::scoped_lock                      lock(user_mutex);
    std::map<uint64, LTE_fdd_enb_user*>::iterator  iter;
    LTE_FDD_ENB_ERROR_ENUM                         err      = LTE_FDD_ENB_ERROR_USER_NOT_FOUND;
    const char                                    *imsi_str = imsi.c_str();
    uint64                                         imsi_num = 0;
    uint32                                         i;

    if(imsi.length() == 15)
    {
        for(i=0; i<15; i++)
        {
            imsi_num *= 10;
            imsi_num += imsi_str[i] - '0';
        }

        iter = user_map.find(imsi_num);

        if(user_map.end() != iter)
        {
            delete (*iter).second;
            user_map.erase(iter);
            err = LTE_FDD_ENB_ERROR_NONE;
        }
    }

    return(err);
}
LTE_FDD_ENB_ERROR_ENUM LTE_fdd_enb_user_mgr::del_user(uint16 c_rnti)
{
    boost::mutex::scoped_lock                      u_lock(user_mutex);
    boost::mutex::scoped_lock                      c_lock(c_rnti_mutex);
    boost::mutex::scoped_lock                      t_lock(timer_id_mutex);
    std::map<uint64, LTE_fdd_enb_user*>::iterator  u_iter;
    std::map<uint16, LTE_fdd_enb_user*>::iterator  c_iter = c_rnti_map.find(c_rnti);
    std::map<uint32, uint16>::iterator             t_iter;
    LTE_fdd_enb_timer_mgr                         *timer_mgr = LTE_fdd_enb_timer_mgr::get_instance();
    LTE_FDD_ENB_ERROR_ENUM                         err       = LTE_FDD_ENB_ERROR_USER_NOT_FOUND;
    uint64                                         fake_imsi = 0xF000000000000000UL;

    fake_imsi += c_rnti;

    u_iter = user_map.find(fake_imsi);

    if(user_map.end() != u_iter)
    {
        // Delete user class
        delete (*u_iter).second;
        user_map.erase(u_iter);

        // Free the C-RNTI
        if(c_rnti_map.end() != c_iter)
        {
            c_rnti_map.erase(c_iter);
        }

        // Stop the C-RNTI timer
        for(t_iter=timer_id_map.begin(); t_iter!=timer_id_map.end(); t_iter++)
        {
            if((*t_iter).second == c_rnti)
            {
                timer_mgr->stop_timer((*t_iter).first);
                break;
            }
        }

        err = LTE_FDD_ENB_ERROR_NONE;
    }

    return(err);
}

/**********************/
/*    C-RNTI Timer    */
/**********************/
void LTE_fdd_enb_user_mgr::handle_c_rnti_timer_expiry(uint32 timer_id)
{
    LTE_fdd_enb_interface              *interface = LTE_fdd_enb_interface::get_instance();
    boost::mutex::scoped_lock           lock(timer_id_mutex);
    std::map<uint32, uint16>::iterator  iter = timer_id_map.find(timer_id);

    if(timer_id_map.end() != iter)
    {
        interface->send_debug_msg(LTE_FDD_ENB_DEBUG_TYPE_INFO,
                                  LTE_FDD_ENB_DEBUG_LEVEL_USER,
                                  __FILE__,
                                  __LINE__,
                                  "C-RNTI allocation timer expiry C-RNTI=%u",
                                  (*iter).second);
        timer_id_map.erase(iter);

        timer_id_mutex.unlock();
        free_c_rnti((*iter).second);
    }
}
