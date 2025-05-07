
/*************************************************************
   此文件函数主要是record模式按键处理和事件处理

	void app_record_task()
   record模式主函数

	static int record_sys_event_handler(struct sys_event *event)
   record模式系统事件所有处理入口

	static void record_task_close(void)
	record模式退出

**************************************************************/
#include "system/app_core.h"
#include "system/includes.h"
#include "server/server_core.h"
#include "media/includes.h"
#include "app_config.h"
#include "app_task.h"
#include "tone_player.h"
#include "asm/charge.h"
#include "app_charge.h"
#include "app_main.h"
#include "app_online_cfg.h"
#include "app_power_manage.h"
#include "gSensor/gSensor_manage.h"
#include "ui_manage.h"
#include "vm.h"
#include "app_chargestore.h"
#include "key_event_deal.h"
#include "asm/pwm_led.h"
#include "user_cfg.h"
#include "record/record.h"
#include "audio_enc.h"
#include "audio_dec.h"
#include "ui/ui_api.h"
#include "fm_emitter/fm_emitter_manage.h"
#include "clock_cfg.h"
#include "dev_manager.h"
#include "user_fun_cfg.h"

#if TCFG_APP_RECORD_EN

#define LOG_TAG_CONST       APP_RECORD
#define LOG_TAG             "[APP_RECORD]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"


//*----------------------------------------------------------------------------*/
/**@brief    mic录音启动
   @param    无
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void record_mic_start(void)
{
    struct record_file_fmt fmt = {0};
    /* char logo[] = {"sd0"}; */		//可以指定设备
    char folder[] = {REC_FOLDER_NAME};         //录音文件夹名称
    char filename[] = {"****"};     //录音文件名，不需要加后缀，录音接口会根据编码格式添加后缀

#if (TCFG_NOR_REC)
    char logo[] = {"rec_nor"};		//外挂flash录音
#elif (FLASH_INSIDE_REC_ENABLE)
    char logo[] = {"rec_sdfile"};		//内置flash录音
#else
    char *logo = dev_manager_get_phy_logo(dev_manager_find_active(0));//普通设备录音，获取最后活动设备
#endif

    fmt.dev = logo;
    fmt.folder = folder;
    fmt.filename = filename;
    fmt.coding_type = AUDIO_CODING_MP3; //编码格式： AUDIO_CODING_WAV, AUDIO_CODING_MP3
    fmt.channel = 1;                    //声道数： 1：单声道 2：双声道
    fmt.sample_rate = 44100;            //采样率：8000，16000，32000，44100
    fmt.cut_head_time = 300;            //录音文件去头时间,单位ms
    fmt.cut_tail_time = 300;            //录音文件去尾时间,单位ms
    fmt.limit_size = 3000;              //录音文件大小最小限制， 单位byte
    fmt.gain = 8;
    fmt.source = ENCODE_SOURCE_MIC;     //录音输入源
    fmt.err_callback = NULL;
    int ret = recorder_encode_start(&fmt);
    if (ret) {
        log_e("record_mic_start fail !!, dev = %s\n", logo);
    } else {
        log_i("record_mic_start succ !!, dev = %s\n", logo);
    }
}
//*----------------------------------------------------------------------------*/
/**@brief    mic 录音停止
   @param    无
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void record_mic_stop(void)
{
    recorder_encode_stop();
}
//*----------------------------------------------------------------------------*/
/**@brief    mic 录音与回放切换
   @param    无
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void record_key_pp()
{
    if (recorder_is_encoding()) {
        log_i("mic record stop && replay\n");
        record_mic_stop();
        record_file_play();
    } else {
        record_file_close();
        record_mic_start();
        log_i("mic record start\n");
    }
}

//*----------------------------------------------------------------------------*/
/**@brief    录音模式提示音结束处理
   @param    无
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void  record_tone_play_end_callback(void *priv, int flag)
{
    u32 index = (u32)priv;
    if (APP_RECORD_TASK != app_get_curr_task()) {
        log_e("tone callback task out \n");
        return;
    }
    switch (index == IDEX_TONE_RECORD) {
    case IDEX_TONE_DI:
        // record_key_pp();
    case IDEX_TONE_RECORD:
        log_i("IDEX_TONE_RECORD end\n");
        break;
    }
}

//*----------------------------------------------------------------------------*/
/**@brief    record 按键消息入口
  @param    无
  @return   1、消息已经处理，不需要发送到common  0、消息发送到common处理
  @note
 */
