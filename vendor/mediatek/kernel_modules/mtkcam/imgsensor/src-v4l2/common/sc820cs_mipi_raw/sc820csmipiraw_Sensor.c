// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022 MediaTek Inc.

/*****************************************************************************
 *
 * Filename:
 * ---------
 *	 sc820csmipiraw_Sensor.c
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
#include "sc820csmipiraw_Sensor.h"

static void set_group_hold(void *arg, u8 en);
static u16 get_gain2reg(u32 gain);
static int  sc820cs_set_gain(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int  sc820cs_set_test_pattern(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int  sc820cs_set_shutter(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int init_ctx(struct subdrv_ctx *ctx,	struct i2c_client *i2c_client, u8 i2c_write_id);
static int ultra_vsync_notify(struct subdrv_ctx *ctx, unsigned int sof_cnt);
static int sc820cs_get_csi_param(struct subdrv_ctx *ctx, enum SENSOR_SCENARIO_ID_ENUM scenario_id, struct mtk_csi_param *csi_param);

#define sc820cs_LY_SENSOR_GAIN_MAX_VALID_INDEX  6
#define sc820cs_LY_SENSOR_GAIN_MAP_SIZE         6

#define sc820cs_LY_SENSOR_BASE_GAIN           0x400
#define sc820cs_LY_SENSOR_MAX_GAIN            (32* sc820cs_LY_SENSOR_BASE_GAIN )

/* STRUCT */
static struct subdrv_feature_control feature_control_list[] = {
	{SENSOR_FEATURE_SET_TEST_PATTERN, sc820cs_set_test_pattern},
	{SENSOR_FEATURE_SET_ESHUTTER, sc820cs_set_shutter},
	{SENSOR_FEATURE_SET_GAIN, sc820cs_set_gain}
};

static struct eeprom_info_struct eeprom_info[] = {
	{
		.header_id = 0x3551d90b,
		.addr_header_id = 0x00000001,
		.i2c_write_id = 0xA0,

		.xtalk_support = TRUE,
		.xtalk_size = 2048,
		.addr_xtalk = 0x150F,
	},
};

// mode 0: 1632*1224@30fps, normal preview
static struct mtk_mbus_frame_desc_entry frame_desc_prev[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 1632,
			.vsize = 1224,
			.user_data_desc = VC_STAGGER_NE,
		},
	},
};

// mode 1: 3264*2448@30fps, normal cap
static struct mtk_mbus_frame_desc_entry frame_desc_cap[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 3264,
			.vsize = 2448,
			.user_data_desc = VC_STAGGER_NE,
		},
	},
};

// mode 0: 3264*1836@30fps, normal video
static struct mtk_mbus_frame_desc_entry frame_desc_video[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 3264,
			.vsize = 1836,
			.user_data_desc = VC_STAGGER_NE,
		},
	},
};

// mode 0: 1632*1224@30fps, normal preview
#define preview_mode_struct \
{\
	.frame_desc = frame_desc_prev,\
	.num_entries = ARRAY_SIZE(frame_desc_prev),\
	.mode_setting_table = sc820cs_preview_setting,\
	.mode_setting_len = ARRAY_SIZE(sc820cs_preview_setting),\
	.seamless_switch_group = PARAM_UNDEFINED,\
	.seamless_switch_mode_setting_table = PARAM_UNDEFINED,\
	.seamless_switch_mode_setting_len = PARAM_UNDEFINED,\
	.hdr_mode = HDR_NONE,\
	.raw_cnt = 1,\
	.exp_cnt = 1,\
	.pclk = 132000000,\
	.linelength = 1760,\
	.framelength = 2500,\
	.max_framerate = 300,\
	.mipi_pixel_rate = 264000000,\
	.readout_length = 0,\
	.read_margin = 5,\
	.framelength_step = 2,\
	.imgsensor_winsize_info = {\
		.full_w = 3264,\
		.full_h = 2448,\
		.x0_offset = 0,\
		.y0_offset = 0,\
		.w0_size = 3264,\
		.h0_size = 2448,\
		.scale_w = 1632,\
		.scale_h = 1224,\
		.x1_offset = 0,\
		.y1_offset = 0,\
		.w1_size = 1632,\
		.h1_size = 1224,\
		.x2_tg_offset = 0,\
		.y2_tg_offset = 0,\
		.w2_tg_size = 1632,\
		.h2_tg_size = 1224,\
	},\
	.pdaf_cap = FALSE,\
	.imgsensor_pd_info = PARAM_UNDEFINED,\
	.ae_binning_ratio = 4,\
	.fine_integ_line = 0,\
	.delay_frame = 2,\
}

