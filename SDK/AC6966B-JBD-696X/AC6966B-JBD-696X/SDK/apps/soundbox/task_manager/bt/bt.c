
/*************************************************************
   此文件函数主要是蓝牙模式各种状态处理

	void app_bt_task()
   bt模式主函数

	static int bt_sys_event_handler(struct sys_event *event)
   bt模式系统事件所有处理入口

	static void bt_task_close(void)
	bt模式退出


	void bt_function_select_init()
    蓝牙功能配置

	void bredr_handle_register()
 	蓝牙协议栈回调函数

	static int bt_connction_status_event_handler(struct bt_event *bt)
	蓝牙状态事件处理函数

	static int bt_hci_event_handler(struct bt_event *bt)
	蓝牙协议栈事件回调，app处理函数

	static int bt_ai_event_handler(struct bt_event *bt)
	蓝牙ai事件处理函数

	int bt_background_event_handler_filter(struct sys_event *event)
	蓝牙后台事件过滤处理
	int bt_background_event_handler(struct sys_event *event)
	蓝牙后台事件处理函数

	int bt_key_event_handler(struct sys_event *event)
	蓝牙按键处理函数

	avctp_user.h 有蓝牙支持的命令介绍

**************************************************************/



#include "system/includes.h"
#include "media/includes.h"

#include "app_config.h"
#include "app_task.h"

#include "btstack/avctp_user.h"
#include "btstack/btstack_task.h"
#include "btstack/bluetooth.h"
#include "btstack/btstack_error.h"
#include "btctrler/btctrler_task.h"
#include "classic/hci_lmp.h"

#include "bt/bt_tws.h"
#include "bt/bt_ble.h"
#include "bt/bt.h"
#include "bt/vol_sync.h"
#include "bt/bt_emitter.h"
#include "bt_common.h"
#include "aec_user.h"

#include "math.h"
#include "spp_user.h"


#include "app_chargestore.h"
#include "app_charge.h"
#include "app_main.h"
#include "app_power_manage.h"
#include "user_cfg.h"

#include "asm/pwm_led.h"
#include "asm/timer.h"
#include "asm/hwi.h"
#include "cpu.h"

#include "ui/ui_api.h"
#include "ui_manage.h"
#include "ui/ui_style.h"

#include "key_event_deal.h"
#include "clock_cfg.h"
#include "gSensor/gSensor_manage.h"
#include "soundcard/soundcard.h"

#include "audio_dec.h"
#include "tone_player.h"
#include "dac.h"


#define LOG_TAG_CONST        BT
#define LOG_TAG             "[BT]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
#define LOG_DUMP_ENABLE
#define LOG_CLI_ENABLE
#include "debug.h"

#include "user_fun_cfg.h"

#if TCFG_APP_BT_EN

struct app_bt_opr app_bt_hdl = {
    .exit_flag = 1,
    .replay_tone_flag = 1,
    .esco_dump_packet = ESCO_DUMP_PACKET_CALL,
    .hid_mode = 0,
};

#define __this 	(&app_bt_hdl)

BT_USER_PRIV_VAR bt_user_priv_var;



/*----------------------------------------------------------------------------*/
/**@brief    蓝牙模式变量初始化
   @param    无
   @return   无
   @note
*/
/*----------------------------------------------------------------------------*/
void bt_var_init()
{
    memset((u8 *)&bt_user_priv_var, 0, sizeof(BT_USER_PRIV_VAR));
}


/*----------------------------------------------------------------------------*/
/**@brief    蓝牙模式协议栈回调函数
   @param    无
   @return   无
   @note
*/
/*----------------------------------------------------------------------------*/
void bredr_handle_register()
{
#if TRANS_DATA_EN
    spp_data_deal_handle_register(user_spp_data_handler);
#else
    spp_data_deal_handle_register(spp_data_handler);
#endif

#if BT_SUPPORT_MUSIC_VOL_SYNC
    ///蓝牙音乐和通话音量同步
    music_vol_change_handle_register(bt_set_music_device_volume, phone_get_device_vol);
#endif

#if BT_SUPPORT_DISPLAY_BAT
    ///电量显示获取电量的接口
    get_battery_value_register(bt_get_battery_value);
#endif

    ///被测试盒链接上进入快速测试回调
    bt_fast_test_handle_register(bt_fast_test_api);

    ///样机进入dut被测试仪器链接上回调
    bt_dut_test_handle_register(bt_dut_api);

    ///获取远端设备蓝牙名字回调
    read_remote_name_handle_register(bt_read_remote_name);

    ////获取歌曲信息回调
    /* bt_music_info_handle_register(user_get_bt_music_info); */

#if TCFG_USER_EMITTER_ENABLE
    ////发射器设置回调等
    void bt_emitter_init() ;
    bt_emitter_init();
    extern u8 emitter_search_result(char *name, u8 name_len, u8 * addr, u32 dev_class, char rssi);
    inquiry_result_handle_register(emitter_search_result);
    extern void emitter_or_receiver_switch(u8 flag);
    emitter_or_receiver_switch(BT_EMITTER_EN);
#endif
}


