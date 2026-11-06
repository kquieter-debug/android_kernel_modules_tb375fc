// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 MediaTek Inc.

/*****************************************************************************
 *
 * Filename:
 * ---------
 *   s5k4h7mipiraw_Sensor.c
 *
 * Project:
 * --------
 *   ALPS
 *
 * Description:
 * ------------
 *   Source code of Sensor driver
 *
 *
 *------------------------------------------------------------------------------
 * Upper this line, this part is controlled by CC/CQ. DO NOT MODIFY!!
 *============================================================================
 ****************************************************************************/
#include "s5k4h7mipiraw_Sensor.h"

#define PFX "s5k4h7_camera_sensor_0939"
#define LOG_INF(format, args...) pr_err(PFX "[%s] " format, __func__, ##args)
#define LOG_ERR(format, args...) pr_err(PFX "[%s] " format, __func__, ##args)
#define LOG_DEBUG(...) do { if ((DEBUG_LOG_EN)) LOG_INF(__VA_ARGS__); } while (0)


//static void set_group_hold(void *arg, u8 en);
static u16 get_gain2reg(u32 gain);
static int s5k4h7_set_test_pattern(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int s5k4h7_set_test_pattern_data(struct subdrv_ctx *ctx, u8 *para, u32 *len);
static int init_ctx(struct subdrv_ctx *ctx,	struct i2c_client *i2c_client, u8 i2c_write_id);
static void s5k4h7_sensor_init(struct subdrv_ctx *ctx);
static int open(struct subdrv_ctx *ctx);

/* STRUCT */

static struct subdrv_feature_control feature_control_list[] = {
	{SENSOR_FEATURE_SET_TEST_PATTERN, s5k4h7_set_test_pattern},
	{SENSOR_FEATURE_SET_TEST_PATTERN_DATA, s5k4h7_set_test_pattern_data},
};

static struct mtk_mbus_frame_desc_entry frame_desc_prev[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 0x0660,  // 1632
			.vsize = 0x04C8,  // 1224
		},
	},
};
static struct mtk_mbus_frame_desc_entry frame_desc_cap[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 0x0CC0,  // 3264
			.vsize = 0x0990,  // 2448
		},
	},
};
static struct mtk_mbus_frame_desc_entry frame_desc_vid[] = {
	{
		.bus.csi2 = {
			.channel = 0,
			.data_type = 0x2b,
			.hsize = 0x0CC0,  // 3264
			.vsize = 0x0990,  // 2448
		},
	},
};


static struct subdrv_mode_struct mode_struct[] = {
	{
		.frame_desc = frame_desc_prev,
		.num_entries = ARRAY_SIZE(frame_desc_prev),
		.mode_setting_table = addr_data_pair_preview,
		.mode_setting_len = ARRAY_SIZE(addr_data_pair_preview),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.raw_cnt = 1,
		.exp_cnt = 1,
		.pclk = 280000000,
		.linelength = 3688,
		.framelength = 2530,
		.max_framerate = 300,
		.mipi_pixel_rate = 280000000,
		.readout_length = 0,
		.read_margin = 0,
		.imgsensor_winsize_info = {
			.full_w = 3280,
			.full_h = 2464,
			.x0_offset = 8,
			.y0_offset = 8,
			.w0_size = 3264,
			.h0_size = 2448,
			.scale_w = 1632,
			.scale_h = 1224,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 1632,
			.h1_size = 1224,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 1632,
			.h2_tg_size = 1224,
		},
		.pdaf_cap = FALSE,
		.imgsensor_pd_info = PARAM_UNDEFINED,
		.ae_binning_ratio = 4,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = { 0 },
	},
	{
		.frame_desc = frame_desc_cap,
		.num_entries = ARRAY_SIZE(frame_desc_cap),
		.mode_setting_table = addr_data_pair_capture,
		.mode_setting_len = ARRAY_SIZE(addr_data_pair_capture),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.raw_cnt = 1,
		.exp_cnt = 1,
		.pclk = 280000000,
		.linelength = 3688,
		.framelength = 2530,
		.max_framerate = 300,
		.mipi_pixel_rate = 280000000,
		.readout_length = 0,
		.read_margin = 0,
		.imgsensor_winsize_info = {
			.full_w = 3280,
			.full_h = 2464,
			.x0_offset = 8,
			.y0_offset = 8,
			.w0_size = 3264,
			.h0_size = 2448,
			.scale_w = 3264,
			.scale_h = 2448,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 3264,
			.h1_size = 2448,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 3264,
			.h2_tg_size = 2448,
		},
		.pdaf_cap = FALSE,
		.imgsensor_pd_info = PARAM_UNDEFINED,
		.ae_binning_ratio = 4,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = { 0 },
	},
	{
		.frame_desc = frame_desc_vid,
		.num_entries = ARRAY_SIZE(frame_desc_vid),
		.mode_setting_table = addr_data_pair_normal_video,
		.mode_setting_len = ARRAY_SIZE(addr_data_pair_normal_video),
		.seamless_switch_group = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_table = PARAM_UNDEFINED,
		.seamless_switch_mode_setting_len = PARAM_UNDEFINED,
		.hdr_mode = HDR_NONE,
		.raw_cnt = 1,
		.exp_cnt = 1,
		.pclk = 280000000,
		.linelength = 3688,
		.framelength = 2530,
		.max_framerate = 300,
		.mipi_pixel_rate = 280000000,
		.readout_length = 0,
		.read_margin = 0,
		.imgsensor_winsize_info = {
			.full_w = 3280,
			.full_h = 2464,
			.x0_offset = 8,
			.y0_offset = 314,
			.w0_size = 3264,
			.h0_size = 1836,
			.scale_w = 3264,
			.scale_h = 1836,
			.x1_offset = 0,
			.y1_offset = 0,
			.w1_size = 3264,
			.h1_size = 1836,
			.x2_tg_offset = 0,
			.y2_tg_offset = 0,
			.w2_tg_size = 3264,
			.h2_tg_size = 1836,
		},
		.pdaf_cap = FALSE,
		.imgsensor_pd_info = PARAM_UNDEFINED,
		.ae_binning_ratio = 4,
		.fine_integ_line = 0,
		.delay_frame = 2,
		.csi_param = { 0 },
	},
};

