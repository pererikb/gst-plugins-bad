/* GStreamer Intel MSDK plugin
 * Copyright (c) 2018, Intel Corporation, Inc.
 * Author : Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __GST_MSDKVPP_H__
#define __GST_MSDKVPP_H__

#include "gstmsdkcontext.h"
#include "msdk-enums.h"
#include <gst/base/gstbasetransform.h>
G_BEGIN_DECLS

#define GST_TYPE_MSDKVPP \
  (gst_msdkvpp_get_type())
#define GST_MSDKVPP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MSDKVPP,GstMsdkVPP))
#define GST_MSDKVPP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MSDKVPP,GstMsdkVPPClass))
#define GST_MSDKVPP_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_MSDKVPP,GstMsdkVPPClass))
#define GST_IS_MSDKVPP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MSDKVPP))
#define GST_IS_MSDKVPP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MSDKVPP))

#define MAX_EXTRA_PARAMS                 8

typedef struct _GstMsdkVPP GstMsdkVPP;
typedef struct _GstMsdkVPPClass GstMsdkVPPClass;

typedef enum {
  GST_MSDK_FLAG_DENOISE      = 1 << 0,
  GST_MSDK_FLAG_ROTATION     = 1 << 1,
  GST_MSDK_FLAG_DEINTERLACE  = 1 << 2,
  GST_MSDK_FLAG_HUE          = 1 << 3,
  GST_MSDK_FLAG_SATURATION   = 1 << 4,
  GST_MSDK_FLAG_BRIGHTNESS   = 1 << 5,
  GST_MSDK_FLAG_CONTRAST     = 1 << 6,
  GST_MSDK_FLAG_DETAIL       = 1 << 7,
  GST_MSDK_FLAG_MIRRORING    = 1 << 8,
  GST_MSDK_FLAG_SCALING_MODE = 1 << 9,
  GST_MSDK_FLAG_FRC          = 1 << 10,
  GST_MSDK_FLAG_VIDEO_DIRECTION = 1 << 11,
} GstMsdkVppFlags;

struct _GstMsdkVPP
{
  GstBaseTransform element;

  /* sinkpad info */
  GstPad *sinkpad;
  GstVideoInfo sinkpad_info;
  GstVideoInfo sinkpad_buffer_pool_info;
  GstBufferPool *sinkpad_buffer_pool;

  /* srcpad info */
  GstPad *srcpad;
  GstVideoInfo srcpad_info;
  GstVideoInfo srcpad_buffer_pool_info;
  GstBufferPool *srcpad_buffer_pool;

  /* MFX context */
  GstMsdkContext *context;
  GstMsdkContext *old_context;
  mfxVideoParam param;
  guint in_num_surfaces;
  guint out_num_surfaces;
  mfxFrameAllocResponse in_alloc_resp;
  mfxFrameAllocResponse out_alloc_resp;

  gboolean initialized;
  gboolean use_video_memory;
  gboolean use_sinkpad_dmabuf;
  gboolean use_srcpad_dmabuf;
  gboolean shared_context;
  gboolean add_video_meta;
  gboolean need_vpp;
  guint flags;

  /* element properties */
  gboolean hardware;
  guint async_depth;
  guint denoise_factor;
  guint rotation;
  guint deinterlace_mode;
  guint deinterlace_method;
  gfloat hue;
  gfloat saturation;
  gfloat brightness;
  gfloat contrast;
  guint detail;
  guint mirroring;
  guint scaling_mode;
  gboolean keep_aspect;
  guint frc_algm;
  guint video_direction;
  guint crop_left;
  guint crop_right;
  guint crop_top;
  guint crop_bottom;

  GstClockTime buffer_duration;

  /* MFX Filters */
  mfxExtVPPDoUse mfx_vpp_douse;
  mfxExtVPPDenoise mfx_denoise;
  mfxExtVPPRotation mfx_rotation;
  mfxExtVPPDeinterlacing mfx_deinterlace;
  mfxExtVPPProcAmp mfx_procamp;
  mfxExtVPPDetail mfx_detail;
  mfxExtVPPMirroring mfx_mirroring;
  mfxExtVPPScaling mfx_scaling;
  mfxExtVPPFrameRateConversion mfx_frc;

  /* Extended buffers */
  mfxExtBuffer *extra_params[MAX_EXTRA_PARAMS];
  guint num_extra_params;
};

struct _GstMsdkVPPClass
{
  GstBaseTransformClass parent_class;
};

GType gst_msdkvpp_get_type (void);

G_END_DECLS

#endif /* __GST_MSDKVPP_H__ */
