#include "system/includes.h"
#include "app_power_manage.h"
#include "app_main.h"
#include "app_config.h"
#include "app_action.h"
#include "asm/charge.h"
#include "ui_manage.h"
#include "tone_player.h"
#include "asm/adc_api.h"
#include "btstack/avctp_user.h"
#include "user_cfg.h"
#include "bt.h"
#include "asm/charge.h"
#include "user_fun_cfg.h"

#if TCFG_USER_TWS_ENABLE
#include "bt_tws.h"
#endif

#define LOG_TAG_CONST       APP_POWER
#define LOG_TAG             "[APP_POWER]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

enum {
    VBAT_NORMAL = 0,
    VBAT_WARNING,
    VBAT_LOWPOWER,
} VBAT_STATUS;

enum {
    VBAT_TIMER_2_MS = 0,
    VBAT_TIMER_10_S,
} VBAT_CHECK_TIME;

static int vbat_timer = 0;
static u8 vbat_check_idle = 1;
static u8 old_battery_level = 9;
static u16 bat_val = 0;
static volatile u8 cur_battery_level = 0;
static u16 battery_full_value = 0;
static u8 tws_sibling_bat_level = 0xff;
static u8 tws_sibling_bat_percent_level = 0xff;

void vbat_check(void *priv);
void sys_enter_soft_poweroff(void *priv);
void clr_wdt(void);
void power_event_to_user(u8 event);


u8 get_tws_sibling_bat_level(void)
{
#if TCFG_USER_TWS_ENABLE
    /* log_info("***********get_tws_sibling_bat_level: %2x", tws_sibling_bat_percent_level); */
    return tws_sibling_bat_level & 0x7f;
#endif
    return 0xff;
}

u8 get_tws_sibling_bat_persent(void)
{
#if TCFG_USER_TWS_ENABLE
    /* log_info("***********get_tws_sibling_bat_level: %2x", tws_sibling_bat_percent_level); */
    return tws_sibling_bat_percent_level;
#endif
    return 0xff;
}

void app_power_set_tws_sibling_bat_level(u8 vbat, u8 percent)
{
#if TCFG_USER_TWS_ENABLE
    tws_sibling_bat_level = vbat;
    tws_sibling_bat_percent_level = percent;
    /*
     ** 发出电量同步事件进行进一步处理
     **/
    power_event_to_user(POWER_EVENT_SYNC_TWS_VBAT_LEVEL);

    log_info("set_sibling_bat_level: %d, %d\n", vbat, percent);
#endif
}


static void set_tws_sibling_bat_level(void *_data, u16 len, bool rx)
{
    u8 *data = (u8 *)_data;

    if (rx) {
        app_power_set_tws_sibling_bat_level(data[0], data[1]);
    }
}

#if TCFG_USER_TWS_ENABLE
REGISTER_TWS_FUNC_STUB(vbat_sync_stub) = {
    .func_id = TWS_FUNC_ID_VBAT_SYNC,
    .func    = set_tws_sibling_bat_level,
};
#endif

void tws_sync_bat_level(void)
{
#if (TCFG_USER_TWS_ENABLE && BT_SUPPORT_DISPLAY_BAT)
    u8 battery_level = cur_battery_level;
#if CONFIG_DISPLAY_DETAIL_BAT
    u8 percent_level = get_vbat_percent();
#else
    u8 percent_level = get_self_battery_level() * 10 + 10;
#endif
    if (get_charge_online_flag()) {
        percent_level |= BIT(7);
    }

    u8 data[2];
    data[0] = battery_level;
    data[1] = percent_level;
    tws_api_send_data_to_sibling(data, 2, TWS_FUNC_ID_VBAT_SYNC);

    log_info("tws_sync_bat_level: %d,%d\n", battery_level, percent_level);
#endif
}
static u8 lowpower_timer;
void power_event_to_user(u8 event)
{
    struct sys_event e;
    e.type = SYS_DEVICE_EVENT;
    e.arg  = (void *)DEVICE_EVENT_FROM_POWER;
    e.u.dev.event = event;
    e.u.dev.value = 0;
    sys_event_notify(&e);
}