// mode 0: 3264*2448@30fps, normal cap
#define cap_mode_struct \
{\
	.frame_desc = frame_desc_cap,\
	.num_entries = ARRAY_SIZE(frame_desc_cap),\
	.mode_setting_table = sc820cs_cap_setting,\
	.mode_setting_len = ARRAY_SIZE(sc820cs_cap_setting),\
	.seamless_switch_group = PARAM_UNDEFINED,\
	.seamless_switch_mode_setting_table = PARAM_UNDEFINED,\
	.seamless_switch_mode_setting_len = PARAM_UNDEFINED,\
	.hdr_mode = HDR_NONE,\
	.raw_cnt = 1,\
	.exp_cnt = 1,\
	.pclk = 132000000,\
	.linelength = 1760,\
	.framelength = 2500,\
	.max_framerate = 300,\
	.mipi_pixel_rate = 264000000,\
	.readout_length = 0,\
	.read_margin = 5,\
	.framelength_step = 2,\
	.imgsensor_winsize_info = {\
		.full_w = 3264,\
		.full_h = 2448,\
		.x0_offset = 0,\
		.y0_offset = 0,\
		.w0_size = 3264,\
		.h0_size = 2448,\
		.scale_w = 3264,\
		.scale_h = 2448,\
		.x1_offset = 0,\
		.y1_offset = 0,\
		.w1_size = 3264,\
		.h1_size = 2448,\
		.x2_tg_offset = 0,\
		.y2_tg_offset = 0,\
		.w2_tg_size = 3264,\
		.h2_tg_size = 2448,\
	},\
	.pdaf_cap = FALSE,\
	.imgsensor_pd_info = PARAM_UNDEFINED,\
	.ae_binning_ratio = 4,\
	.fine_integ_line = 0,\
	.delay_frame = 2,\
}

// mode 2: 3264*1836@30fps, normal video
#define video_mode_struct \
{\
	.frame_desc = frame_desc_video,\
	.num_entries = ARRAY_SIZE(frame_desc_video),\
	.mode_setting_table = sc820cs_video_setting,\
	.mode_setting_len = ARRAY_SIZE(sc820cs_video_setting),\
	.seamless_switch_group = PARAM_UNDEFINED,\
	.seamless_switch_mode_setting_table = PARAM_UNDEFINED,\
	.seamless_switch_mode_setting_len = PARAM_UNDEFINED,\
	.hdr_mode = HDR_NONE,\
	.raw_cnt = 1,\
	.exp_cnt = 1,\
	.pclk = 132000000,\
	.linelength = 1760,\
	.framelength = 2500,\
	.max_framerate = 300,\
	.mipi_pixel_rate = 264000000,\
	.readout_length = 0,\
	.read_margin = 5,\
	.framelength_step = 2,\
	.imgsensor_winsize_info = {\
		.full_w = 3264,\
		.full_h = 2448,\
		.x0_offset = 0,\
		.y0_offset = 306,\
		.w0_size = 3264,\
		.h0_size = 1836,\
		.scale_w = 3264,\
		.scale_h = 1836,\
		.x1_offset = 0,\
		.y1_offset = 0,\
		.w1_size = 3264,\
		.h1_size = 1836,\
		.x2_tg_offset = 0,\
		.y2_tg_offset = 0,\
		.w2_tg_size = 3264,\
		.h2_tg_size = 1836,\
	},\
	.pdaf_cap = FALSE,\
	.imgsensor_pd_info = PARAM_UNDEFINED,\
	.ae_binning_ratio = 4,\
	.fine_integ_line = 0,\
	.delay_frame = 2,\
}

