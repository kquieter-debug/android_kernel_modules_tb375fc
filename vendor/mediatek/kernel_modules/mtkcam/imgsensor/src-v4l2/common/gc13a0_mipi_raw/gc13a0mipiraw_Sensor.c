// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022 MediaTek Inc.

/*****************************************************************************
 *
 * Filename:
 * ---------
 *	 gc13a0mipiraw_Sensor.c
 *
 * Project:
 * --------
 *	 ALPS
 *
 * Description:
 * ------------
 *	 Source code of Sensor driver
 *
 *
 *------------------------------------------------------------------------------
 * Upper this line, this part is controlled by CC/CQ. DO NOT MODIFY!!
 *============================================================================
 ****************************************************************************/
#include "gc13a0mipiraw_Sensor.h"

#define SSSS 0

static int gc13a0_set_shutter(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int gc13a0_set_gain(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int  gc13a0_set_test_pattern(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int gc13a0_set_shutter_frame_length(struct subdrv_ctx *ctx,u8 *para, u32 *len);
static int init_ctx(struct subdrv_ctx *ctx,	struct i2c_client *i2c_client, u8 i2c_write_id);
static int ultra_vsync_notify(struct subdrv_ctx *ctx, unsigned int sof_cnt);
static int gc13a0_get_csi_param(struct subdrv_ctx *ctx, enum SENSOR_SCENARIO_ID_ENUM scenario_id, struct mtk_csi_param *csi_param);

static struct eeprom_info_struct eeprom_info[] = {
	{
		.header_id = 0x04030201,//0x43535338
		.addr_header_id = 0x00000001,
		.i2c_write_id = 0xA0,

		.xtalk_support = TRUE,
		.xtalk_size = 2048,
		.addr_xtalk = 0x150F,
	},
};

/* STRUCT */
static struct subdrv_feature_control feature_control_list[] = {
	{SENSOR_FEATURE_SET_TEST_PATTERN, gc13a0_set_test_pattern},
    {SENSOR_FEATURE_SET_GAIN, gc13a0_set_gain},
	{SENSOR_FEATURE_SET_ESHUTTER, gc13a0_set_shutter},
	{SENSOR_FEATURE_SET_SHUTTER_FRAME_TIME, gc13a0_set_shutter_frame_length},	
};

// mode 0: 4208*3120@30fps, normal preview
static struct mtk_mbus_frame_desc_entry frame_desc_prev[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 4208,
			.vsize = 3120,
			.user_data_desc = VC_STAGGER_NE,
		},
	},
};

// mode 1: 4208*3120@30fps, capture preview
static struct mtk_mbus_frame_desc_entry frame_desc_cap[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 4208,
			.vsize = 3120,
			.user_data_desc = VC_STAGGER_NE,
		},
	},
};

// mode 2: 3840*2160@30fps, normal video(4k)
static struct mtk_mbus_frame_desc_entry frame_desc_video[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 4208,
			.vsize = 2368,
			.user_data_desc = VC_STAGGER_NE,
		},
	},
};