int app_power_event_handler(struct device_event *dev)
{
    int ret = false;

#if(TCFG_SYS_LVD_EN == 1)
    switch (dev->event) {
    case POWER_EVENT_POWER_NORMAL:
        ui_update_status(STATUS_EXIT_LOWPOWER);
        break;
    case POWER_EVENT_POWER_WARNING:
        puts("POWER_EVENT_POWER_WARNING power manage\n");
        ui_update_status(STATUS_LOWPOWER);        
        /* tone_play(TONE_LOW_POWER); */
        STATUS *p_tone = get_tone_config();
        tone_play_index(p_tone->lowpower, 0);
        break;
    case POWER_EVENT_POWER_LOW:
        printf(" POWER_EVENT_POWER_LOW");
#if TCFG_APP_BT_EN
#if (RCSP_ADV_EN)
        extern u8 adv_tws_both_in_charge_box(u8 type);
        adv_tws_both_in_charge_box(1);
#endif
        //低电关机需要关闭sdgp引脚供电设备
        user_power_off_class(1);
        
        soft_poweroff_mode(1);  ///强制关机
        sys_enter_soft_poweroff(NULL);
#else
        void app_entry_idle() ;
        app_entry_idle() ;
#endif
        break;
#if TCFG_APP_BT_EN
#if TCFG_USER_TWS_ENABLE
    case POWER_EVENT_SYNC_TWS_VBAT_LEVEL:
        if (tws_api_get_role() == TWS_ROLE_MASTER) {
            user_send_cmd_prepare(USER_CTRL_HFP_CMD_UPDATE_BATTARY, 0, NULL);
        }
        break;
#endif

    case POWER_EVENT_POWER_CHANGE:
        /* log_info("POWER_EVENT_POWER_CHANGE\n"); */
#if TCFG_USER_TWS_ENABLE
        if (tws_api_get_tws_state() & TWS_STA_SIBLING_CONNECTED) {
            if (tws_api_get_tws_state()&TWS_STA_ESCO_OPEN) {
                break;
            }
            tws_sync_bat_level();
        }
#endif
        user_send_cmd_prepare(USER_CTRL_HFP_CMD_UPDATE_BATTARY, 0, NULL);
#endif
        break;
    default:
        break;
    }
#endif

    return ret;
}


u16 user_get_vbat_level(u16 level){
    #define USER_VBAT_TABLE_SIZE    40

    static u16 save_table_old[USER_VBAT_TABLE_SIZE] = {0};
    static u8 i=0;
    static u32 sam = 0;
    u16 max = 0;
    u16 mix = 0;

    save_table_old[i++]=level;
    if(i++ >= USER_VBAT_TABLE_SIZE){
        i = 0;
        sam = 1;
    }else{
    //    sam += level;
    //    sam /= i;
    }

    for(int j = 0;j<USER_VBAT_TABLE_SIZE;j++){
        if(save_table_old[j] && sam){
            sam+=save_table_old[j];
            sam/=2;
        }
        
        if(save_table_old[j]>max){
            max = save_table_old[j];
        }

        if(save_table_old[j] < mix){
            mix = save_table_old[j];
        }
    }
    // if(i)sam/=i;
    // printf(">>>>>>>> vbat %d\n",sam);
    return max;
}


u16 get_vbat_level(void)
{
    u16 vbat_tp = 0;
    #if (defined(USER_VBAT_CHECK_EN) && USER_VBAT_CHECK_EN)
    vbat_tp = user_fun_get_vbat();
    #else
    vbat_tp = (adc_get_voltage(AD_CH_VBAT) * 4 / 10);
    #endif
    // vbat_tp = user_get_vbat_level(vbat_tp);
    // printf(">>>>>>>>>> vbat vol %04d\n",vbat_tp);
    //return 370;     //debug
    return vbat_tp;
}

__attribute__((weak)) u8 remap_calculate_vbat_percent(u16 bat_val)
{
    return 0;
}

u16 get_vbat_value(void)
{
    return bat_val;
}

u8 get_vbat_percent(void)
{
    u16 tmp_bat_val;
    u16 bat_val = get_vbat_level();
    if (battery_full_value == 0) {
#if TCFG_CHARGE_ENABLE
        battery_full_value = (get_charge_full_value() - 100) / 10; //防止部分电池充不了这么高电量，充满显示未满的情况
#else
        battery_full_value = 420;
#endif
    }

    if (bat_val <= app_var.poweroff_tone_v) {
        return 0;
    }

    tmp_bat_val = remap_calculate_vbat_percent(bat_val);
    if (!tmp_bat_val) {
        tmp_bat_val = ((u32)bat_val - app_var.poweroff_tone_v) * 100 / (battery_full_value - app_var.poweroff_tone_v);
        if (tmp_bat_val > 100) {
            tmp_bat_val = 100;
        }
    }
    return (u8)tmp_bat_val;
}