static struct subdrv_mode_struct mode_struct[] = {
	preview_mode_struct,		//mode 0
	cap_mode_struct,		//mode 1
	video_mode_struct,		    //mode 2
};

static struct subdrv_static_ctx static_ctx = {
	.sensor_id = SC820CS_SENSOR_ID,
	.reg_addr_sensor_id = {0x3107, 0x3108},
	.i2c_addr_table = {0x6c,0x20,0x6d,0xFF}, // TBD
	.i2c_burst_write_support = FALSE,
	.i2c_transfer_data_type = I2C_DT_ADDR_16_DATA_8_SEPAR,
	.eeprom_info = eeprom_info,
	.eeprom_num = ARRAY_SIZE(eeprom_info),
	.resolution = {3264, 2448},
	.mirror = IMAGE_NORMAL, // TBD

	.mclk = 24,
	.isp_driving_current = ISP_DRIVING_6MA,
	.sensor_interface_type = SENSOR_INTERFACE_TYPE_MIPI,
	.mipi_sensor_type = MIPI_OPHY_NCSI2,
	.mipi_lane_num = SENSOR_MIPI_4_LANE,
	.ob_pedestal = 0x40,

	.sensor_output_dataformat = SENSOR_OUTPUT_FORMAT_RAW_B,
	.ana_gain_def = BASEGAIN * 4,
	.ana_gain_min = BASEGAIN * 1,
	.ana_gain_max = BASEGAIN * 16,
	.ana_gain_type = 5,
	.ana_gain_step = 32,
	.ana_gain_table = sc820cs_ana_gain_table,
	.ana_gain_table_size = sizeof(sc820cs_ana_gain_table),
	.min_gain_iso = 100,
	.exposure_def = 0x3D0,
	.exposure_min = 4,
	.exposure_max = 0xFFFF - 8,
	.exposure_step = 1,
	.exposure_margin = 8,

	.frame_length_max = 0xFFFF,
	.ae_effective_frame = 3,
	.frame_time_delay_frame = 2,
	.start_exposure_offset = 500000,

	.pdaf_type = PDAF_SUPPORT_NA,
	.hdr_type = HDR_SUPPORT_NA,
	.seamless_switch_support = FALSE,
	.temperature_support = FALSE,

	.g_temp = PARAM_UNDEFINED,
	.g_gain2reg = get_gain2reg,
	.s_gph = set_group_hold,

	.reg_addr_stream = 0x0100,
	.reg_addr_mirror_flip = PARAM_UNDEFINED, // TBD
	.reg_addr_exposure = {
		{0x3e00, 0x3e01, 0x3e02}
	},
	.long_exposure_support = FALSE,
	.reg_addr_exposure_lshift = PARAM_UNDEFINED,
	.reg_addr_ana_gain = {
		{0x3e08,0x3e07}
	},
	.reg_addr_frame_length = {0x320e, 0x320f},
	.reg_addr_temp_en = PARAM_UNDEFINED,
	.reg_addr_temp_read = PARAM_UNDEFINED,
	.reg_addr_auto_extend = PARAM_UNDEFINED,
	.reg_addr_frame_count = PARAM_UNDEFINED,

	.init_setting_table = sc820cs_init_setting,
	.init_setting_len = ARRAY_SIZE(sc820cs_init_setting),
	.mode = mode_struct,
	.sensor_mode_num = ARRAY_SIZE(mode_struct),
	.list = feature_control_list,
	.list_len = ARRAY_SIZE(feature_control_list),
	.chk_s_off_sta = 0,
	.chk_s_off_end = 0,