/*----------------------------------------------------------------------------*/
/**@brief    蓝牙模式协议栈功能配置
   @param    无
   @return   无
   @note
*/
/*----------------------------------------------------------------------------*/
void bt_function_select_init()
{
    ////设置协议栈支持设备数
    __set_user_ctrl_conn_num(TCFG_BD_NUM);
    ////msbc功能使能
    __set_support_msbc_flag(1);
#if TCFG_BT_SUPPORT_AAC
    ////AAC功能使能
    __set_support_aac_flag(1);
#else
    __set_support_aac_flag(0);
#endif

#if BT_SUPPORT_DISPLAY_BAT
    ////设置更新电池电量的时间间隔
    __bt_set_update_battery_time(60);
#else
    __bt_set_update_battery_time(0);
#endif

    ////回连搜索时间长度设置,可使用该函数注册使用，ms单位,u16
    __set_page_timeout_value(8000);

    ////回连时超时参数设置。ms单位。做主机有效
    __set_super_timeout_value(8000);

#if (TCFG_BD_NUM == 2)
    ////设置开机回链的设备个数
    __set_auto_conn_device_num(2);
#endif

#if (TCFG_SPI_LCD_ENABLE)
#if TCFG_USER_EMITTER_ENABLE//带有屏幕的方案根据UI选项连接
    ////设置开机回链的设备个数
    __set_auto_conn_device_num(10);//彩屏方案支持10个设备的连接
#endif
#endif

#if BT_SUPPORT_MUSIC_VOL_SYNC
    ////设置音乐音量同步的表
    vol_sys_tab_init();
#endif

    ////设置蓝牙是否跑后台
    __set_user_background_goback(BACKGROUND_GOBACK); // 后台链接是否跳回蓝牙 1:跳回

    ////设置蓝牙加密的level
    //io_capabilities ; /*0: Display only 1: Display YesNo 2: KeyboardOnly 3: NoInputNoOutput*/
    //authentication_requirements: 0:not protect  1 :protect
    __set_simple_pair_param(3, 0, 2);

#if (AUDIO_OUTPUT_WAY == AUDIO_OUTPUT_WAY_FM)
    bt_set_ldos(0);
#endif

#if (USER_SUPPORT_PROFILE_PBAP==1)
    ////设置蓝牙设备类型
    __change_hci_class_type(BD_CLASS_CAR_AUDIO);
#endif

#if (TCFG_BT_SNIFF_ENABLE == 0)
    void lmp_set_sniff_disable(void);
    lmp_set_sniff_disable();
#endif


    /*
                TX     RX
       AI800x   PA13   PA12
       AC692x   PA13   PA12
       AC693x   PA8    PA9
       AC695x   PA9    PA10
       AC696x   PA9    PA10
       AC694x   PB1    PB2
       AC697x   PC2    PC3
       AC631x   PA7    PA8

    */
    ////设置蓝牙接收状态io输出，可以外接pa
    /* bt_set_rxtx_status_enable(1); */

#if TCFG_USER_BLE_ENABLE
    {
        u8 tmp_ble_addr[6];
#if (TCFG_BLE_DEMO_SELECT == DEF_BLE_DEMO_ADV)
        /* bt_set_tx_power(9);//ble txpwer level:0~9 */
        memcpy(tmp_ble_addr, (void *)bt_get_mac_addr(), 6);
#else
        lib_make_ble_address(tmp_ble_addr, (void *)bt_get_mac_addr());
#endif //
        le_controller_set_mac((void *)tmp_ble_addr);
        printf("\n-----edr + ble 's address-----");
        printf_buf((void *)bt_get_mac_addr(), 6);
        printf_buf((void *)tmp_ble_addr, 6);
    }
#endif // TCFG_USER_BLE_ENABLE

#if USER_BT_VBAT_DISPLAY
    __set_disable_sco_flag(1);
#endif

}


