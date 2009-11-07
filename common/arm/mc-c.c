/*****************************************************************************
 * mc-c.c: h264 encoder library (Motion Compensation)
 *****************************************************************************
 * Copyright (C) 2009 x264 project
 *
 * Authors: David Conrad <lessen42@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *****************************************************************************/

#include "common/common.h"
#include "mc.h"

void x264_prefetch_ref_arm( uint8_t *, int, int );
void x264_prefetch_fenc_arm( uint8_t *, int, uint8_t *, int, int );

void *x264_memcpy_aligned_neon( void * dst, const void * src, size_t n );
void x264_memzero_aligned_neon( void *dst, int n );

void x264_pixel_avg_16x16_neon( uint8_t *, int, uint8_t *, int, uint8_t *, int, int );
void x264_pixel_avg_16x8_neon( uint8_t *, int, uint8_t *, int, uint8_t *, int, int );
void x264_pixel_avg_8x16_neon( uint8_t *, int, uint8_t *, int, uint8_t *, int, int );
void x264_pixel_avg_8x8_neon( uint8_t *, int, uint8_t *, int, uint8_t *, int, int );
void x264_pixel_avg_8x4_neon( uint8_t *, int, uint8_t *, int, uint8_t *, int, int );
void x264_pixel_avg_4x8_neon( uint8_t *, int, uint8_t *, int, uint8_t *, int, int );
void x264_pixel_avg_4x4_neon( uint8_t *, int, uint8_t *, int, uint8_t *, int, int );
void x264_pixel_avg_4x2_neon( uint8_t *, int, uint8_t *, int, uint8_t *, int, int );

void x264_pixel_avg2_w4_neon( uint8_t *, int, uint8_t *, int, uint8_t *, int );
void x264_pixel_avg2_w8_neon( uint8_t *, int, uint8_t *, int, uint8_t *, int );
void x264_pixel_avg2_w16_neon( uint8_t *, int, uint8_t *, int, uint8_t *, int );
void x264_pixel_avg2_w20_neon( uint8_t *, int, uint8_t *, int, uint8_t *, int );

void x264_mc_copy_w4_neon( uint8_t *, int, uint8_t *, int, int );
void x264_mc_copy_w8_neon( uint8_t *, int, uint8_t *, int, int );
void x264_mc_copy_w16_neon( uint8_t *, int, uint8_t *, int, int );
void x264_mc_copy_w16_aligned_neon( uint8_t *, int, uint8_t *, int, int );

void x264_mc_chroma_neon( uint8_t *, int, uint8_t *, int, int, int, int, int );
void x264_frame_init_lowres_core_neon( uint8_t *, uint8_t *, uint8_t *, uint8_t *, uint8_t *, int, int, int, int);

static void (* const x264_pixel_avg_wtab_neon[6])( uint8_t *, int, uint8_t *, int, uint8_t *, int ) =
{
    NULL,
    x264_pixel_avg2_w4_neon,
    x264_pixel_avg2_w8_neon,
    x264_pixel_avg2_w16_neon,   // no slower than w12, so no point in a separate function
    x264_pixel_avg2_w16_neon,
    x264_pixel_avg2_w20_neon,
};

static void (* const x264_mc_copy_wtab_neon[5])( uint8_t *, int, uint8_t *, int, int ) =
{
    NULL,
    x264_mc_copy_w4_neon,
    x264_mc_copy_w8_neon,
    NULL,
    x264_mc_copy_w16_neon,
};

static const int hpel_ref0[16] = {0,1,1,1,0,1,1,1,2,3,3,3,0,1,1,1};
static const int hpel_ref1[16] = {0,0,0,0,2,2,3,2,2,2,3,2,2,2,3,2};

static void mc_luma_neon( uint8_t *dst,    int i_dst_stride,
                          uint8_t *src[4], int i_src_stride,
                          int mvx, int mvy,
                          int i_width, int i_height )
{
    int qpel_idx = ((mvy&3)<<2) + (mvx&3);
    int offset = (mvy>>2)*i_src_stride + (mvx>>2);
    uint8_t *src1 = src[hpel_ref0[qpel_idx]] + offset;
    if ( (mvy&3) == 3 )             // explict if() to force conditional add
        src1 += i_src_stride;

    if( qpel_idx & 5 ) /* qpel interpolation needed */
    {
        uint8_t *src2 = src[hpel_ref1[qpel_idx]] + offset + ((mvx&3) == 3);
        x264_pixel_avg_wtab_neon[i_width>>2](
                dst, i_dst_stride, src1, i_src_stride,
                src2, i_height );
    }
    else
    {
        x264_mc_copy_wtab_neon[i_width>>2](
                dst, i_dst_stride, src1, i_src_stride, i_height );
    }
}