	//TBD
	.checksum_value = 0xf7375923,
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
	.get_csi_param = sc820cs_get_csi_param,
	.update_sof_cnt = common_update_sof_cnt,
	.vsync_notify = ultra_vsync_notify,
};

static struct subdrv_pw_seq_entry pw_seq[] = {
	{HW_ID_RST,    0,		1},
	{HW_ID_MCLK,   24,		1},
	{HW_ID_MCLK_DRIVING_CURRENT, 2, 1},
	{HW_ID_DOVDD,  1800000, 5},
	{HW_ID_DVDD1,   1,       9},
	{HW_ID_AVDD1,   1,       5},
	{HW_ID_RST,    1,		5},
};

const struct subdrv_entry sc820cs_mipi_raw_entry = {
	.name = "sc820cs_mipi_raw",
	.id = SC820CS_SENSOR_ID,
	.pw_seq = pw_seq,
	.pw_seq_cnt = ARRAY_SIZE(pw_seq),
	.ops = &ops,
};

static void set_group_hold(void *arg, u8 en)
{
	//struct subdrv_ctx *ctx = (struct subdrv_ctx *)arg;
	if (en) {
		//set_i2c_buffer(ctx, 0x0104, 0x01);
	} else {
		//set_i2c_buffer(ctx, 0x0104, 0x00);
	}
}

static u16 get_gain2reg(u32 gain)
{
	u16 reg_gain = (u16)gain;

	if (reg_gain < sc820cs_LY_SENSOR_BASE_GAIN)
		reg_gain = sc820cs_LY_SENSOR_BASE_GAIN;
	else if (reg_gain > sc820cs_LY_SENSOR_MAX_GAIN)
		reg_gain = sc820cs_LY_SENSOR_MAX_GAIN;

	return (u16)reg_gain;
}

static int sc820cs_set_gain(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	unsigned long long *tmp = (unsigned long long *)para;
	u16 gain = ((u16)*tmp);
	u16 reg_gain = 0;
	u16 tempreg_gain = 0;
	int index = 0;
	u16 gainparam[6][2] = {
		{1024,0x00},
		{2048,0x08},
		{4096,0x09},
		{8192,0x0b},
		{16384,0x0f},
		{32768,0x1f},
	};

	reg_gain = get_gain2reg(gain);
	for(index = 5;index >= 0;index--){
		if(reg_gain >= gainparam[index][0]){
			break;
		}
	}

	if(index < 0){
		index = 0;
	}

	subdrv_i2c_wr_u8(ctx, ctx->s_ctx.reg_addr_ana_gain[0].addr[0],gainparam[index][1]);
	tempreg_gain = reg_gain * sc820cs_LY_SENSOR_BASE_GAIN/gainparam[index][0];
	subdrv_i2c_wr_u8(ctx, ctx->s_ctx.reg_addr_ana_gain[0].addr[1],(tempreg_gain >> 3) & 0xff);

	return 0;
}