static struct subdrv_static_ctx static_ctx = {
	.sensor_id = S5K4H7_SENSOR_ID,
	.reg_addr_sensor_id = {0x0000, 0x0001},
	.i2c_addr_table = {0x20, 0xFF},
	.i2c_burst_write_support = FALSE,
	.i2c_transfer_data_type = I2C_DT_ADDR_16_DATA_8,
	.eeprom_info = 0,
	.eeprom_num = 0,
	.resolution = {3280, 2464},
	.mirror = IMAGE_NORMAL,

	.mclk = 24,
	.isp_driving_current = ISP_DRIVING_6MA,
	.sensor_interface_type = SENSOR_INTERFACE_TYPE_MIPI,
	.mipi_sensor_type = MIPI_OPHY_NCSI2,
	.mipi_lane_num = SENSOR_MIPI_4_LANE,
	.ob_pedestal = 0x40,

	.sensor_output_dataformat = SENSOR_OUTPUT_FORMAT_RAW_Gr,
	.ana_gain_def = BASEGAIN * 4,
	.ana_gain_min = BASEGAIN * 1,
	.ana_gain_max = BASEGAIN * 16,
	.ana_gain_type = 2,
	.ana_gain_step = 256,
	.ana_gain_table = s5k4h7_ana_gain_table,
	.ana_gain_table_size = sizeof(s5k4h7_ana_gain_table),
	.min_gain_iso = 100,
	.exposure_def = 0x3D0,
	.exposure_min = 3,
	.exposure_max = 0xFFFF - 3,
	.exposure_step = 1,
	.exposure_margin = 3,
	//.saturation_info = &imgsensor_saturation_info_10bit,//???

	.frame_length_max = 0xFFFF,
	.ae_effective_frame = 2,
	.frame_time_delay_frame = 2,
	.start_exposure_offset = 500000,

	.pdaf_type = PDAF_SUPPORT_NA,
	.hdr_type = HDR_SUPPORT_NA,
	.seamless_switch_support = FALSE,
	.temperature_support = FALSE,
	.g_temp = PARAM_UNDEFINED,
	.g_gain2reg = get_gain2reg,
	//.s_gph = set_group_hold,

	.reg_addr_stream = 0x0100,
	.reg_addr_mirror_flip = PARAM_UNDEFINED,
	.reg_addr_exposure = {{0x0202, 0x0203},},
	.long_exposure_support = FALSE,
	.reg_addr_exposure_lshift = PARAM_UNDEFINED,
	.reg_addr_ana_gain = {{0x0204, 0x0205},},
	//.reg_addr_dig_gain = {{0x020e, 0x020f},},
	.reg_addr_frame_length = {0x0340, 0x0341},
	.reg_addr_temp_en = PARAM_UNDEFINED,
	.reg_addr_temp_read = PARAM_UNDEFINED,
	.reg_addr_auto_extend = PARAM_UNDEFINED,
	.reg_addr_frame_count = 0x0005,
	.init_setting_table = PARAM_UNDEFINED,
	.init_setting_len = PARAM_UNDEFINED,