static uint8_t *get_ref_neon( uint8_t *dst,   int *i_dst_stride,
                              uint8_t *src[4], int i_src_stride,
                              int mvx, int mvy,
                              int i_width, int i_height )
{
    int qpel_idx = ((mvy&3)<<2) + (mvx&3);
    int offset = (mvy>>2)*i_src_stride + (mvx>>2);
    uint8_t *src1 = src[hpel_ref0[qpel_idx]] + offset;
    if ( (mvy&3) == 3 )             // explict if() to force conditional add
        src1 += i_src_stride;

    if( qpel_idx & 5 ) /* qpel interpolation needed */
    {
        uint8_t *src2 = src[hpel_ref1[qpel_idx]] + offset + ((mvx&3) == 3);
        x264_pixel_avg_wtab_neon[i_width>>2](
                dst, *i_dst_stride, src1, i_src_stride,
                src2, i_height );
        return dst;
    }
    else
    {
        *i_dst_stride = i_src_stride;
        return src1;
    }
}

void x264_hpel_filter_v_neon( uint8_t *, uint8_t *, int16_t *, int, int );
void x264_hpel_filter_c_neon( uint8_t *, int16_t *, int );
void x264_hpel_filter_h_neon( uint8_t *, uint8_t *, int );

static void hpel_filter_neon( uint8_t *dsth, uint8_t *dstv, uint8_t *dstc, uint8_t *src,
                              int stride, int width, int height, int16_t *buf )
{
    int realign = (intptr_t)src & 15;
    src -= realign;
    dstv -= realign;
    dstc -= realign;
    dsth -= realign;
    width += realign;
    while( height-- )
    {
        x264_hpel_filter_v_neon( dstv, src, buf+8, stride, width );
        x264_hpel_filter_c_neon( dstc, buf+8, width );
        x264_hpel_filter_h_neon( dsth, src, width );
        dsth += stride;
        dstv += stride;
        dstc += stride;
        src  += stride;
    }
}

void x264_mc_init_arm( int cpu, x264_mc_functions_t *pf )
{
    if( !(cpu&X264_CPU_ARMV6) )
        return;

    pf->prefetch_fenc = x264_prefetch_fenc_arm;
    pf->prefetch_ref  = x264_prefetch_ref_arm;

    if( !(cpu&X264_CPU_NEON) )
        return;

    pf->copy_16x16_unaligned = x264_mc_copy_w16_neon;
    pf->copy[PIXEL_16x16] = x264_mc_copy_w16_aligned_neon;
    pf->copy[PIXEL_8x8]   = x264_mc_copy_w8_neon;
    pf->copy[PIXEL_4x4]   = x264_mc_copy_w4_neon;

    pf->avg[PIXEL_16x16] = x264_pixel_avg_16x16_neon;
    pf->avg[PIXEL_16x8]  = x264_pixel_avg_16x8_neon;
    pf->avg[PIXEL_8x16]  = x264_pixel_avg_8x16_neon;
    pf->avg[PIXEL_8x8]   = x264_pixel_avg_8x8_neon;
    pf->avg[PIXEL_8x4]   = x264_pixel_avg_8x4_neon;
    pf->avg[PIXEL_4x8]   = x264_pixel_avg_4x8_neon;
    pf->avg[PIXEL_4x4]   = x264_pixel_avg_4x4_neon;
    pf->avg[PIXEL_4x2]   = x264_pixel_avg_4x2_neon;

    pf->memcpy_aligned  = x264_memcpy_aligned_neon;
    pf->memzero_aligned = x264_memzero_aligned_neon;

    pf->mc_chroma = x264_mc_chroma_neon;
    pf->mc_luma = mc_luma_neon;
    pf->get_ref = get_ref_neon;
    pf->hpel_filter = hpel_filter_neon;
    pf->frame_init_lowres_core = x264_frame_init_lowres_core_neon;
}