/*----------------------------------------------------------------------------*/
static int record_key_event_opr(struct sys_event *event)
{
    int ret = false;
    int err = 0;

#if TCFG_APP_FM_EMITTER_EN
#if TCFG_UI_ENABLE
    if (ui_get_app_menu(GRT_CUR_MENU) == MENU_FM_SET_FRE) {
        return ret;
    }

#endif
#endif
    int key_event = event->u.key.event;
    int key_value = event->u.key.value;//

    log_i("key_event:%d \n", key_event);
    
    user_message_filtering(key_event);

    switch (key_event) {
    case USER_MSG_TO_BT_MODE:
{
        extern int user_app_goto_bt(void);
        user_app_goto_bt();
}
        break;
    case KEY_ENC_START:
        printf("  KEY_ENC_START \n");
        record_key_pp();
        return true;
    case KEY_CHANGE_MODE:
        puts(">>>> recorde to music");
        app_task_switch_to(APP_MUSIC_TASK);
        ret = true;
        break;
#if (TCFG_MIC_EFFECT_ENABLE)
    case KEY_REVERB_OPEN:
        log_i("record mode ignore mic_effect!!\n");
        ret = true;
        break;
#endif

    default:
        break;
    }

    return ret;
}
//*----------------------------------------------------------------------------*/
/**@brief    record 模式初始化
   @param    无
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void record_task_start(void)
{
    UI_SHOW_WINDOW(ID_WINDOW_REC);
    sys_key_event_enable();
    ui_update_status(STATUS_RECORD_MODE);

    clock_idle(REC_IDLE_CLOCK);
}


//*----------------------------------------------------------------------------*/
/**@brief    record 退出
   @param    无
   @return
   @note
*/
/*----------------------------------------------------------------------------*/

static void record_task_close()
{
    UI_HIDE_CURR_WINDOW();
    //停止播放模式提示音
    tone_play_stop_by_path(tone_table[IDEX_TONE_RECORD]);
    ///停止mic录音
    record_mic_stop();
    ///停止回放
    record_file_close();
}

//*----------------------------------------------------------------------------*/
/**@brief    record 模式活跃状态 所有消息入口
   @param    无
   @return   1、当前消息已经处理，不需要发送comomon
   			 0、当前消息不是linein处理的，发送到common统一处理
   @note
*/
/*----------------------------------------------------------------------------*/
static int record_sys_event_handler(struct sys_event *event)
{
    const char *logo = NULL;
    int err = 0;
    switch (event->type) {
    case SYS_KEY_EVENT:
        return record_key_event_opr(event);
    case SYS_DEVICE_EVENT:
        ///所有设备相关的事件不能返回true， 必须给留给公共处理的地方响应设备上下线
        switch ((u32)event->arg) {
        case DRIVER_EVENT_FROM_SD0:
        case DRIVER_EVENT_FROM_SD1:
        case DRIVER_EVENT_FROM_SD2:
            logo = (char *)event->u.dev.value;
        case DEVICE_EVENT_FROM_OTG:
            if ((u32)event->arg == DEVICE_EVENT_FROM_OTG) {
                logo = (char *)"udisk0";
            }

            if (event->u.dev.event == DEVICE_EVENT_IN) {
            } else if (event->u.dev.event == DEVICE_EVENT_OUT) {
                //判断是否是正在录音的设备掉线
                recorder_device_offline_check(logo);
            }
            break;//DEVICE_EVENT_FROM_USB_HOST
        }//switch((u32)event->arg)
        break;//SYS_DEVICE_EVENT
    default:
        break;;
    }//switch (event->type)

    return false;
}

//*----------------------------------------------------------------------------*/
/**@brief    record 在线检测  切换模式判断使用
   @param    无
   @return   1 设备在线 0 设备不在线
   @note
*/
/*----------------------------------------------------------------------------*/
int record_app_check(void)
{
    if(!user_record_status(0xff)){
        return false;
    }

#if (TCFG_MIC_EFFECT_ENABLE)
    if (mic_effect_get_status()) {
        log_i("mic_effect working!!\n");
        return false;
    }
#endif
    if (dev_manager_get_total(0)) {
        user_record_status(0);
        return true;
    }
    return false;
}

//*----------------------------------------------------------------------------*/
/**@brief    record 主任务
   @param    无
   @return   无
   @note
*/
/*----------------------------------------------------------------------------*/
void app_record_task()
{
    int res;
    int msg[32];
    printf(">>>>>> recode init\n");
    record_task_start();
    // int err =  tone_play_with_callback_by_name(tone_table[IDEX_TONE_RECORD], 1, record_tone_play_end_callback, (void *)IDEX_TONE_RECORD);
    // int err =  tone_play_with_callback_by_name(TONE_DI, 1, record_tone_play_end_callback, (void *)IDEX_TONE_DI);
    // if (err) {
    //     log_e(" tone play err!!\n");
    //     // record_key_pp();
    // }
    // record_key_pp();
    printf(">>>>>  record 0\n");
    sys_timeout_add(NULL,record_mic_start,1000);
    while (1) {
        app_task_get_msg(msg, ARRAY_SIZE(msg), 1);

        switch (msg[0]) {
        case APP_MSG_SYS_EVENT:
            if (record_sys_event_handler((struct sys_event *)(&msg[1])) == false) {
                app_default_event_deal((struct sys_event *)(&msg[1]));    //由common统一处理
            }
            break;
        default:
            break;
        }

        if (app_task_exitting()) {
            record_task_close();
            user_record_status(0);
            return;
        }
    }
}

#else

int record_app_check(void)
{
    return false;
}

void app_record_task()
{

}

#endif