static struct SET_PD_BLOCK_INFO_T imgsensor_pd_info = {
	.i4OffsetX = 24,
	.i4OffsetY = 24,
	.i4PitchX = 64,
	.i4PitchY = 64,
	.i4PairNum = 16,
	.i4SubBlkW = 16,
	.i4SubBlkH = 16,
    .i4BlockNumX = 65,
    .i4BlockNumY = 48,
	.i4PosL = {{28, 27},{80, 27},{44, 31},{64, 31},{32, 47},{76 ,47},{48, 51},{60, 51},
        {48, 59},{60, 59},{32, 63},{76, 63},{44, 79},{64, 79},{28, 83},{80, 83}},
	.i4PosR = {{28, 31},{80, 31},{44, 35},{64, 35},{32, 43},{76, 43},{48, 47},{60, 47},
        {48, 63},{60, 63},{32, 67},{76, 67},{44, 75},{64, 75},{28, 79},{80, 79}},
	.i4Crop = {
		// <prev> <cap> <vid> <hs_vid> <slim_vid>
		{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
		// <cust1> <cust2> <cust3> <cust4> <cust5>
		{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
		// <cust6> <cust7>
		{0, 0}, {0, 0},
	},
    .iMirrorFlip = 0,
    .i4ModeIndex = 1,
    .PDAF_Support = PDAF_SUPPORT_RAW,
};

static struct SET_PD_BLOCK_INFO_T imgsensor_pd_info_vid = {
	.i4OffsetX = 24,
	.i4OffsetY = 24,
	.i4PitchX = 64,
	.i4PitchY = 64,
	.i4PairNum = 16,
	.i4SubBlkW = 16,
	.i4SubBlkH = 16,
    .i4BlockNumX = 65,
    .i4BlockNumY = 36,
	.i4PosL = {{28, 27},{80, 27},{44, 31},{64, 31},{32, 47},{76 ,47},{48, 51},{60, 51},
        {48, 59},{60, 59},{32, 63},{76, 63},{44, 79},{64, 79},{28, 83},{80, 83}},
	.i4PosR = {{28, 31},{80, 31},{44, 35},{64, 35},{32, 43},{76, 43},{48, 47},{60, 47},
        {48, 63},{60, 63},{32, 67},{76, 67},{44, 75},{64, 75},{28, 79},{80, 79}},
	.i4Crop = {
		// <prev> <cap> <vid> <hs_vid> <slim_vid>
		{0, 0}, {0, 0}, {0, 376}, {0, 0}, {0, 0},
		// <cust1> <cust2> <cust3> <cust4> <cust5>
		{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
		// <cust6> <cust7>
		{0, 0}, {0, 0},
	},
    .iMirrorFlip = 0,
    .i4ModeIndex = 1,
    .PDAF_Support = PDAF_SUPPORT_RAW,
};

// mode 0: 4208*3120@30fps, normal preview
#define preview_mode_struct \
{\
	.frame_desc = frame_desc_prev,\
	.num_entries = ARRAY_SIZE(frame_desc_prev),\
	.mode_setting_table = gc13a0_4208x3120_addr_data,\
	.mode_setting_len = ARRAY_SIZE(gc13a0_4208x3120_addr_data),\
	.seamless_switch_group = PARAM_UNDEFINED,\
	.seamless_switch_mode_setting_table = PARAM_UNDEFINED,\
	.seamless_switch_mode_setting_len = PARAM_UNDEFINED,\
	.hdr_mode = HDR_NONE,\
	.raw_cnt = 1,\
	.exp_cnt = 1,\
	.pclk = 568800000,\
	.linelength = 5850,\
	.framelength = 3232,\
	.max_framerate = 300,\
    .mipi_pixel_rate = 480000000,\
	.readout_length = 0,\
	.read_margin = 16,\
	.framelength_step = 2,\
	.imgsensor_winsize_info = {\
		.full_w = 4208,\
		.full_h = 3120,\
		.x0_offset = 0,\
		.y0_offset = 0,\
		.w0_size = 4208,\
		.h0_size = 3120,\
		.scale_w = 4208,\
		.scale_h = 3120,\
		.x1_offset = 0,\
		.y1_offset = 0,\
		.w1_size = 4208,\
		.h1_size = 3120,\
		.x2_tg_offset = 0,\
		.y2_tg_offset = 0,\
		.w2_tg_size = 4208,\
		.h2_tg_size = 3120,\
	},\
	.pdaf_cap = TRUE,\
	.imgsensor_pd_info = &imgsensor_pd_info,\
	.ae_binning_ratio = 1000,\
	.fine_integ_line = 0,\
	.delay_frame = 2,\
}

// mode 1: 4208*3120@30fps, normal capture
#define capture_mode_struct \
{\
	.frame_desc = frame_desc_cap,\
	.num_entries = ARRAY_SIZE(frame_desc_cap),\
	.mode_setting_table = gc13a0_4208x3120_addr_data,\
	.mode_setting_len = ARRAY_SIZE(gc13a0_4208x3120_addr_data),\
	.seamless_switch_group = PARAM_UNDEFINED,\
	.seamless_switch_mode_setting_table = PARAM_UNDEFINED,\
	.seamless_switch_mode_setting_len = PARAM_UNDEFINED,\
	.hdr_mode = HDR_NONE,\
	.raw_cnt = 1,\
	.exp_cnt = 1,\
	.pclk = 568800000,\
	.linelength = 5850,\
	.framelength = 3232,\
	.max_framerate = 300,\
	.mipi_pixel_rate = 480000000,\
	.readout_length = 0,\
	.read_margin = 16,\
	.framelength_step = 2,\
	.imgsensor_winsize_info = {\
		.full_w = 4208,\
		.full_h = 3120,\
		.x0_offset = 0,\
		.y0_offset = 0,\
		.w0_size = 4208,\
		.h0_size = 3120,\
		.scale_w = 4208,\
		.scale_h = 3120,\
		.x1_offset = 0,\
		.y1_offset = 0,\
		.w1_size = 4208,\
		.h1_size = 3120,\
		.x2_tg_offset = 0,\
		.y2_tg_offset = 0,\
		.w2_tg_size = 4208,\
		.h2_tg_size = 3120,\
	},\
	.pdaf_cap = TRUE,\
	.imgsensor_pd_info = &imgsensor_pd_info,\
	.ae_binning_ratio = 1000,\
	.fine_integ_line = 0,\
	.delay_frame = 2,\
}

// mode 2: 3840*2160@30fps, normal video(4K)
#define normal_video_mode_struct \
{\
	.frame_desc = frame_desc_video,\
	.num_entries = ARRAY_SIZE(frame_desc_video),\
	.mode_setting_table = gc13a0_4208x2368_addr_data,\
	.mode_setting_len = ARRAY_SIZE(gc13a0_4208x2368_addr_data),\
	.seamless_switch_group = PARAM_UNDEFINED,\
	.seamless_switch_mode_setting_table = PARAM_UNDEFINED,\
	.seamless_switch_mode_setting_len = PARAM_UNDEFINED,\
	.hdr_mode = HDR_NONE,\
	.raw_cnt = 1,\
	.exp_cnt = 1,\
	.pclk = 568800000,\
	.linelength = 5850,\
	.framelength = 3232,\
	.max_framerate = 300,\
	.mipi_pixel_rate = 480000000,\
	.readout_length = 0,\
	.read_margin = 16,\
	.framelength_step = 2,\
	.imgsensor_winsize_info = {\
		.full_w = 4208,\
		.full_h = 3120,\
		.x0_offset = 0,\
		.y0_offset = 0,\
		.w0_size = 4208,\
		.h0_size = 3120,\
		.scale_w = 4208,\
		.scale_h = 3120,\
		.x1_offset = 0,\
		.y1_offset = 376,\
		.w1_size = 4208,\
		.h1_size = 2368,\
		.x2_tg_offset = 0,\
		.y2_tg_offset = 0,\
		.w2_tg_size = 4208,\
		.h2_tg_size = 2368,\
	},\
	.pdaf_cap = TRUE,\
	.imgsensor_pd_info = &imgsensor_pd_info_vid,\
	.ae_binning_ratio = 1000,\
	.fine_integ_line = 0,\
	.delay_frame = 2,\
}

static struct subdrv_mode_struct mode_struct[] = {
	preview_mode_struct,		//mode 0
	capture_mode_struct,		//mode 1
    normal_video_mode_struct,       //mode 2
};

static struct subdrv_static_ctx static_ctx = {
	.sensor_id = GC13A0_SENSOR_ID,
	.reg_addr_sensor_id = {0x03F0, 0x03F1},
	.i2c_addr_table = {0x22, 0xff},
	.i2c_burst_write_support = FALSE,
	.i2c_transfer_data_type = I2C_DT_ADDR_16_DATA_8_SEPAR,
	.eeprom_info = eeprom_info,
	.eeprom_num = ARRAY_SIZE(eeprom_info),
	.resolution = {4208, 3120},
	.mirror = IMAGE_NORMAL, // TBD

	.mclk = 24,
	.isp_driving_current = ISP_DRIVING_4MA,
	.sensor_interface_type = SENSOR_INTERFACE_TYPE_MIPI,
	.mipi_sensor_type = MIPI_OPHY_NCSI2,
	.mipi_lane_num = SENSOR_MIPI_4_LANE,
	.ob_pedestal = 0x40,

	.sensor_output_dataformat = SENSOR_OUTPUT_FORMAT_RAW_Gr,
	.ana_gain_def = BASEGAIN * 4,
	.ana_gain_min = BASEGAIN * 1,
	.ana_gain_max = BASEGAIN * 16,
	.ana_gain_type = 1,
	.ana_gain_step = 1,
	.ana_gain_table = gc13a0_ana_gain_table,
	.ana_gain_table_size = sizeof(gc13a0_ana_gain_table),
	.min_gain_iso = 100,
	.exposure_def = 0x3D0,
	.exposure_min = 4,
	.exposure_max = 0xfffe - 16,
	.exposure_step = 2,
	.exposure_margin = 16,//8

	.frame_length_max = 0xfffe,
	.ae_effective_frame = 3,
	.frame_time_delay_frame = 2,
	.start_exposure_offset = 153800,

	.pdaf_type = PDAF_SUPPORT_RAW,
	.hdr_type = HDR_SUPPORT_NA,
	.seamless_switch_support = FALSE,
	.temperature_support = FALSE,

	.g_temp = PARAM_UNDEFINED,
	.g_gain2reg = PARAM_UNDEFINED,//
	.s_gph = PARAM_UNDEFINED,

	.reg_addr_stream = 0x0100,
	.reg_addr_mirror_flip = PARAM_UNDEFINED, // TBD
	.reg_addr_exposure = {
		{0x0202, 0x0203}
	},
	.long_exposure_support = FALSE,
	.reg_addr_exposure_lshift = PARAM_UNDEFINED,
	.reg_addr_ana_gain = {
		{0x3508, 0x3509}
	},
	.reg_addr_frame_length = {0x0340, 0x0341},
	.reg_addr_temp_en = PARAM_UNDEFINED,
	.reg_addr_temp_read = PARAM_UNDEFINED,
	.reg_addr_auto_extend = 0x3822,
	.reg_addr_frame_count = PARAM_UNDEFINED,

	.init_setting_table =gc13a0_init_setting,
	.init_setting_len = ARRAY_SIZE(gc13a0_init_setting),
	.mode = mode_struct,
	.sensor_mode_num = ARRAY_SIZE(mode_struct),
	.list = feature_control_list,
	.list_len = ARRAY_SIZE(feature_control_list),
	.chk_s_off_sta = 0,
	.chk_s_off_end = 0,

	//TBD
	.checksum_value = 0xa4c32546,
};

static struct subdrv_ops ops = {
	.get_id = common_get_imgsensor_id,
	.init_ctx = init_ctx,
	.open = common_open,
	.get_info = common_get_info,
	.get_resolution = common_get_resolution,
	.control = common_control,
	.feature_control = common_feature_control,
	.close = common_close,
	.get_frame_desc = common_get_frame_desc,
	.get_temp = common_get_temp,
	.get_csi_param = gc13a0_get_csi_param,
	.update_sof_cnt = common_update_sof_cnt,
	.vsync_notify = ultra_vsync_notify,
};

static struct subdrv_pw_seq_entry pw_seq[] = {
	{HW_ID_RST, 0, 5},
	{HW_ID_MCLK, 24, 5},
	{HW_ID_MCLK_DRIVING_CURRENT, 4, 5},//unknow
	{HW_ID_DOVDD, 1800000, 5},
	{HW_ID_DVDD1, 1, 5},
	{HW_ID_AVDD1, 1, 5},
	{HW_ID_AFVDD, 2800000, 3},
	{HW_ID_RST, 1, 5},
};

const struct subdrv_entry gc13a0_mipi_raw_entry = {
	.name = "gc13a0_mipi_raw",
	.id = GC13A0_SENSOR_ID,
	.pw_seq = pw_seq,
	.pw_seq_cnt = ARRAY_SIZE(pw_seq),
	.ops = &ops,
};

static int gc13a0_set_test_pattern(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	u32 mode = *((u32 *)para);

	if (mode != ctx->test_pattern)
		pr_err( "mode(%u->%u)\n", ctx->test_pattern, mode);
	if (mode) {
		subdrv_i2c_wr_u8(ctx, 0x008c, 0x01);
        subdrv_i2c_wr_u8(ctx, 0x008d, 0x00);
	}else{
		subdrv_i2c_wr_u8(ctx, 0x008c, 0x00);
	}
	ctx->test_pattern = mode;
	return ERROR_NONE;
}

static int init_ctx(struct subdrv_ctx *ctx,	struct i2c_client *i2c_client, u8 i2c_write_id)
{
	memcpy(&(ctx->s_ctx), &static_ctx, sizeof(struct subdrv_static_ctx));
	subdrv_ctx_init(ctx);
	ctx->i2c_client = i2c_client;
	ctx->i2c_write_id = i2c_write_id;
	pr_err("init_ctx");
	return 0;
}

static int ultra_vsync_notify(struct subdrv_ctx *ctx, unsigned int sof_cnt)
{
	u16 sensor_output_cnt;

	sensor_output_cnt = (subdrv_i2c_rd_u8(ctx, 0x4848) << 8);
	sensor_output_cnt |= subdrv_i2c_rd_u8(ctx, 0x4849);
	DRV_LOG_MUST(ctx, "sensormode(%d) sof_cnt(%d) sensor_output_cnt(%d)\n",
		ctx->current_scenario_id, sof_cnt, sensor_output_cnt);
	return 0;
};

static int gc13a0_get_csi_param(struct subdrv_ctx *ctx,
	enum SENSOR_SCENARIO_ID_ENUM scenario_id,
	struct mtk_csi_param *csi_param)
{
	DRV_LOG(ctx, "+ scenario_id:%u,aov_csi_clk:%u\n",scenario_id, ctx->aov_csi_clk);
	switch (scenario_id) {
	default:
		csi_param->legacy_phy = 0;
		csi_param->not_fixed_trail_settle = 1;
		csi_param->not_fixed_dphy_settle = 1;
		csi_param->dphy_data_settle = 0x28;
		csi_param->dphy_clk_settle = 0x28;
		csi_param->dphy_trail = 0x40;
		break;
	}
	return 0;
}


static int gc13a0_set_gain(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
    u32 gain = *((u32 *)para);
    u32 reg_gain, min_gain;
    u32 max_gain;
    max_gain = ctx->s_ctx.ana_gain_max;
    min_gain = ctx->s_ctx.ana_gain_min;

    if (gain < min_gain || gain > max_gain) {
        if (gain < min_gain)
            gain = min_gain;
        else if (gain > max_gain)
            gain = max_gain;
    }
    reg_gain = gain;
    memset(ctx->ana_gain, 0, sizeof(ctx->ana_gain));
	ctx->ana_gain[0] = gain;
    subdrv_i2c_wr_u16(ctx, 0x0204, reg_gain & 0xffff);
    reg_gain = subdrv_i2c_rd_u16(ctx, 0x0204);
    return gain;
}


void gc13a0_set_dummy(struct subdrv_ctx *ctx)
{
    subdrv_i2c_wr_u16(ctx, 0x0340, ctx->frame_length & 0xfffe);
} /* set_dummy */

void gc13a0_set_max_framerate(struct subdrv_ctx *ctx,
        u16 framerate, bool min_framelength_en)
{
    /*kal_int16 dummy_line;*/
    u32 frame_length = ctx->frame_length;

    frame_length = ctx->pclk / framerate * 10 / ctx->line_length;
    if (frame_length >= ctx->min_frame_length)
        ctx->frame_length = frame_length;
    else
        ctx->frame_length = ctx->min_frame_length;

    ctx->dummy_line =
            ctx->frame_length - ctx->min_frame_length;

    if (ctx->frame_length > ctx->s_ctx.frame_length_max) {
        ctx->frame_length = ctx->s_ctx.frame_length_max;
        ctx->dummy_line =
            ctx->frame_length - ctx->min_frame_length;
    }
    if (min_framelength_en)
        ctx->min_frame_length = ctx->frame_length;
        
    gc13a0_set_dummy(ctx);
} /* set_max_framerate */

static int gc13a0_set_shutter(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	u64 *feature_data = (u64 *)para;
	u32 shutter = *feature_data;
   // u32 frame_length = *(feature_data + 1);
    u16 realtime_fps = 0;

    if (shutter > ctx->min_frame_length - ctx->s_ctx.exposure_margin)
        ctx->frame_length = shutter + ctx->s_ctx.exposure_margin;
    else
        ctx->frame_length = ctx->min_frame_length;

    if (ctx->frame_length > ctx->s_ctx.frame_length_max)
        ctx->frame_length = ctx->s_ctx.frame_length_max;
    if (shutter < ctx->s_ctx.exposure_min)//
        shutter = ctx->s_ctx.exposure_min;

    if (ctx->autoflicker_en) {
        realtime_fps = ctx->pclk / ctx->line_length * 10
                / ctx->frame_length;
        if (realtime_fps >= 297 && realtime_fps <= 305)
            gc13a0_set_max_framerate(ctx, 296, 0);
        else if (realtime_fps >= 147 && realtime_fps <= 150)
            gc13a0_set_max_framerate(ctx, 146, 0);
        else
            subdrv_i2c_wr_u16(ctx, 0x0340, ctx->frame_length & 0xfffe);
    } else
            subdrv_i2c_wr_u16(ctx, 0x0340, ctx->frame_length & 0xfffe);

    // Update Shutter 
    subdrv_i2c_wr_u16(ctx, 0x0202, shutter & 0xffff);
    return ERROR_NONE;
}


static int gc13a0_set_shutter_frame_length(struct subdrv_ctx *ctx,u8 *para, u32 *len)
{
    u64 *feature_data = (u64 *)para;
    u32 shutter = *feature_data;
    u32 frame_length  = *(feature_data +1);

    u16 realtime_fps = 0;
    u32 dummy_line = 0;
    ctx->shutter = shutter;

    // Change frame time
    if (frame_length > 1)
        dummy_line = frame_length - ctx->frame_length;

    ctx->frame_length = ctx->frame_length + dummy_line;

    if (shutter > ctx->frame_length - ctx->s_ctx.exposure_margin)
        ctx->frame_length = shutter + ctx->s_ctx.exposure_margin;

    if (ctx->frame_length > ctx->s_ctx.frame_length_max)
        ctx->frame_length = ctx->s_ctx.frame_length_max;

    shutter = (shutter < ctx->s_ctx.exposure_min)
            ? ctx->s_ctx.exposure_min : shutter;
    shutter = (shutter > ( ctx->s_ctx.frame_length_max - ctx->s_ctx.exposure_margin))
        ? ( ctx->s_ctx.frame_length_max - ctx->s_ctx.exposure_margin)
        : shutter;

    if (ctx->autoflicker_en) {
        realtime_fps = ctx->pclk / ctx->line_length * 10
                / ctx->frame_length;
        if (realtime_fps >= 297 && realtime_fps <= 305)
            gc13a0_set_max_framerate(ctx, 296, 0);
        else if (realtime_fps >= 147 && realtime_fps <= 150)
            gc13a0_set_max_framerate(ctx, 146, 0);
        else
            subdrv_i2c_wr_u16(ctx, 0x0340, ctx->frame_length & 0xfffe);
    } else
            subdrv_i2c_wr_u16(ctx, 0x0340, ctx->frame_length & 0xfffe);

    // Update Shutter 
    subdrv_i2c_wr_u16(ctx, 0x0202, shutter & 0xffff);
    pr_err("shutter =%d, framelength =%d\n",
        shutter, ctx->frame_length);
    return ERROR_NONE;

} 