	.mode = mode_struct,
	.sensor_mode_num = ARRAY_SIZE(mode_struct),
	.list = feature_control_list,
	.list_len = ARRAY_SIZE(feature_control_list),
	.chk_s_off_sta = 1,
	.chk_s_off_end = 0,
	.checksum_value = 0xffb1ec31,
};

static struct subdrv_ops ops = {
	.get_id = common_get_imgsensor_id,
	.init_ctx = init_ctx,
	.open = open,
	.get_info = common_get_info,
	.get_resolution = common_get_resolution,
	.control = common_control,
	.feature_control = common_feature_control,
	.close = common_close,
	.get_frame_desc = common_get_frame_desc,
	.get_temp = common_get_temp,
	.get_csi_param = common_get_csi_param,
	.update_sof_cnt = common_update_sof_cnt,
};

static struct subdrv_pw_seq_entry pw_seq[] = {
    {HW_ID_MCLK, 24, 1},
    {HW_ID_MCLK_DRIVING_CURRENT, 6, 1},
    {HW_ID_RST,  0, 1},
    {HW_ID_DVDD1, 1, 1},
    {HW_ID_AVDD1, 1, 1},
    {HW_ID_DOVDD, 1800000, 1},
    {HW_ID_RST, 1, 1},
};

const struct subdrv_entry s5k4h7_mipi_raw_entry = {
	.name = "s5k4h7_mipi_raw",
	.id = S5K4H7_SENSOR_ID,
	.pw_seq = pw_seq,
	.pw_seq_cnt = ARRAY_SIZE(pw_seq),
	.ops = &ops,
};

/*static void set_group_hold(void *arg, u8 en)
{
	struct subdrv_ctx *ctx = (struct subdrv_ctx *)arg;

	if (en)
		set_i2c_buffer(ctx, 0x0104, 0x01);
	else
		set_i2c_buffer(ctx, 0x0104, 0x00);
}*/

static u16 get_gain2reg(u32 gain)
{
	return gain * 32 / BASEGAIN;
}

static int s5k4h7_set_test_pattern(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	u32 mode = *((u32 *)para);	

	if (mode != ctx->test_pattern)
		DRV_LOG(ctx, "mode(%u->%u)\n", ctx->test_pattern, mode);
	if (mode) {
		subdrv_i2c_wr_u16(ctx, 0x0600, mode); /* 100% Color bar */
		subdrv_i2c_wr_u16(ctx, 0x00ce, 0x19); 
	} else {
		subdrv_i2c_wr_u16(ctx, 0x0600, 0x0000);
		subdrv_i2c_wr_u16(ctx, 0x00ce, 0x09); /* No pattern */
	}
	ctx->test_pattern = mode;

	return ERROR_NONE;
}

static int s5k4h7_set_test_pattern_data(struct subdrv_ctx *ctx, u8 *para, u32 *len)
{
	struct mtk_test_pattern_data *data = (struct mtk_test_pattern_data *)para;
	u16 R  = (data->Channel_R >> 22) & 0x3ff;
	u16 Gr = (data->Channel_Gr >> 22) & 0x3ff;
	u16 Gb = (data->Channel_Gb >> 22) & 0x3ff;
	u16 B  = (data->Channel_B >> 22) & 0x3ff;

	subdrv_i2c_wr_u16(ctx, 0x0602, Gr);
	subdrv_i2c_wr_u16(ctx, 0x0604, R);
	subdrv_i2c_wr_u16(ctx, 0x0606, B);
	subdrv_i2c_wr_u16(ctx, 0x0608, Gb);

	DRV_LOG(ctx, "mode(%u) R/Gr/Gb/B = 0x%04x/0x%04x/0x%04x/0x%04x\n",
			ctx->test_pattern, R, Gr, Gb, B);

	return 0;
}

static int init_ctx(struct subdrv_ctx *ctx,	struct i2c_client *i2c_client, u8 i2c_write_id)
{
	memcpy(&(ctx->s_ctx), &static_ctx, sizeof(struct subdrv_static_ctx));
	subdrv_ctx_init(ctx);
	ctx->i2c_client = i2c_client;
	ctx->i2c_write_id = i2c_write_id;

	return 0;
}