bool get_vbat_need_shutdown(void)
{
    if ((bat_val <= LOW_POWER_SHUTDOWN) || adc_check_vbat_lowpower()) {
        return TRUE;
    }
    return FALSE;
}

//将当前电量转换为1~9级发送给手机同步电量
u8  battery_value_to_phone_level(u16 bat_val)
{
    u8  battery_level = 0;
    u8 vbat_percent = get_vbat_percent();

    if (vbat_percent < 5) { //小于5%电量等级为0，显示10%
        return 0;
    }

    battery_level = (vbat_percent - 5) / 10;

    return battery_level;
}

//获取自身的电量
u8  get_self_battery_level(void)
{
    return cur_battery_level;
}

u8  get_cur_battery_level(void)
{
    u8 bat_lev = tws_sibling_bat_level & (~BIT(7));
#if TCFG_USER_TWS_ENABLE
    if (bat_lev == 0x7f) {
        return cur_battery_level;
    }

#if (CONFIG_DISPLAY_TWS_BAT_TYPE == CONFIG_DISPLAY_TWS_BAT_LOWER)
    return cur_battery_level < bat_lev ? cur_battery_level : bat_lev;
#elif (CONFIG_DISPLAY_TWS_BAT_TYPE == CONFIG_DISPLAY_TWS_BAT_HIGHER)
    return cur_battery_level < bat_lev ? bat_lev : cur_battery_level;
#elif (CONFIG_DISPLAY_TWS_BAT_TYPE == CONFIG_DISPLAY_TWS_BAT_LEFT)
    return tws_api_get_local_channel() == 'L' ? cur_battery_level : bat_lev;
#elif (CONFIG_DISPLAY_TWS_BAT_TYPE == CONFIG_DISPLAY_TWS_BAT_RIGHT)
    return tws_api_get_local_channel() == 'R' ? cur_battery_level : bat_lev;
#else
    return cur_battery_level;
#endif //END CONFIG_DISPLAY_TWS_BAT_TYPE

#else  //TCFG_USER_TWS_ENABLE == 0
    return cur_battery_level;
#endif
}

void vbat_check_init(void)
{
    if (vbat_timer == 0) {
        vbat_timer = sys_timer_add(NULL, vbat_check, 20);
        vbat_check_idle = 0;
    }
}

void vbat_timer_update(u32 msec)
{
    if (vbat_timer) {
        sys_timer_modify(vbat_timer, msec);
        /* sys_timer_del(vbat_timer); */
        /* vbat_timer = sys_timer_add(NULL, vbat_check, msec); */
    }
}

void vbat_timer_delete(void)
{
    if (vbat_timer) {
        sys_timer_del(vbat_timer);
        vbat_timer = 0;
        vbat_check_idle = 1;
    }
}

//降音量
extern u32 timer_get_sec(void);
void user_down_sys_vol_cnt(u8 vol){
    static u32 down_sys_vol_time = 0;
    u32 down_sys_vol_tp_time = timer_get_sec();

    if(0xff == vol){//更新时间
        down_sys_vol_time = down_sys_vol_tp_time;
        return ;
    }

    if((down_sys_vol_tp_time - down_sys_vol_time)>=5){
        down_sys_vol_time = down_sys_vol_tp_time;
        if(20==vol){
            user_dow_sys_vol_20();
        }else if(10==vol){
            user_dow_sys_vol_10();
        }
    }

    user_low_power_show(1);
    puts(">>>>>>>>  low power set 3\n");
}
static u8 cur_bat_st = VBAT_NORMAL;