static int sc820cs_set_test_pattern(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	u32 mode = *((u32 *)para);

	if (mode != ctx->test_pattern)
		DRV_LOG(ctx, "mode(%u->%u)\n", ctx->test_pattern, mode);
	if (mode) {
		subdrv_i2c_wr_u8(ctx, 0x4501, 0xcc);
		subdrv_i2c_wr_u8(ctx, 0x3902, 0x85);
		subdrv_i2c_wr_u8(ctx, 0x3908, 0x00);
		subdrv_i2c_wr_u8(ctx, 0x3909, 0xff);
		subdrv_i2c_wr_u8(ctx, 0x390a, 0xff);
		subdrv_i2c_wr_u8(ctx, 0x391d, 0x18);
	}else{
		// subdrv_i2c_wr_u8(ctx, 0x0100, 0x00);
		subdrv_i2c_wr_u8(ctx, 0x4501, 0xc4);
		subdrv_i2c_wr_u8(ctx, 0x3902, 0xc0);
		subdrv_i2c_wr_u8(ctx, 0x3908, 0x41);
		subdrv_i2c_wr_u8(ctx, 0x3909, 0x00);
		subdrv_i2c_wr_u8(ctx, 0x390a, 0x00);
		subdrv_i2c_wr_u8(ctx, 0x391d, 0x01);
		// subdrv_i2c_wr_u8(ctx, 0x0100, 0x01);
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

static int sc820cs_set_shutter(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	int fine_integ_line = 0;
	u64 *feature_data = (u64 *)para;
	u32 shutter = *feature_data;
	u32 frame_length = *(feature_data + 1);

	bool gph = !ctx->is_seamless && (ctx->s_ctx.s_gph != NULL);

	ctx->frame_length = frame_length ? frame_length : ctx->min_frame_length;
	check_current_scenario_id_bound(ctx);
	/* check boundary of shutter */
	fine_integ_line = ctx->s_ctx.mode[ctx->current_scenario_id].fine_integ_line;
	shutter = FINE_INTEG_CONVERT(shutter, fine_integ_line);
	shutter = max_t(u64, shutter,
		(u64)ctx->s_ctx.mode[ctx->current_scenario_id].multi_exposure_shutter_range[0].min);
	shutter = min_t(u64, shutter,
		(u64)ctx->s_ctx.mode[ctx->current_scenario_id].multi_exposure_shutter_range[0].max);
	/* check boundary of framelength */
	ctx->frame_length = max((u32)shutter + ctx->s_ctx.exposure_margin, ctx->min_frame_length);
	ctx->frame_length = min(ctx->frame_length, ctx->s_ctx.frame_length_max);
	/* restore shutter */
	memset(ctx->exposure, 0, sizeof(ctx->exposure));
	ctx->exposure[0] = (u32) shutter;
	/* group hold start */
	if (gph)
		ctx->s_ctx.s_gph((void *)ctx, 1);
	/* enable auto extend */
	if (ctx->s_ctx.reg_addr_auto_extend)
		subdrv_i2c_wr_u8(ctx, ctx->s_ctx.reg_addr_auto_extend, 0x01);

	/* write framelength */
	if (set_auto_flicker(ctx, 0) || frame_length || !ctx->s_ctx.reg_addr_auto_extend)
	{
		subdrv_i2c_wr_u8(ctx,ctx->s_ctx.reg_addr_frame_length.addr[0],(ctx->frame_length >> 8) & 0xFF);
		subdrv_i2c_wr_u8(ctx,ctx->s_ctx.reg_addr_frame_length.addr[1],(ctx->frame_length) & 0xFF);
	}
	/* write shutter */
	set_long_exposure(ctx);

	ctx->exposure[0] = ctx->exposure[0] *2;

	subdrv_i2c_wr_u8(ctx,ctx->s_ctx.reg_addr_exposure[0].addr[0],(ctx->exposure[0] >> 12) & 0xFF);
	subdrv_i2c_wr_u8(ctx,ctx->s_ctx.reg_addr_exposure[0].addr[1],(ctx->exposure[0] >> 4)  & 0xFF);
	subdrv_i2c_wr_u8(ctx,ctx->s_ctx.reg_addr_exposure[0].addr[2],(ctx->exposure[0] << 4)  & 0xF0);

	DRV_LOG(ctx, "exp[0x%x], fll(input/output):%u/%u, flick_en:%d\n",
		ctx->exposure[0], frame_length, ctx->frame_length, ctx->autoflicker_en);
	if (!ctx->ae_ctrl_gph_en) {
		if (gph)
			ctx->s_ctx.s_gph((void *)ctx, 0);
		commit_i2c_buffer(ctx);
	}
	/* group hold end */

	return ERROR_NONE;
}

static int sc820cs_get_csi_param(struct subdrv_ctx *ctx,
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