/*----------------------------------------------------------------------------*/
/**@brief    蓝牙模式协议栈对应状态处理函数
   @param    bt:事件
   @return
   @note     蓝牙初始化完成、链接、通话播歌等状态
*/
/*----------------------------------------------------------------------------*/
static int bt_connction_status_event_handler(struct bt_event *bt)
{

    log_debug("-----------------------bt_connction_status_event_handler %d", bt->event);

    if (bt_status_event_filter(bt) == false) {
        return false;
    }

    switch (bt->event) {
    case BT_STATUS_EXIT_OK:
        log_info("BT_STATUS_EXIT_OK\n");
        break;
    case BT_STATUS_INIT_OK:
        log_info("BT_STATUS_INIT_OK\n");
        bt_status_init_ok(bt);
        break;
    case BT_STATUS_START_CONNECTED:
        log_info(" BT_STATUS_START_CONNECTED\n");
        break;
    case BT_STATUS_ENCRY_COMPLETE:
        log_info(" BT_STATUS_ENCRY_COMPLETE\n");
        break;
    case BT_STATUS_SECOND_CONNECTED:
        log_info(" BT_STATUS_SECOND_CONNECTED\n");
        clear_current_poweron_memory_search_index(0);
    case BT_STATUS_FIRST_CONNECTED:
        log_info("BT_STATUS_CONNECTED\n");
        bt_status_connect(bt);
        break;
    case BT_STATUS_FIRST_DISCONNECT:
    case BT_STATUS_SECOND_DISCONNECT:
        log_info(" BT_STATUS_SECOND_DISCONNECT\n");
        bt_status_disconnect(bt);
        break;
    case BT_STATUS_PHONE_INCOME:
        log_info("BT_STATUS_PHONE_INCOME\n");
        bt_status_phone_income(bt);
        break;
    case BT_STATUS_PHONE_OUT:
        log_info("BT_STATUS_PHONE_OUT\n");
        bt_status_phone_out(bt);
        break;
    case BT_STATUS_PHONE_ACTIVE:
        log_info("BT_STATUS_PHONE_ACTIVE\n");
        bt_status_phone_active(bt);
        break;
    case BT_STATUS_PHONE_HANGUP:
        log_info(" BT_STATUS_PHONE_HANGUP\n");
        bt_status_phone_hangup(bt);
        break;
    case BT_STATUS_PHONE_NUMBER:
        log_info("BT_STATUS_PHONE_NUMBER\n");
        bt_status_phone_number(bt);
        break;
    case BT_STATUS_INBAND_RINGTONE:
        log_info("BT_STATUS_INBAND_RINGTONE\n");
        bt_status_inband_ringtone(bt);
        break;
    case BT_STATUS_BEGIN_AUTO_CON:
        log_info("BT_STATUS_BEGIN_AUTO_CON\n");
        break;
    case BT_STATUS_A2DP_MEDIA_START:
        log_info(" BT_STATUS_A2DP_MEDIA_START\n");
        bt_status_a2dp_media_start(bt);
        break;
    case BT_STATUS_A2DP_MEDIA_STOP:
        log_info(" BT_STATUS_A2DP_MEDIA_STOP");
        bt_status_a2dp_media_stop(bt);
        break;
    case BT_STATUS_SCO_STATUS_CHANGE:
        log_info(" BT_STATUS_SCO_STATUS_CHANGE");
        bt_status_sco_change(bt);
        break;
    case BT_STATUS_CALL_VOL_CHANGE:
        log_info(" BT_STATUS_CALL_VOL_CHANGE ");
        bt_status_call_vol_change(bt);
        break;
    case BT_STATUS_SNIFF_STATE_UPDATE:
        log_info(" BT_STATUS_SNIFF_STATE_UPDATE \n");    //0退出SNIFF
        bt_status_sniff_state_update(bt);
        break;
    case BT_STATUS_LAST_CALL_TYPE_CHANGE:
        log_info("BT_STATUS_LAST_CALL_TYPE_CHANGE\n");
        bt_status_last_call_type_change(bt);
        break;
    case BT_STATUS_CONN_A2DP_CH:
        bt_status_conn_a2dp_ch(bt);
    case BT_STATUS_CONN_HFP_CH:
        bt_status_conn_hfp_ch(bt);
        break;
    case BT_STATUS_PHONE_MANUFACTURER:
        log_info("BT_STATUS_PHONE_MANUFACTURER\n");
        bt_status_phone_menufactuer(bt);
        break;
    case BT_STATUS_VOICE_RECOGNITION:
        log_info(" BT_STATUS_VOICE_RECOGNITION \n");
        bt_status_voice_recognition(bt);
        break;
    case BT_STATUS_AVRCP_INCOME_OPID:
        log_info("  BT_STATUS_AVRCP_INCOME_OPID \n");
        bt_status_avrcp_income_opid(bt);
        break;
    case  BT_STATUS_RECONN_OR_CONN:
        log_info("  BT_STATUS_RECONN_OR_CONN \n");
        break;
    default:
        log_info(" BT STATUS DEFAULT\n");
        break;
    }
    return 0;
}







