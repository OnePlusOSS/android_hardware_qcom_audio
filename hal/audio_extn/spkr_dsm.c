/*
 * Copyright (c) 2013 - 2015, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define LOG_TAG "audio_hw_spkr_dsm"
/*#define LOG_NDEBUG 0*/
//#define LOG_NDDEBUG 0

#include <errno.h>
#include <math.h>
#include <cutils/log.h>
#include <fcntl.h>
#include <dirent.h>
#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include <sys/stat.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <math.h>
#include <cutils/properties.h>
#include "audio_extn.h"
#include <linux/msm_audio_calibration.h>
#define SPKR_DSM_ENABLED
#ifdef SPKR_DSM_ENABLED

#define SPKR_PROCESSING_IN_PROGRESS 1
#define SPKR_PROCESSING_IN_IDLE 0

struct speaker_dsm_session {
    int spkr_processing_state;
    struct pcm *pcm_tx;
 };

static struct speaker_dsm_session dsm_handle ={
    .pcm_tx = 0,
    .spkr_processing_state = 0,
};

static struct pcm_config pcm_config_spkr_dsm = {
    .channels = 2,
    .rate = 48000,
    .period_size = 256,
    .period_count = 4,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

int audio_extn_spkr_dsm_start_processing(struct audio_device *adev)
{
    struct audio_usecase *uc_info_tx;
    int32_t pcm_dev_tx_id = -1, ret = 0;

    ALOGD("%s: Entry", __func__);
    /* cancel speaker calibration */
    if (!adev) {
       ALOGE("%s: Invalid params", __func__);
       return -EINVAL;
    }
    uc_info_tx = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));
    if (!uc_info_tx) {
        return -ENOMEM;
    }

    ALOGD("%s: start", __func__);
    if (dsm_handle.spkr_processing_state == SPKR_PROCESSING_IN_IDLE) {
        uc_info_tx->id = USECASE_AUDIO_SPKR_CALIB_TX;
        uc_info_tx->type = PCM_CAPTURE;
        uc_info_tx->in_snd_device = SND_DEVICE_IN_CAPTURE_VI_FEEDBACK;
        uc_info_tx->out_snd_device = SND_DEVICE_NONE;
        list_add_tail(&adev->usecase_list, &uc_info_tx->list);
        //enable_snd_device(adev, SND_DEVICE_IN_CAPTURE_VI_FEEDBACK);
        //enable_audio_route(adev, uc_info_tx);
	//audio_extn_utils_send_audio_calibration(adev, uc_info_tx);
	platform_send_audio_calibration(adev->platform, uc_info_tx, platform_get_default_app_type(adev->platform), 48000);

        pcm_dev_tx_id = platform_get_pcm_device_id(uc_info_tx->id, PCM_CAPTURE);
	pcm_dev_tx_id = 35;
        if (pcm_dev_tx_id < 0) {
            ALOGE("%s: Invalid pcm device for usecase (%d)",
                    __func__, uc_info_tx->id);
            ret = -ENODEV;
            goto exit;
        }

        ALOGD("%s: pcm start for TX to %d", __func__, pcm_dev_tx_id);
        dsm_handle.pcm_tx = pcm_open(adev->snd_card,
                pcm_dev_tx_id,
                PCM_IN, &pcm_config_spkr_dsm);
        if (dsm_handle.pcm_tx && !pcm_is_ready(dsm_handle.pcm_tx)) {
            ALOGE("%s: %s", __func__, pcm_get_error(dsm_handle.pcm_tx));
            ret = -EIO;
            goto exit;
        }

        if (pcm_start(dsm_handle.pcm_tx) < 0) {
            ALOGE("%s: pcm start for TX failed to %d", __func__, pcm_dev_tx_id);
            ret = -EINVAL;
        }
    }else{
        free(uc_info_tx);
    }
exit:
     if (ret) {
        if (dsm_handle.pcm_tx)
            pcm_close(dsm_handle.pcm_tx);
        dsm_handle.pcm_tx = NULL;
        list_remove(&uc_info_tx->list);
        uc_info_tx->id = USECASE_AUDIO_SPKR_CALIB_TX;
        uc_info_tx->type = PCM_CAPTURE;
        uc_info_tx->in_snd_device = SND_DEVICE_IN_CAPTURE_VI_FEEDBACK;
        uc_info_tx->out_snd_device = SND_DEVICE_NONE;
        /* disable_snd_device(adev, SND_DEVICE_IN_CAPTURE_VI_FEEDBACK); */
        /* disable_audio_route(adev, uc_info_tx); */
        free(uc_info_tx);
     }else{
        dsm_handle.spkr_processing_state = SPKR_PROCESSING_IN_PROGRESS;
     }

    ALOGE("%s: Exit", __func__);
    return ret;
}

void audio_extn_spkr_dsm_stop_processing(struct audio_device *adev)
{
    struct audio_usecase *uc_info_tx;

    ALOGE("%s: Entry", __func__);
    if (adev && dsm_handle.spkr_processing_state == SPKR_PROCESSING_IN_PROGRESS) {
        uc_info_tx = get_usecase_from_list(adev, USECASE_AUDIO_SPKR_CALIB_TX);
        if (dsm_handle.pcm_tx)
            pcm_close(dsm_handle.pcm_tx);
        dsm_handle.pcm_tx = NULL;
        /* disable_snd_device(adev, SND_DEVICE_IN_CAPTURE_VI_FEEDBACK); */
        if (uc_info_tx) {
            list_remove(&uc_info_tx->list);
            /* disable_audio_route(adev, uc_info_tx); */
            free(uc_info_tx);
        }
    }
    dsm_handle.spkr_processing_state = SPKR_PROCESSING_IN_IDLE;
    ALOGE("%s: Exit", __func__);
}

#endif /*SPKR_PROT_ENABLED*/