static void s5k4h7_sensor_init(struct subdrv_ctx *ctx)
{
	DRV_LOG(ctx, "E\n");
	subdrv_i2c_wr_u8(ctx, 0x0100, 0x00);
  	subdrv_i2c_wr_u8(ctx, 0x0100, 0x00);
  	subdrv_i2c_wr_u8(ctx, 0x0B05, 0x01);
  	subdrv_i2c_wr_u8(ctx, 0x3074, 0x06);
  	subdrv_i2c_wr_u8(ctx, 0x3075, 0x2F);
  	subdrv_i2c_wr_u8(ctx, 0x308A, 0x20);
  	subdrv_i2c_wr_u8(ctx, 0x308B, 0x08);
  	subdrv_i2c_wr_u8(ctx, 0x308C, 0x0B);
  	subdrv_i2c_wr_u8(ctx, 0x3081, 0x07);
  	subdrv_i2c_wr_u8(ctx, 0x307B, 0x85);
  	subdrv_i2c_wr_u8(ctx, 0x307A, 0x0A);
  	subdrv_i2c_wr_u8(ctx, 0x3079, 0x0A);
  	subdrv_i2c_wr_u8(ctx, 0x306E, 0x71);
  	subdrv_i2c_wr_u8(ctx, 0x306F, 0x28);
  	subdrv_i2c_wr_u8(ctx, 0x301F, 0x20);
  	subdrv_i2c_wr_u8(ctx, 0x306B, 0x9A);
  	subdrv_i2c_wr_u8(ctx, 0x3091, 0x1F);
  	subdrv_i2c_wr_u8(ctx, 0x30C4, 0x06);
  	subdrv_i2c_wr_u8(ctx, 0x3200, 0x09);
  	subdrv_i2c_wr_u8(ctx, 0x306A, 0x79);
  	subdrv_i2c_wr_u8(ctx, 0x30B0, 0xFF);
  	subdrv_i2c_wr_u8(ctx, 0x306D, 0x08);
  	subdrv_i2c_wr_u8(ctx, 0x3080, 0x00);
  	subdrv_i2c_wr_u8(ctx, 0x3929, 0x3F);
  	subdrv_i2c_wr_u8(ctx, 0x3084, 0x16);
  	subdrv_i2c_wr_u8(ctx, 0x3070, 0x0F);
  	subdrv_i2c_wr_u8(ctx, 0x3B45, 0x01);
  	subdrv_i2c_wr_u8(ctx, 0x30C2, 0x05);
  	subdrv_i2c_wr_u8(ctx, 0x3069, 0x87);
  	subdrv_i2c_wr_u8(ctx, 0x3924, 0x7F);
  	subdrv_i2c_wr_u8(ctx, 0x3925, 0xFD);
  	subdrv_i2c_wr_u8(ctx, 0x3C08, 0xFF);
  	subdrv_i2c_wr_u8(ctx, 0x3C09, 0xFF);
  	subdrv_i2c_wr_u8(ctx, 0x3C31, 0xFF);
  	subdrv_i2c_wr_u8(ctx, 0x3C32, 0xFF);
  	subdrv_i2c_wr_u8(ctx, 0X30CF, 0X00); //dgain enable
  	subdrv_i2c_wr_u8(ctx, 0x3400, 0x00); //00 lsc on , 01 lsc off
	subdrv_i2c_wr_u8(ctx, 0x392F, 0x01);
	subdrv_i2c_wr_u8(ctx, 0x3930, 0x80);
	DRV_LOG(ctx, "X\n");
}
static int open(struct subdrv_ctx *ctx)
{
	u32 sensor_id = 0;
	u32 scenario_id = 0;
	/*get sensor id*/
	if (common_get_imgsensor_id(ctx, &sensor_id) != ERROR_NONE)
	{
		return ERROR_SENSOR_CONNECT_FAIL;
	}
	/*initail setting*/
	s5k4h7_sensor_init(ctx);

	memset(ctx->exposure, 0, sizeof(ctx->exposure));
	memset(ctx->ana_gain, 0, sizeof(ctx->gain));
	ctx->exposure[0] = ctx->s_ctx.exposure_def;
	ctx->ana_gain[0] = ctx->s_ctx.ana_gain_def;
	ctx->current_scenario_id = scenario_id;
	ctx->pclk = ctx->s_ctx.mode[scenario_id].pclk;
	ctx->line_length = ctx->s_ctx.mode[scenario_id].linelength;
	ctx->frame_length = ctx->s_ctx.mode[scenario_id].framelength;
	ctx->current_fps = 10 * ctx->pclk / ctx->line_length / ctx->frame_length;
	ctx->readout_length = ctx->s_ctx.mode[scenario_id].readout_length;
	ctx->read_margin= ctx->s_ctx.mode[scenario_id].read_margin;
	ctx->min_frame_length = ctx->frame_length;
	ctx->autoflicker_en = FALSE;
	ctx->test_pattern = 0;
	ctx->ihdr_mode = 0;
	ctx->pdaf_mode = 0;
	ctx->hdr_mode = 0;
	ctx->extend_frame_length_en = 0;
	ctx->is_seamless = 0;
	ctx->fast_mode_on = 0;
	ctx->sof_cnt = 0;
	ctx->ref_sof_cnt = 0;
	ctx->is_streaming = 0;

	return ERROR_NONE;
}






