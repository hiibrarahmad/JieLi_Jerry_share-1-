#include "system/includes.h"
#include "media/includes.h"
#include "tone_player.h"
#include "audio_config.h"
#include "app_main.h"
#include "clock_cfg.h"
#include "audio_dec.h"
#include "key_event_deal.h"

//////////////////////////////////////////////////////////////////////////////

static FILE *record_file = NULL;

//////////////////////////////////////////////////////////////////////////////

extern int last_enc_file_path_get(char path[64]);

//////////////////////////////////////////////////////////////////////////////
/*----------------------------------------------------------------------------*/
/**@brief    关闭录音文件播放
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void record_file_close(void)
{
    file_dec_close();
    if (record_file) {
        fclose(record_file);
        record_file = NULL;
    }
}

/*----------------------------------------------------------------------------*/
/**@brief    录音播放事件回调
   @param    *priv:  私有句柄
   @param    argc: 参数个数
   @param    *argv: 参数
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
static void record_file_play_evt_handler(void *priv, int argc, int *argv)
{
    /* printf("fun = %s\n", __FUNCTION__); */
    record_file_close();
    //播放完 当前录制的音频文件后返回music模式

    puts(">>>>>>>>>>>>>>>>>   record play end\n");
    app_task_put_key_msg(KEY_CHANGE_MODE,0);
    puts(">>>>>>>>>>>>>>>>>   record play end 11\n");

}

/*----------------------------------------------------------------------------*/
/**@brief    录音文件播放
   @param
   @return   0:成功
   @note
*/
/*----------------------------------------------------------------------------*/
int record_file_play(void)
{
    int ret;
    record_file_close();

    char path[64] = {0};
    ret = last_enc_file_path_get(path);
    if (ret) {
        return -1;
    }
    record_file = fopen(path, "r");
    if (!record_file) {
        return -1;
    }
    ret = file_dec_create(NULL, record_file_play_evt_handler);
    if (ret) {
        return -1;
    }
    ret = file_dec_open(record_file, NULL);
    return ret;
}

/*----------------------------------------------------------------------------*/
/**@brief    按照路径播放录音文件
   @param
   @return   0:成功
   @note	 针对特殊需求， 用户可以直接指定录音路径播放,盘符在路径中直接指定
*/
/*----------------------------------------------------------------------------*/
int record_file_play_by_path(char *path)
{
    int ret;
    record_file_close();

    record_file = fopen(path, "r");
    if (!record_file) {
        return -1;
    }
    ret = file_dec_create(NULL, record_file_play_evt_handler);
    if (ret) {
        return -1;
    }
    ret = file_dec_open(record_file, NULL);
    return ret;
}


/*----------------------------------------------------------------------------*/
/**@brief    获取录音播放总事件
   @param
   @return   总时间
   @note
*/
/*----------------------------------------------------------------------------*/
int record_file_get_total_time(void)
{
    if (!record_file) {
        return 0;
    }
    return file_dec_get_total_time();
}

/*----------------------------------------------------------------------------*/
/**@brief    获取录音播放当前时间
   @param
   @return   当前时间
   @note
*/
/*----------------------------------------------------------------------------*/
int record_file_dec_get_cur_time(void)
{
    if (!record_file) {
        return 0;
    }
    return file_dec_get_cur_time();
}