/*----------------------------------------------------------------------------*/
/**@brief    蓝牙模式协议栈事件
   @param    bt:事件
   @return
   @note     蓝牙底层事件,通过app层处理
*/
/*----------------------------------------------------------------------------*/
static int bt_hci_event_handler(struct bt_event *bt)
{
    //对应原来的蓝牙连接上断开处理函数  ,bt->value=reason
    log_debug("------------------------bt_hci_event_handler reason %x %x", bt->event, bt->value);

    if (bt_hci_event_filter(bt) == 0) {
        return 0;
    }

    switch (bt->event) {
    case HCI_EVENT_INQUIRY_COMPLETE:
        log_info(" HCI_EVENT_INQUIRY_COMPLETE \n");
        bt_hci_event_inquiry(bt);
        break;
    case HCI_EVENT_IO_CAPABILITY_REQUEST:
        log_info(" HCI_EVENT_IO_CAPABILITY_REQUEST \n");
        clock_add_set(BT_CONN_CLK);
        break;
    case HCI_EVENT_USER_CONFIRMATION_REQUEST:
        log_info(" HCI_EVENT_USER_CONFIRMATION_REQUEST \n");
        ///<可通过按键来确认是否配对 1：配对   0：取消
        bt_send_pair(1);
        clock_remove_set(BT_CONN_CLK);
        break;
    case HCI_EVENT_USER_PASSKEY_REQUEST:
        log_info(" HCI_EVENT_USER_PASSKEY_REQUEST \n");
        ///<可以开始输入6位passkey
        break;
    case HCI_EVENT_USER_PRESSKEY_NOTIFICATION:
        log_info(" HCI_EVENT_USER_PRESSKEY_NOTIFICATION %x\n", bt->value);
        ///<可用于显示输入passkey位置 value 0:start  1:enrer  2:earse   3:clear  4:complete
        break;
    case HCI_EVENT_PIN_CODE_REQUEST :
        log_info("HCI_EVENT_PIN_CODE_REQUEST  \n");
        bt_send_pair(1);
        break;
    case HCI_EVENT_VENDOR_NO_RECONN_ADDR :
        log_info("HCI_EVENT_VENDOR_NO_RECONN_ADDR \n");
        bt_hci_event_disconnect(bt) ;
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE :
        log_info("HCI_EVENT_DISCONNECTION_COMPLETE \n");
        bt_hci_event_disconnect(bt) ;
        clock_remove_set(BT_CONN_CLK);
        break;
    case BTSTACK_EVENT_HCI_CONNECTIONS_DELETE:
    case HCI_EVENT_CONNECTION_COMPLETE:
        log_info(" HCI_EVENT_CONNECTION_COMPLETE \n");
        switch (bt->value) {
        case ERROR_CODE_SUCCESS :
            log_info("ERROR_CODE_SUCCESS  \n");
            bt_hci_event_connection(bt);
            break;
        case ERROR_CODE_PIN_OR_KEY_MISSING:
            log_info(" ERROR_CODE_PIN_OR_KEY_MISSING \n");
            bt_hci_event_linkkey_missing(bt);
        case ERROR_CODE_SYNCHRONOUS_CONNECTION_LIMIT_TO_A_DEVICE_EXCEEDED :
        case ERROR_CODE_CONNECTION_REJECTED_DUE_TO_LIMITED_RESOURCES:
        case ERROR_CODE_CONNECTION_REJECTED_DUE_TO_UNACCEPTABLE_BD_ADDR:
        case ERROR_CODE_CONNECTION_ACCEPT_TIMEOUT_EXCEEDED  :
        case ERROR_CODE_REMOTE_USER_TERMINATED_CONNECTION   :
        case ERROR_CODE_CONNECTION_TERMINATED_BY_LOCAL_HOST :
        case ERROR_CODE_AUTHENTICATION_FAILURE :
        case CUSTOM_BB_AUTO_CANCEL_PAGE:
            bt_hci_event_disconnect(bt) ;
            break;
        case ERROR_CODE_PAGE_TIMEOUT:
            log_info(" ERROR_CODE_PAGE_TIMEOUT \n");
            bt_hci_event_page_timeout(bt);
            break;
        case ERROR_CODE_CONNECTION_TIMEOUT:
            log_info(" ERROR_CODE_CONNECTION_TIMEOUT \n");
            bt_hci_event_connection_timeout(bt);
            break;
        case ERROR_CODE_ACL_CONNECTION_ALREADY_EXISTS  :
            log_info("ERROR_CODE_ACL_CONNECTION_ALREADY_EXISTS   \n");
            bt_hci_event_connection_exist(bt);
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return 0;
}



/*----------------------------------------------------------------------------*/
/**@brief    蓝牙后台事件过滤处理
   @param    event: 事件
   @return   0:不需要切换模式， 1:需要切换模式，2:通话导致需要切换
   @note
*/
/*----------------------------------------------------------------------------*/
int bt_background_event_handler_filter(struct sys_event *event)
{
    u8 ret = 0;
#if TCFG_BLUETOOTH_BACK_MODE
    if ((u32)event->arg == SYS_BT_EVENT_TYPE_CON_STATUS) {
        log_info("bt con event: %d \n", event->u.bt.event);
        switch (event->u.bt.event) {
        // 需要切换蓝牙的命令
        case BT_STATUS_A2DP_MEDIA_START:
            if (__this->sbc_packet_step != 0) {
                /* log_info("sbc_packet_step : %d \n", __this->sbc_packet_step); */
                break;
            }
        case BT_STATUS_FIRST_DISCONNECT:
        case BT_STATUS_SECOND_DISCONNECT:
#if (AUDIO_OUTPUT_WAY == AUDIO_OUTPUT_WAY_BT)
            ret = 1;
#else

#if BACKGROUND_GOBACK
            /* ret = 1; */
#else
            /* if (tws_api_get_role() == TWS_ROLE_MASTER) {
                bt_tws_api_push_cmd(SYNC_CMD_LED_PHONE_DISCONN_STATUS, 400);
            } */
#endif
#endif
            break;
        case BT_STATUS_SECOND_CONNECTED:
        case BT_STATUS_FIRST_CONNECTED:
#if BACKGROUND_GOBACK
            ret = 1;
#else

#if TCFG_USER_TWS_ENABLE
            /* bt_tws_phone_connected(); */
#endif
#endif
            break;

        case BT_STATUS_START_CONNECTED:
#if BACKGROUND_GOBACK
            ret = 1;
#endif
            break;

        case  BT_STATUS_ENCRY_COMPLETE:
            break;
        case BT_STATUS_SCO_STATUS_CHANGE:
#if BACKGROUND_GOBACK
            ret = 1;
#endif
            break;
        case BT_STATUS_VOICE_RECOGNITION:
        case BT_STATUS_PHONE_INCOME:
        case BT_STATUS_PHONE_NUMBER:
        /* case BT_STATUS_PHONE_MANUFACTURER: */
        case BT_STATUS_PHONE_OUT:
        case BT_STATUS_PHONE_ACTIVE:
            /* case BT_STATUS_PHONE_HANGUP: */
            ret = 2;
            break;
        // 不需要处理的命令
        case BT_STATUS_A2DP_MEDIA_STOP:
            bt_drop_a2dp_frame_stop();
        case BT_STATUS_CALL_VOL_CHANGE:
            break;
        // 按原方式处理的命令
        default:
            bt_connction_status_event_handler(&event->u.bt);
            break;
        }
    } else if ((u32)event->arg == SYS_BT_EVENT_TYPE_HCI_STATUS) {
        /* log_info("bt hci event: %d \n", event->u.bt.event); */
        switch (event->u.bt.event) {
        case HCI_EVENT_IO_CAPABILITY_REQUEST:
            clock_add_set(BT_CONN_CLK);
#if BACKGROUND_GOBACK
            ret = 1;
#endif
            break;
        default:
            bt_hci_event_handler(&event->u.bt);
            break;
        }
    }
#if TCFG_USER_TWS_ENABLE
    else if (((u32)event->arg == SYS_BT_EVENT_FROM_TWS)) {
        /* log_info("bt tws event: %d \n", event->u.bt.event); */
        switch (event->u.bt.event) {
        /* case TWS_EVENT_CONNECTED: */
        /* ret = 1; */
        /* break; */
        default:
            bt_tws_connction_status_event_handler(&event->u.bt);
#if (TCFG_DEC2TWS_ENABLE)
            localtws_bt_event_deal(&event->u.bt);
#endif
            break;
        }
    }
#endif
    else if ((u32)event->arg == SYS_EVENT_FROM_CTRLER) {
        switch (event->u.bt.event) {
        case  BTCTRLER_EVENT_RESUME_REQ:
            /*log_info("-------   BTCTRLER_EVENT_RESUME_REQ \n"); */
            ret = 2;
            break;
        }
    }
#endif
    return ret;
}

/*----------------------------------------------------------------------------*/
/**@brief    蓝牙后台事件处理
   @param    event: 事件
   @return   无
   @note
*/
/*----------------------------------------------------------------------------*/
int bt_background_event_handler(struct sys_event *event)
{
    int ret = bt_background_event_handler_filter(event);
    if (ret) {
        if (false == app_check_curr_task(APP_BT_TASK)) {
            __this->cmd_flag = 1;
            if (ret == 2) {
                __this->call_flag = 1;
            } else {
                __this->call_flag = 0;
            }
            app_task_switch_to(APP_BT_TASK);
            sys_event_notify(event);
        }
    }
    return 0;
}

/*----------------------------------------------------------------------------*/
/**@brief    蓝牙模式按键响应处理
   @param    event: 按键
   @return   1:事件处理  0:事件无处理
   @note
*/
/*----------------------------------------------------------------------------*/
int bt_key_event_handler(struct sys_event *event)
{
    int ret = true;
    struct key_event *key = &event->u.key;

    if (bt_key_event_filter_before() == false) {
        return false;
    }

    int key_event = event->u.key.event;
    int key_value = event->u.key.value;

    #if USER_IR_TWS_KEY_FILTER_EN
    //过滤对箱都有接收到遥控器按键值 情况
    extern u32 timer_get_ms(void);
    static u16 key_old_key =0xfff;
    static u32 key_old_time = 0;
    u32 tp_time = timer_get_ms();
    if(KEY_DRIVER_TYPE_IR == key->type){
        printf(">>>> key old time %d %d %d %d\n",(tp_time - key_old_time),(KEY_EVENT_FROM_TWS == (u32)event->arg),key_old_key,key_event);
        if((KEY_EVENT_FROM_TWS == (u32)event->arg)){
            printf("is tws key msg key:%d old key:%d\n",key_event,key_old_key);
            if((tp_time - key_old_time)<100 /*&& key_event==key_old_key */ ){
                key_old_time =  tp_time;
                key_old_key = key_event;
                printf("time min return key type %d\n",key->type);
                return ret;
            }

            key_old_key = key_event;
        }
        key_old_time =  tp_time;
    }

    #endif

    log_debug("bt key_event:%d %d %d %d\n", key_event, key->value, key->event, key->init);

    if (bt_key_event_filter_after(key_event) == true) {
        return true;
    }

    user_message_filtering(key_event);

    switch (key_event) {

    case  KEY_MUSIC_PP:
        log_info("   KEY_MUSIC_PP  \n");
        bt_key_music_pp();
        break;
    case  KEY_MUSIC_PREV:
        log_info("    KEY_MUSIC_PREV \n");
        bt_key_music_prev();
        break;
    case  KEY_MUSIC_NEXT:
        log_info("    KEY_MUSIC_NEXT \n");
        bt_key_music_next();
        break;
    case  KEY_VOL_UP:
        log_info("    KEY_VOL_UP \n");
        bt_key_vol_up();
        break;
    case  KEY_VOL_DOWN:
        log_info("    KEY_VOL_DOWN \n");
        bt_key_vol_down();
        break;
    case  KEY_CALL_LAST_NO:
        log_info("    KEY_CALL_LAST_NO \n");
        bt_key_call_last_on();
        break;
    case  KEY_CALL_HANG_UP:
        log_info("    KEY_CALL_HANG_UP \n");
        bt_key_call_hand_up();
        break;
    case  KEY_CALL_ANSWER:
        log_info("    KEY_CALL_ANSWER \n");
        bt_key_call_answer();
        break;
    case  KEY_OPEN_SIRI:
        log_info("    KEY_OPEN_SIRI \n");
        bt_key_call_siri();
        break;
    case  KEY_HID_CONTROL:
        log_info("    KEY_HID_CONTROL \n");
        bt_key_hid_control();
        break;
    case KEY_THIRD_CLICK:
        log_info("    KEY_THIRD_CLICK \n");
        bt_key_third_click(event);
        break;
    case KEY_LOW_LANTECY:
        log_info("    KEY_LOW_LANTECY \n");
        bt_key_low_lantecy();
        break;
    case  KEY_NULL:
        log_info("    KEY_NULL \n");
        ret = false;
#if TCFG_USER_TWS_ENABLE
        if ((u32)event->arg == KEY_EVENT_FROM_TWS) {
            break;
        }
#endif
        break;

    case KEY_HID_MODE_SWITCH:
        __this->hid_mode = !__this->hid_mode;
        printf("KEY_HID_TAKE_PICTURE, %d\n", __this->hid_mode);
        user_change_profile_mode(__this->hid_mode);///HID设备名称， 默认蓝牙名称追加_HID
        break;
    case KEY_HID_TAKE_PICTURE:
        if (__this->hid_mode) {
            printf("KEY_HID_TAKE_PICTURE\n");
            user_send_cmd_prepare(USER_CTRL_HID_BOTH, 0, NULL);
        }
        break;

#if TCFG_USER_TWS_ENABLE
    case  KEY_EQ_MODE:
#if(TCFG_EQ_ENABLE == 1)
    if (tws_api_get_tws_state() & TWS_STA_SIBLING_CONNECTED) {
        if (tws_api_get_role() == TWS_ROLE_MASTER) {
            u8 ret = user_eq_mode_sw(EQ_MODE_NEXT);//eq_mode_sw();
            user_bt_tws_sync_msg_send(USER_TWS_SYNC_EQ_MODE,ret);
        }
        ret = true;
    }

#endif
    break;

    case KEY_CHANGE_MODE:
    if((u32)event->arg == KEY_EVENT_FROM_TWS){
        ret = true;
    }else{
        ret = false;
    }
    break;
#if (TCFG_DEC2TWS_ENABLE)
        ret = false;
#else
        if (tws_api_get_tws_state() & TWS_STA_SIBLING_CONNECTED) {
            if (tws_api_get_role() == TWS_ROLE_MASTER) {
                bt_tws_api_push_cmd(SYNC_CMD_MODE_CHANGE, 400);
            }
        } else {
            ret = false;
            break;
        }

#endif
        break;
#endif

    default:
        ret = false;
        break;
    }
#if (SMART_BOX_EN)
    extern int smartbox_key_event_deal(u8 key_event, int ret);
    ret = smartbox_key_event_deal(key_event, ret);
#endif
    return ret;
}


/*----------------------------------------------------------------------------*/
/**@brief    蓝牙模式协议栈状态事件、hci事件、对箱事件
   @param    event: 事件
   @return   无
   @note
*/
/*----------------------------------------------------------------------------*/
int bt_sys_event_office(struct sys_event *event)
{
    if ((u32)event->arg == SYS_BT_EVENT_TYPE_CON_STATUS) {
        bt_connction_status_event_handler(&event->u.bt);
    } else if ((u32)event->arg == SYS_BT_EVENT_TYPE_HCI_STATUS) {
        bt_hci_event_handler(&event->u.bt);
    } else if ((u32)event->arg == SYS_BT_EVENT_FORM_SELF) {
        bt_reverb_status_change(&event->u.bt);
    }
#if TCFG_USER_TWS_ENABLE
    else if (((u32)event->arg == SYS_BT_EVENT_FROM_TWS)) {
        bt_tws_connction_status_event_handler(&event->u.bt);
    }
#endif
    else if (((u32)event->arg == SYS_BT_EVENT_FROM_KEY)) {
        switch (event->u.key.event) {
        case KEY_CHANGE_MODE:
            app_task_switch_next();
            break;
        }
    }
    return false;
}


/*----------------------------------------------------------------------------*/
/**@brief    蓝牙模式事件消息处理
   @param    event: 事件
   @return   1:处理完成,消息不需要进入common处理  0:common defult处理
   @note
*/
/*----------------------------------------------------------------------------*/
static int bt_sys_event_handler(struct sys_event *event)
{
    int ret = false;
    switch (event->type) {
    case SYS_KEY_EVENT:
        ret = bt_key_event_handler(event);
        break;
    case SYS_BT_EVENT:
        ret = bt_sys_event_office(event);
        break;
    case SYS_DEVICE_EVENT:
        break;
    default:
        return false;
    }
    // SYS_EVENT_HANDLER_SPECIFIC(event);
    return ret;
}

//*----------------------------------------------------------------------------*/
/**@brief    BT 模式提示音播放结束处理
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void  bt_tone_play_end_callback(void *priv, int flag)
{
    u32 index = (u32)priv;

    if (APP_BT_TASK != app_get_curr_task()) {
        log_error("tone callback task out \n");
        return;
    }

    switch (index) {
    case IDEX_TONE_BT_MODE:
        ///提示音播放结束
        #if USER_BT_TONE_PLAY_GO_INIT
        puts(">>>>>>> bt statr\n");
        bt_task_start();
        #endif
        break;
    default:
        break;
    }
}

/*----------------------------------------------------------------------------*/
/**@brief    蓝牙模式
   @param
   @return   无
   @note
*/
/*----------------------------------------------------------------------------*/
void app_bt_task()
{
    __this->wait_exit = 0;//从其他模式进bt 会初始化两次

    int res;
    int msg[32];
    ui_update_status(STATUS_EXIT_LOWPOWER);

    if (!__this->cmd_flag) { //蓝牙后台拉回蓝牙模式不播放提示音

        int err =  tone_play_with_callback_by_name(tone_table[IDEX_TONE_BT_MODE], 1, bt_tone_play_end_callback, (void *)IDEX_TONE_BT_MODE);
        #if USER_BT_TONE_PLAY_GO_INIT
        if (err) {
            puts(">>>>>>>>>>>>>>>>>   bt ton play  err\n");
            bt_task_start();
        }
        #else
        puts(">>>>>> no no  no   USER_BT_TONE_PLAY_GO_INIT\n");
        bt_task_start();
        // tone_play_by_path(tone_table[IDEX_TONE_BT_MODE], 1);
        #endif
    }else{
        bt_task_start();
    }

    while (1) {
        app_task_get_msg(msg, ARRAY_SIZE(msg), 1);

        switch (msg[0]) {
        case APP_MSG_SYS_EVENT:
            if (bt_sys_event_handler((struct sys_event *)(msg + 1)) == false) {
                app_default_event_deal((struct sys_event *)(&msg[1]));
            }
            break;
        case APP_MSG_USER:
            printf(">>> APP_MSG_USER\n");
            printf("0:%d 1:%d 2:%d\n",msg[0],msg[1],msg[2]);
            break;
        default:
            break;
        }

        if (app_task_exitting()) {
            bt_task_close();
            __this->wait_exit = 1;
        }

        if (__this->wait_exit) { //开始退出
            if (!__this->exiting) { ///等待蓝牙退出处理完成
                return;
            }
        }
    }
}

u8 bt_app_exit_check()
{
    return	bt_app_switch_exit_check();
}

#else

void app_bt_task()
{
    __this->wait_exit = 0;
    ui_update_status(STATUS_EXIT_LOWPOWER);

    puts(">>>> Entering BT Task");

    bt_task_start();  // Start Bluetooth task stack

    while (1) {
        int msg[32];
        app_task_get_msg(msg, ARRAY_SIZE(msg), 1);

        switch (msg[0]) {
        case APP_MSG_SYS_EVENT:
            if (bt_sys_event_handler((struct sys_event *)(msg + 1)) == false) {
                app_default_event_deal((struct sys_event *)(&msg[1]));
            }
            break;
        default:
            break;
        }

        if (app_task_exitting()) {
            bt_task_close();
            __this->wait_exit = 1;
        }

        if (__this->wait_exit) {
            if (!__this->exiting) {
                return;
            }
        }
    }
}


u8 bt_app_exit_check()
{
    return 1;
}

int bt_background_event_handler_filter(struct sys_event *event)
{
    return 0;
}


u8 get_call_status()
{
    return BT_CALL_HANGUP;
}

u32 bt_tws_master_slot_clk(void)
{
    return 0;
}

#endif