void vbat_check(void *priv)
{
    static u8 cur_timer_period = VBAT_TIMER_2_MS;
    static u8 unit_cnt = 0;
    static u8 low_warn_cnt = 0;
    static u8 low_dow_sys_vol_cnt = 0;
    static u8 low_off_cnt = 0;
    static u8 low_voice_cnt = 0;
    static u8 low_power_cnt = 0;
    static u8 power_normal_cnt = 0;
    static u8 charge_ccvol_v_cnt = 0;
    static u8 charge_online_flag = 0;
    static u8 low_voice_first_flag = 1;//进入低电后先提醒一次
    static u8 tp_low_war_cnt = 0;

    u8 down_sys_vol_flag = 0;
    u8 detect_cnt = 60;

    if (cur_timer_period == VBAT_TIMER_10_S) {
        vbat_timer_update(50);
        cur_timer_period = VBAT_TIMER_2_MS;
        vbat_check_idle = 0;
    }

    if (!bat_val) {
        bat_val = user_get_vbat_level(get_vbat_level());//get_vbat_level();
    } else {
        bat_val = (user_get_vbat_level(get_vbat_level()) + bat_val) / 2;
    }

    cur_battery_level = battery_value_to_phone_level(bat_val);

    // printf("bv:%d, bl:%d , check_vbat:%d\n", bat_val, cur_battery_level, adc_check_vbat_lowpower());

    unit_cnt++;

    if(bat_val <= 360){
        low_dow_sys_vol_cnt++;
    }else{
        low_dow_sys_vol_cnt = 0;
        tp_low_war_cnt = 0;
        low_warn_cnt = 0;
        low_off_cnt = 0;
        user_low_power_show(0);
    }

    /* if (bat_val < LOW_POWER_OFF_VAL) { */
    if ((bat_val <= app_var.poweroff_tone_v) || adc_check_vbat_lowpower()) {
        low_off_cnt++;
    }
    /* if (bat_val < LOW_POWER_WARN_VAL) { */
    if (bat_val <= app_var.warning_tone_v) {
        low_warn_cnt++;
    }else{
        // puts(">>>>> max power vol\n");
        tp_low_war_cnt = 0;
        low_warn_cnt = 0;
        low_off_cnt = 0;
  
    }
#if TCFG_CHARGE_ENABLE
    if (bat_val >= CHARGE_CCVOL_V) {
        charge_ccvol_v_cnt++;
    }
#endif
    // printf("bv:%d, bl:%d , vol:%d\n", bat_val, cur_battery_level, app_audio_get_volume(APP_AUDIO_STATE_MUSIC));
    if(low_off_cnt || low_warn_cnt || tp_low_war_cnt || low_dow_sys_vol_cnt){
        printf("                    power 0ff %d  %d  %d %d %d \n",bat_val,tp_low_war_cnt,low_warn_cnt,low_off_cnt,app_audio_get_volume(APP_AUDIO_STATE_MUSIC));
    }    

    /* log_info("unit_cnt:%d\n", unit_cnt); */

    if (unit_cnt >= detect_cnt) {

        if (get_charge_online_flag() == 0) {
            if (low_off_cnt > (detect_cnt / 2)) { //低电关机
                low_power_cnt++;
                low_voice_cnt = 0;
                power_normal_cnt = 0;
                cur_bat_st = VBAT_LOWPOWER;
                if (low_power_cnt > 6) {
                    log_info("\n*******Low Power,enter softpoweroff******%d**\n",low_power_cnt);
                    low_power_cnt = 0;
                    if(vbat_timer){
                        sys_timer_del(vbat_timer);
                        vbat_timer = 0;
                    }
                    
                    if (lowpower_timer) {
                        sys_timer_del(lowpower_timer);
                        lowpower_timer = 0 ;
                    }
                    user_down_sys_vol_cnt(10);
                    power_event_to_user(POWER_EVENT_POWER_LOW);
                }
            } else if (low_warn_cnt > (detect_cnt / 2)) { //低电提醒
                low_voice_cnt ++;
                low_power_cnt = 0;
                power_normal_cnt = 0;
                cur_bat_st = VBAT_WARNING;
                if ((low_voice_first_flag && low_voice_cnt > 1) || //第一次进低电10s后报一次
                    (!low_voice_first_flag && low_voice_cnt >= 5)) {
                    low_voice_first_flag = 0;
                    low_voice_cnt = 0;
                    if (!lowpower_timer) {
                        log_info("\n**Low Power,Please Charge Soon!!!**\n");
                        // power_event_to_user(POWER_EVENT_POWER_WARNING);
                        // lowpower_timer = sys_timer_add((void *)POWER_EVENT_POWER_WARNING, (void (*)(void *))power_event_to_user, LOW_POWER_WARN_TIME);
                    }
                }
                
                user_down_sys_vol_cnt(10);

                //20s 播一次 5次之后关机
                
                static u32 tone_warn_time = 0;
                if((timer_get_sec()-tone_warn_time)>20){
                    tone_warn_time = timer_get_sec();

                    tp_low_war_cnt++;
                    printf(">>>>>>>>> power low cnt %d\n",tp_low_war_cnt);
                    if(tp_low_war_cnt>=5){
                        tp_low_war_cnt = 0;
                        printf(">>>>>>>>> power off low\n");
                        power_event_to_user(POWER_EVENT_POWER_LOW);
                        if(vbat_timer){
                            sys_timer_del(vbat_timer);
                            vbat_timer = 0;
                        }
                        
                    }else{
                        user_low_power_show(2);//低电闪烁
                        power_event_to_user(POWER_EVENT_POWER_WARNING);
                    }
                }
            }else if(low_dow_sys_vol_cnt> (detect_cnt / 2)){
                low_dow_sys_vol_cnt = 0;
                low_voice_cnt = 0;
                low_power_cnt = 0;

                user_down_sys_vol_cnt(20);

            } else {
                
                power_normal_cnt++;
                low_voice_cnt = 0;
                low_power_cnt = 0;
                low_dow_sys_vol_cnt = 0;
                if (power_normal_cnt > 2) {
                    if (cur_bat_st != VBAT_NORMAL) {
                        log_info("[Noraml power]\n");
                        cur_bat_st = VBAT_NORMAL;
                        power_event_to_user(POWER_EVENT_POWER_NORMAL);
                    }
                }
            }
        } else {
            if (lowpower_timer) {
                sys_timer_del(lowpower_timer);
                lowpower_timer = 0 ;
            }
#if TCFG_CHARGE_ENABLE
            if (charge_ccvol_v_cnt > (detect_cnt / 2)) {
                set_charge_mA(get_charge_mA_config());
            }
#endif
        }

        unit_cnt = 0;
        low_off_cnt = 0;
        low_warn_cnt = 0;
        charge_ccvol_v_cnt = 0;

        if ((cur_bat_st != VBAT_LOWPOWER) && (cur_timer_period == VBAT_TIMER_2_MS)) {
            // if (get_charge_online_flag()) {
            //     vbat_timer_update(60 * 1000);
            // } else {
            //     vbat_timer_update(10 * 1000);
            // }
            // vbat_timer_update(200);

            cur_timer_period = VBAT_TIMER_10_S;
            vbat_check_idle = 1;
            cur_battery_level = battery_value_to_phone_level(bat_val);
            if (cur_battery_level != old_battery_level) {
                power_event_to_user(POWER_EVENT_POWER_CHANGE);
            } else {
                if (charge_online_flag != get_charge_online_flag()) {
                    //充电变化也要交换，确定是否在充电仓
                    power_event_to_user(POWER_EVENT_POWER_CHANGE);
                }
            }
            charge_online_flag =  get_charge_online_flag();
            old_battery_level = cur_battery_level;
        }
    }
}

bool vbat_is_low_power(void)
{
    return (cur_bat_st != VBAT_NORMAL);
}

static u8 vbat_check_idle_query(void)
{
    return vbat_check_idle;
}

REGISTER_LP_TARGET(vbat_check_lp_target) = {
    .name = "vbat_check",
    .is_idle = vbat_check_idle_query,
};


void check_power_on_voltage(void)
{
#if(TCFG_SYS_LVD_EN == 1)

    u16 val = 0;
    u8 normal_power_cnt = 0;
    u8 low_power_cnt = 0;

    while (1) {
        clr_wdt();
        val = get_vbat_level();
        printf("vbat: %d\n", val);
        if ((val < app_var.poweroff_tone_v) || adc_check_vbat_lowpower()) {
            low_power_cnt++;
            normal_power_cnt = 0;
            if (low_power_cnt > 10) {
                ui_update_status(STATUS_POWERON_LOWPOWER);
                os_time_dly(100);
                log_info("power on low power , enter softpoweroff!\n");
                power_set_soft_poweroff();
            }
        } else {
            normal_power_cnt++;
            low_power_cnt = 0;
            if (normal_power_cnt > 10) {
                vbat_check_init();
                return;
            }
        }
    }
#endif
}


#if(CONFIG_CPU_BR25)
void app_reset_vddiom_lev(u8 lev)
{
    if (TCFG_LOWPOWER_VDDIOM_LEVEL == VDDIOM_VOL_34V) {
        /* printf("\n\n\n\n\n -------------------set vddiom again %d -----------------------\n\n\n\n\n",lev); */
        reset_vddiom_lev(lev);
    }
}
#else
void app_reset_vddiom_lev(u8 lev)
{

}
#endif

void user_key_low_power_off(void){
    power_event_to_user(POWER_EVENT_POWER_WARNING);
}