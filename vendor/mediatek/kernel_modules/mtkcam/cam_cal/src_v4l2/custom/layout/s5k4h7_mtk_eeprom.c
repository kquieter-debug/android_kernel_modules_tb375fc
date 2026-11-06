// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#define PFX "CAM_CAL"
#define pr_fmt(fmt) PFX "[%s] " fmt, __func__

#include <linux/kernel.h>
#include <linux/delay.h>
#include "cam_cal_list.h"
#include "eeprom_i2c_common_driver.h"
#include "eeprom_i2c_custom_driver.h"
#include "cam_cal_config.h"

#define CAM_CAL_LOG_ERR(format, args...) pr_err(PFX "[%s] " format, __func__, ##args)
#define CAM_CAL_LOG_INF(format, args...) pr_info(PFX "[%s] " format, __func__, ##args)
#define CAM_CAL_LOG_DBG(format, args...) pr_info(PFX "[%s] " format, __func__, ##args)

extern unsigned char module_info_s5k4h7[30];
extern bool otp_check_s5k4h7;
static 	unsigned char awb_info_s5k4h7[30] = {0};


#define AWBCFG_FLAG_ADDR 30

#define USHORT             unsigned short
#define BYTE               unsigned char

#define S5K4H7_READ_EEPROM_ADDR 0x20

/*PAGE 21 23 25 for  awb & module info*/
#define MODULE_AWB_INFO_GROUP_MAX 3
#define PAGE_21  21
#define PAGE_23  23
#define PAGE_25  25

#define MODULE_INFO_BASE_ADDR     0x0A05
#define MODULE_INFO_CHECKSUM_ADDR 0x0A22

#define AWB_INFO_BASE_ADDR     0x0A23
#define AWB_INFO_CHECKSUM_ADDR 0x0A40

int page_table[MODULE_AWB_INFO_GROUP_MAX] = { PAGE_21,PAGE_23,PAGE_25 };

#define DATA_FLAG_ADDR         0x0A04
#define PAGE_DATA_FLAG_OK      0x40 /*0100 0000*/

#define LSC_GROUP_FLAG_PAGE    27
#define LSC_INFO_GROUP1_FLAG_ADDR  0x0A04
#define LSC_INFO_GROUP2_FLAG_ADDR  0x0A05
#define LSC_INFO_GROUP_MAX     2

#define LSC_PAGE1_START        1
#define LSC_PAGE1_END          6

#define LSC_PAGE2_START        6
#define LSC_PAGE2_END          12

/*PAGE 1 ~ PAGE 21 FOR LSC DATA*/
#define LSC_DATA_FLAG_ADDR  0xA3D

/*LSC GROUP 1*/
#define LSC_GROUP1_FLAG      0x01
#define LSC_GROUP1_BASE_ADDR 0x0A04
#define LSC_GROUP1_END_ADDR  0x0A2B

/*LSC GROUP 2*/
#define LSC_GROUP2_FLAG      0x03
#define LSC_GROUP2_BASE_ADDR 0x0A2C
#define LSC_GROUP2_END_ADDR  0x0A13

/*LSC CHECK SUM*/
static BYTE lsc_sum = 0;

/*record check result when system up, avoid repeated check when sensor power on every time*/
static bool module_chk_ret = false;
static bool awb_chk_ret = false;
static bool lsc_chk_ret = false;

#define MODULE_INFO_SIZE 30
#define AWB_INFO_SIZE    30

#define DATA_SIZE        (MODULE_INFO_SIZE + AWB_INFO_SIZE)


unsigned int s5k4h7_do_single_lsc(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size, unsigned int *pGetSensorCalData);

unsigned int s5k4h7_do_module_version(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size, unsigned int *pGetSensorCalData);

unsigned int s5k4h7_do_2a_gain(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size, unsigned int *pGetSensorCalData);

unsigned int s5k4h7_layout_no_ck(struct EEPROM_DRV_FD_DATA *pdata,
				unsigned int sensorID);

static struct STRUCT_CALIBRATION_LAYOUT_STRUCT cal_layout_table = {
	0x00000001, 0x010b00ff, CAM_CAL_SINGLE_EEPROM_DATA,
	{
		{0x00000001, 0x00000000, 0x0000001E, s5k4h7_do_module_version},
		{0x00000000, 0x00000005, 0x00000002, do_part_number},
		{0x00000001, 0x00000047, 0x0000074C, s5k4h7_do_single_lsc},
		{0x00000001, 0x00000001, 0x0000001E, s5k4h7_do_2a_gain},
		{0x00000000, 0x00000763, 0x00000800, do_pdaf},
		{0x00000000, 0x00000FAE, 0x00000550, do_stereo_data},
		{0x00000000, 0x00000000, 0x00001600, do_dump_all},
		{0x00000000, 0x00000F80, 0x0000000A, do_lens_id}
	}
};

struct STRUCT_CAM_CAL_CONFIG_STRUCT s5k4h7_mtk_eeprom = {
	.name = "s5k4h7_mtk_eeprom",
	.check_layout_function = s5k4h7_layout_no_ck,
	.read_function = Common_read_region,
	.layout = &cal_layout_table,
	.sensor_id = S5K4H7_SENSOR_ID,
	.i2c_write_id = 0x20,
	.max_size = 0x4000,
	.enable_preload = 0,
	.preload_size = 0x0000,
	.has_stored_data = 0,
};

static void s5k4h7_read_end(struct EEPROM_DRV_FD_DATA *pdata)
{
    unsigned char page_addr_on = 0x04;
    unsigned char page_addr_off = 0x00;

    Common_write_region(pdata->pdrv->pi2c_client, 0x0A00, &page_addr_on,1);
	Common_write_region(pdata->pdrv->pi2c_client, 0x0A00, &page_addr_off,1);
}


static bool s5k4h7_read_pagenum(struct EEPROM_DRV_FD_DATA *pdata,unsigned char num)
{
	BYTE data = 0;
	char i = 0;
	unsigned char page = num;
	unsigned char stream_on = 0x01;
	unsigned char page_addr_on = 0x01;
	unsigned char page_addr_off = 0x00;

	CAM_CAL_LOG_DBG("start %d\n",num);

	Common_write_region(pdata->pdrv->pi2c_client, 0x0100, &stream_on,1);
	mdelay(50);
	Common_write_region(pdata->pdrv->pi2c_client, 0x0A02, &page,1);
	Common_write_region(pdata->pdrv->pi2c_client, 0x0A00, &page_addr_on,1);

	for(i = 0;i < 100; i++)
	{
		mdelay(1);
		Common_read_region(pdata->pdrv->pi2c_client, 0x0A01, &data,1);
		if(data == 0x01)
		{
			break;
		}
	}
	if(i == 100)
	{
		Common_write_region(pdata->pdrv->pi2c_client,0xA00,&page_addr_off,1);
		return false;
	}
	CAM_CAL_LOG_DBG("end ok 0x%x\n",data);
	return true;
}

static bool s5k4h7_read_groupnum(struct EEPROM_DRV_FD_DATA *pdata)
{
	bool ret = false;
	int page = 0;
	BYTE data = 0;

	for(page = 0; page < MODULE_AWB_INFO_GROUP_MAX; page++)
	{
		ret = s5k4h7_read_pagenum(pdata, page_table[page]);
		if(ret)
		{
			Common_read_region(pdata->pdrv->pi2c_client, DATA_FLAG_ADDR,&data,1);
		}

		if(data == PAGE_DATA_FLAG_OK)
		{
			break;
		}
	}

	if(page == MODULE_AWB_INFO_GROUP_MAX)
	{
		return false;
	}
	CAM_CAL_LOG_DBG("end ok 0x%x\n",data);
	return true;
}

static bool s5k4h7_read_module_info(struct EEPROM_DRV_FD_DATA *pdata,unsigned char *dstdata,unsigned int block_size)
{
	int i = 0;
	int check_sum = 0;

	Common_read_region(pdata->pdrv->pi2c_client, MODULE_INFO_BASE_ADDR,dstdata,block_size);

	/*module check sum*/
	for(i = 0;i < MODULE_INFO_SIZE - 1; i++)
	{
		check_sum += dstdata[i];
	}

	check_sum = check_sum % 0xff + 1;

	if(check_sum != dstdata[MODULE_INFO_SIZE-1])
	{
		CAM_CAL_LOG_ERR("0x%x != 0x%x\n",check_sum,dstdata[MODULE_INFO_SIZE-1]);
		return false;
	}

	CAM_CAL_LOG_ERR("0x%x == 0x%x\n",check_sum,dstdata[MODULE_INFO_SIZE-1]);

	return true;
}

static bool s5k4h7_read_awb_info(struct EEPROM_DRV_FD_DATA *pdata,unsigned char *dstdata,unsigned int block_size)
{
	int i = 0;
	int check_sum = 0;

	Common_read_region(pdata->pdrv->pi2c_client, AWB_INFO_BASE_ADDR,dstdata,block_size);

	/*awb check sum*/
	for(i = 1;i < AWB_INFO_SIZE - 1; i++)
	{
		check_sum += dstdata[i];
	}

	check_sum = check_sum % 0xff + 1;

	if(check_sum != dstdata[AWB_INFO_SIZE -1])
	{
		CAM_CAL_LOG_ERR("0x%x != 0x%x\n",check_sum,dstdata[AWB_INFO_SIZE -1]);
		return false;
	}

	CAM_CAL_LOG_ERR("0x%x == 0x%x\n",check_sum,dstdata[AWB_INFO_SIZE -1]);
	return true;
}

static unsigned int read_module_info(struct EEPROM_DRV_FD_DATA *pdata,unsigned char *data,unsigned int block_size)
{
	bool group_flag = false,ret = false;
	unsigned short dts_addr;

	CAM_CAL_LOG_DBG("start");

	mutex_lock(&pdata->pdrv->eeprom_mutex);
	dts_addr = pdata->pdrv->pi2c_client->addr;
	pdata->pdrv->pi2c_client->addr = (s5k4h7_mtk_eeprom.i2c_write_id >> 1);

	group_flag = s5k4h7_read_groupnum(pdata);
	if(!group_flag)
	{
		CAM_CAL_LOG_ERR("s5k4h7_read_groupnum err\n");
		s5k4h7_read_end(pdata);
		pdata->pdrv->pi2c_client->addr = dts_addr;
		mutex_unlock(&pdata->pdrv->eeprom_mutex);
		return false;
	}

	ret = s5k4h7_read_module_info(pdata,data,block_size);
	s5k4h7_read_end(pdata);
	pdata->pdrv->pi2c_client->addr = dts_addr;
	mutex_unlock(&pdata->pdrv->eeprom_mutex);

	return ret;
}

static unsigned int read_awb_info(struct EEPROM_DRV_FD_DATA *pdata,unsigned char *data,unsigned int block_size)
{
	bool group_flag = false,ret = false;
	unsigned short dts_addr;

	CAM_CAL_LOG_DBG("start");

	mutex_lock(&pdata->pdrv->eeprom_mutex);
	dts_addr = pdata->pdrv->pi2c_client->addr;
	pdata->pdrv->pi2c_client->addr = (s5k4h7_mtk_eeprom.i2c_write_id >> 1);

	group_flag = s5k4h7_read_groupnum(pdata);
	if(!group_flag)
	{
		CAM_CAL_LOG_ERR("s5k4h7_read_groupnum err\n");
		s5k4h7_read_end(pdata);
		pdata->pdrv->pi2c_client->addr = dts_addr;
		mutex_unlock(&pdata->pdrv->eeprom_mutex);
		return false;
	}

	ret = s5k4h7_read_awb_info(pdata,data,block_size);
	s5k4h7_read_end(pdata);
	pdata->pdrv->pi2c_client->addr = dts_addr;
	mutex_unlock(&pdata->pdrv->eeprom_mutex);

	return ret;
}


static void s5k4h7_enable_lsc(struct EEPROM_DRV_FD_DATA *pdata)
{
	unsigned char d1 = 0x00;
	unsigned char d2 = 0x01;
	CAM_CAL_LOG_DBG("start");
	Common_write_region(pdata->pdrv->pi2c_client,0x3400,&d1,1);
	Common_write_region(pdata->pdrv->pi2c_client,0x0B00,&d2,1);
}

static int s5k4h7_read_lsc_groupnum(struct EEPROM_DRV_FD_DATA *pdata)
{
	int group = 0;
	BYTE data = 0;
	bool ret = false;
	ret = s5k4h7_read_pagenum(pdata, LSC_GROUP_FLAG_PAGE);
	if(!ret)
	{
		return -1;
	}

	Common_read_region(pdata->pdrv->pi2c_client, LSC_INFO_GROUP1_FLAG_ADDR,&data,1);
	if(data == PAGE_DATA_FLAG_OK)
	{
		Common_read_region(pdata->pdrv->pi2c_client, 0x0A06,&data,1);
		lsc_sum = data;
		group = 1;
		return group;
	}

	Common_read_region(pdata->pdrv->pi2c_client, LSC_INFO_GROUP2_FLAG_ADDR,&data,1);
	if(data == PAGE_DATA_FLAG_OK)
	{
		Common_read_region(pdata->pdrv->pi2c_client, 0x0A07,&data,1);
		lsc_sum = data;
		group = 2;
		return group;
	}

	return -1;
}

static int s5k4h7_read_lsc_info(struct EEPROM_DRV_FD_DATA *pdata,unsigned int block_size)
{
	bool ret = false;
	int group = 0, i = 0,j = 0,sum=0,offset = 0;
	int page_start = 0;
	unsigned char data_lsc[360] = {0};
	CAM_CAL_LOG_DBG("start");
	group = s5k4h7_read_lsc_groupnum(pdata);
	if(group < 0 || group > LSC_INFO_GROUP_MAX)
	{
		CAM_CAL_LOG_DBG("group %d",group);
		return false;
	}
	CAM_CAL_LOG_DBG("group %d",group);
	if(group == 1)
	{
		for(page_start = LSC_PAGE1_START;page_start <= LSC_PAGE1_END;page_start++)
		{
			ret = s5k4h7_read_pagenum(pdata, page_start);
			if(!ret)
			{
				return false;
			}
			if(page_start == LSC_PAGE1_END)
			{
				Common_read_region(pdata->pdrv->pi2c_client, 0x0A04,&data_lsc[320],40);
			}else{
				offset = (page_start-1) * 64;
				Common_read_region(pdata->pdrv->pi2c_client, 0x0A04,&data_lsc[offset],64);
			}
		}
	}

	if(group == 2)
	{
		for(page_start = LSC_PAGE2_START,i = -1 ;page_start <= LSC_PAGE2_END;page_start++,i++)
		{
			ret = s5k4h7_read_pagenum(pdata, page_start);
			if(!ret)
			{
				return false;
			}
			if(page_start == LSC_PAGE2_START)
			{
				Common_read_region(pdata->pdrv->pi2c_client, 0x0A2C,&data_lsc[0],24);
			}else if(page_start == LSC_PAGE2_END){
				Common_read_region(pdata->pdrv->pi2c_client, 0x0A04,&data_lsc[344],16);
			}else{
				offset = i * 64;
				Common_read_region(pdata->pdrv->pi2c_client, 0x0A04,&data_lsc[offset+24],64);
			}
		}
	}

	for(j = 0; j < 360; j++)
	{
		sum += data_lsc[j];
	}

	sum = sum % 0xff + 1;

	if(sum != lsc_sum)
	{
		CAM_CAL_LOG_DBG("check sum %x != %x",sum,lsc_sum);
		return false;
	}

	s5k4h7_enable_lsc(pdata);
	CAM_CAL_LOG_ERR("check sum %x == %x",sum,lsc_sum);
	return true;
}

static unsigned int read_lsc_info(struct EEPROM_DRV_FD_DATA *pdata,unsigned int block_size)
{
	bool ret = false;
	unsigned short dts_addr;

	CAM_CAL_LOG_DBG("start");

	mutex_lock(&pdata->pdrv->eeprom_mutex);
	dts_addr = pdata->pdrv->pi2c_client->addr;
	pdata->pdrv->pi2c_client->addr = (s5k4h7_mtk_eeprom.i2c_write_id >> 1);

	ret = s5k4h7_read_lsc_info(pdata,block_size);
	if(!ret)
	{
		CAM_CAL_LOG_ERR("s5k4h7_read_lsc_info err\n");
		s5k4h7_read_end(pdata);
		pdata->pdrv->pi2c_client->addr = dts_addr;
		mutex_unlock(&pdata->pdrv->eeprom_mutex);
		return false;
	}

	s5k4h7_read_end(pdata);
	pdata->pdrv->pi2c_client->addr = dts_addr;
	mutex_unlock(&pdata->pdrv->eeprom_mutex);

	return true;
}

unsigned int s5k4h7_layout_no_ck(struct EEPROM_DRV_FD_DATA *pdata,
				unsigned int sensorID)
{
	CAM_CAL_LOG_DBG("sensor_id=%x, module_chk_ret %d, awb_chk_ret %d, lsc_chk_ret %d\n", sensorID, module_chk_ret, awb_chk_ret, lsc_chk_ret);
	if(sensorID == 0x487b) {
		//s5k4h7 OTP check
		module_chk_ret  =  read_module_info(pdata, module_info_s5k4h7, 30);
		awb_chk_ret = read_awb_info(pdata, awb_info_s5k4h7, 30);
		lsc_chk_ret = read_lsc_info(pdata, 360);

		if (module_chk_ret == 1 && awb_chk_ret == 1 && lsc_chk_ret == 1) {
			otp_check_s5k4h7 = 1;
		} else {
			CAM_CAL_LOG_ERR("OTP CHECK ERR! Please check module hardware! module_chk_ret=%x, awb_chk_ret %d, lsc_chk_ret %d\n", module_chk_ret, awb_chk_ret, lsc_chk_ret);
		}
		return CAM_CAL_ERR_NO_ERR;
	}
	return CAM_CAL_ERR_NO_DEVICE;
}

unsigned int s5k4h7_do_module_version(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size, unsigned int *pGetSensorCalData)
{
	struct STRUCT_CAM_CAL_DATA_STRUCT *pCamCalData =
				(struct STRUCT_CAM_CAL_DATA_STRUCT *)pGetSensorCalData;
	unsigned int err = CamCalReturnErr[pCamCalData->Command];
	unsigned int size_limit = 30;   //30
	bool ret = false;

	CAM_CAL_LOG_DBG("sensor_id=%x start_addr=%u block_size=%u\n", pCamCalData->sensorID, start_addr, block_size);
	if (block_size > size_limit) {
		CAM_CAL_LOG_ERR("module version size can't larger than %u\n", size_limit);
		return err;
	}

	if (module_chk_ret) {
		CAM_CAL_LOG_DBG("module_chk_ret ok when system on, will not call read_module_info() this time\n");
	} else {
		ret = read_module_info(pdata, module_info_s5k4h7, block_size);
		if(!ret)
		{
			return err;
		}
	}

	CAM_CAL_LOG_DBG("======================Module Info==================\n");
	CAM_CAL_LOG_DBG("[Module House ID] = 0x%x\n", module_info_s5k4h7[0]);
	CAM_CAL_LOG_DBG("[Part Number] = %d:%d:%d \n", module_info_s5k4h7[1], module_info_s5k4h7[2], module_info_s5k4h7[3]);
	CAM_CAL_LOG_DBG("[Sensor ID] = 0x%x\n", module_info_s5k4h7[4]);
	CAM_CAL_LOG_DBG("[LensID/VCMID/VCMDRIVE ID] = 0x%x/0x%x/0x%x \n", module_info_s5k4h7[21], module_info_s5k4h7[22], module_info_s5k4h7[23]);
	CAM_CAL_LOG_DBG("[YY/MM] = %d/%d \n", module_info_s5k4h7[24], module_info_s5k4h7[25]);
	CAM_CAL_LOG_DBG("[Phase] = %d \n", module_info_s5k4h7[26]);
	CAM_CAL_LOG_DBG("[Mirror/Flip Status] = %d \n", module_info_s5k4h7[27]);
	CAM_CAL_LOG_DBG("[IR filter ID] = %d \n", module_info_s5k4h7[28]);
	CAM_CAL_LOG_DBG("======================Module Info==================\n");

	return CAM_CAL_ERR_NO_ERR;
}


/***********************************************************************************
 * Function : To read 2A information. Please put your AWB+AF data function, here.
 ***********************************************************************************/

unsigned int s5k4h7_do_2a_gain(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size, unsigned int *pGetSensorCalData)
{
	struct STRUCT_CAM_CAL_DATA_STRUCT *pCamCalData =
				(struct STRUCT_CAM_CAL_DATA_STRUCT *)pGetSensorCalData;
	int read_data_size;
	unsigned int err = CamCalReturnErr[pCamCalData->Command];

	unsigned char AWBAFConfig = 0;
	unsigned char AFConfig = 0;
	unsigned char AWBConfig = 0;
	unsigned short AFInf = 0, AFMacro = 0;
	int tempMax = 0;
	int CalR = 1, CalGr = 1, CalGb = 1, CalG = 1, CalB = 1;
	int FacR = 1, FacGr = 1, FacGb = 1, FacG = 1, FacB = 1;
	bool ret = false;
	CAM_CAL_LOG_DBG("sensor_id=%x start_addr=%u block_size=%u\n", pCamCalData->sensorID, start_addr, block_size);
	memset((void *)&pCamCalData->Single2A, 0, sizeof(struct STRUCT_CAM_CAL_SINGLE_2A_STRUCT));
	/* Check rule */
	if (pCamCalData->DataVer >= CAM_CAL_TYPE_NUM) {
		err = CAM_CAL_ERR_NO_DEVICE;
		CAM_CAL_LOG_ERR("Read Failed\n");
		show_cmd_error_log(pCamCalData->Command);
		return err;
	}

	if (awb_chk_ret) {
		CAM_CAL_LOG_DBG("awb_chk_ret ok when system on, will not call read_awb_info() this time\n");
	} else {
		ret = read_awb_info(pdata, awb_info_s5k4h7, block_size);
		if(!ret)
		{
			return err;
		}
	}

	AWBConfig = (awb_info_s5k4h7[0] >> 6);
	if (AWBConfig > 0) {
		err = CAM_CAL_ERR_NO_ERR;
	} else {
		pCamCalData->Single2A.S2aBitEn = CAM_CAL_NONE_BITEN;
		CAM_CAL_LOG_ERR("Read Failed\n");
		show_cmd_error_log(pCamCalData->Command);
	}

	AWBAFConfig = AWBConfig | AFConfig;
	pCamCalData->Single2A.S2aVer = 0x01;
	pCamCalData->Single2A.S2aBitEn = (0x03 & AWBAFConfig);
	CAM_CAL_LOG_DBG("S2aBitEn=0x%02x", pCamCalData->Single2A.S2aBitEn);
	/* AWB Calibration Data*/
	if (0x1 & AWBAFConfig) {
		/* AWB Unit Gain (5100K) */
		CAM_CAL_LOG_ERR("5100K AWB\n");
		pCamCalData->Single2A.S2aAwb.rGainSetNum = 0;

		CalR = (awb_info_s5k4h7[13] << 8) | awb_info_s5k4h7[14];
		CalGr = (awb_info_s5k4h7[15] << 8) | awb_info_s5k4h7[16];
		CalGb = (awb_info_s5k4h7[17] << 8) | awb_info_s5k4h7[18];
		CalG  = ((CalGr + CalGb) + 1) >> 1;
		CalB = (awb_info_s5k4h7[19] << 8) | awb_info_s5k4h7[20];
		if (CalR > CalG){
			/* R > G */
			if (CalR > CalB){
				tempMax = CalR;
			} else {
				tempMax = CalB;
			}
		} else {
			/* G > R */
			if (CalG > CalB){
				tempMax = CalG;
			} else {
				tempMax = CalB;
			}
		}
		CAM_CAL_LOG_ERR(
			"UnitR:%d, UnitG:%d, UnitB:%d, New Unit Max=%d",
			CalR, CalG, CalB, tempMax);
		err = CAM_CAL_ERR_NO_ERR;

		if (CalR    != 0x00000000 &&
			CalG    != 0x00000000 &&
			CalB    != 0x00000000) {
			pCamCalData->Single2A.S2aAwb.rGainSetNum++;
			pCamCalData->Single2A.S2aAwb.rUnitGainu4R =
					(unsigned int)((tempMax * 512 + (CalR >> 1)) / CalR);
			pCamCalData->Single2A.S2aAwb.rUnitGainu4G =
					(unsigned int)((tempMax * 512 + (CalG >> 1)) / CalG);
			pCamCalData->Single2A.S2aAwb.rUnitGainu4B =
					(unsigned int)((tempMax * 512 + (CalB >> 1)) / CalB);
		} else {
			CAM_CAL_LOG_ERR(
			"There are something wrong on EEPROM, plz contact module vendor!!\n");
		}

		/* AWB Golden Gain (5100K) */
		FacR = (awb_info_s5k4h7[21] << 8) | awb_info_s5k4h7[22];
		FacGr = (awb_info_s5k4h7[23] << 8) | awb_info_s5k4h7[24];
		FacGb = (awb_info_s5k4h7[25] << 8) | awb_info_s5k4h7[26];
		FacG  = ((FacGr + FacGb) + 1) >> 1;
		FacB = (awb_info_s5k4h7[27] << 8) | awb_info_s5k4h7[28];
		if (FacR > FacG) {
			if (FacR > FacB)
				tempMax = FacR;
			else
				tempMax = FacB;
		 } else {
			if (FacG > FacB){
				tempMax = FacG;
			} else {
				tempMax = FacB;
			}
		 }
		CAM_CAL_LOG_ERR(
			"GoldenR:%d, GoldenG:%d, GoldenB:%d, New Golden Max=%d",
			FacR, FacG, FacB, tempMax);
		err = CAM_CAL_ERR_NO_ERR;


		if (FacR    != 0x00000000 &&
			FacG    != 0x00000000 &&
			FacB    != 0x00000000)	{
			pCamCalData->Single2A.S2aAwb.rGoldGainu4R =
					(unsigned int)((tempMax * 512 + (FacR >> 1)) / FacR);
			pCamCalData->Single2A.S2aAwb.rGoldGainu4G =
					(unsigned int)((tempMax * 512 + (FacG >> 1)) / FacG);
			pCamCalData->Single2A.S2aAwb.rGoldGainu4B =
					(unsigned int)((tempMax * 512 + (FacB >> 1)) / FacB);
		} else {
			CAM_CAL_LOG_ERR(
			"There are something wrong on EEPROM, plz contact module vendor!!\n");
		}

		/* Set AWB to 3A Layer */
		pCamCalData->Single2A.S2aAwb.rValueR   = CalR;
		pCamCalData->Single2A.S2aAwb.rValueGr  = CalGr;
		pCamCalData->Single2A.S2aAwb.rValueGb  = CalGb;
		pCamCalData->Single2A.S2aAwb.rValueB   = CalB;
		pCamCalData->Single2A.S2aAwb.rGoldenR  = FacR;
		pCamCalData->Single2A.S2aAwb.rGoldenGr = FacGr;
		pCamCalData->Single2A.S2aAwb.rGoldenGb = FacGb;
		pCamCalData->Single2A.S2aAwb.rGoldenB  = FacB;

		CAM_CAL_LOG_DBG("======================AWB CAM_CAL==================\n");
		CAM_CAL_LOG_DBG("[rCalGain.u4R] = %d\n", pCamCalData->Single2A.S2aAwb.rUnitGainu4R);
		CAM_CAL_LOG_DBG("[rCalGain.u4G] = %d\n", pCamCalData->Single2A.S2aAwb.rUnitGainu4G);
		CAM_CAL_LOG_DBG("[rCalGain.u4B] = %d\n", pCamCalData->Single2A.S2aAwb.rUnitGainu4B);
		CAM_CAL_LOG_DBG("[rFacGain.u4R] = %d\n", pCamCalData->Single2A.S2aAwb.rGoldGainu4R);
		CAM_CAL_LOG_DBG("[rFacGain.u4G] = %d\n", pCamCalData->Single2A.S2aAwb.rGoldGainu4G);
		CAM_CAL_LOG_DBG("[rFacGain.u4B] = %d\n", pCamCalData->Single2A.S2aAwb.rGoldGainu4B);
		CAM_CAL_LOG_DBG("======================AWB CAM_CAL==================\n");
	}

	/* AF Calibration Data*/
	if (0x2 & AWBAFConfig) {
		read_data_size = read_data(pdata, pCamCalData->sensorID, pCamCalData->deviceID,
				start_addr + 31, 2, (unsigned char *)&awb_info_s5k4h7);
		if (read_data_size > 0)
			err = CAM_CAL_ERR_NO_ERR;
		else {
			pCamCalData->Single2A.S2aBitEn = CAM_CAL_NONE_BITEN;
			CAM_CAL_LOG_ERR("Read Failed\n");
			show_cmd_error_log(pCamCalData->Command);
		}
		AFInf = (awb_info_s5k4h7[0] << 8) | awb_info_s5k4h7[1];
		read_data_size = read_data(pdata, pCamCalData->sensorID, pCamCalData->deviceID,
				start_addr + 33, 2, (unsigned char *)&awb_info_s5k4h7);
		if (read_data_size > 0)
			err = CAM_CAL_ERR_NO_ERR;
		else {
			pCamCalData->Single2A.S2aBitEn = CAM_CAL_NONE_BITEN;
			CAM_CAL_LOG_ERR("Read Failed\n");
			show_cmd_error_log(pCamCalData->Command);
		}
		AFMacro = (awb_info_s5k4h7[0] << 8) | awb_info_s5k4h7[1];
		pCamCalData->Single2A.S2aAf[0] = AFInf;
		pCamCalData->Single2A.S2aAf[1] = AFMacro;

		////Only AF Gathering <////
		CAM_CAL_LOG_DBG("======================AF CAM_CAL==================\n");
		CAM_CAL_LOG_DBG("[AFInf] = %d\n", AFInf);
		CAM_CAL_LOG_DBG("[AFMacro] = %d\n", AFMacro);
		CAM_CAL_LOG_DBG("======================AF CAM_CAL==================\n");
	}
	return err;
}

/***********************************************************************************
 * Function : To read LSC Table
 ***********************************************************************************/
unsigned int s5k4h7_do_single_lsc(struct EEPROM_DRV_FD_DATA *pdata,
		unsigned int start_addr, unsigned int block_size, unsigned int *pGetSensorCalData)
{
	struct STRUCT_CAM_CAL_DATA_STRUCT *pCamCalData =
				(struct STRUCT_CAM_CAL_DATA_STRUCT *)pGetSensorCalData;
	unsigned int err = CamCalReturnErr[pCamCalData->Command];
	bool ret = false;
	lsc_sum = 0;
	pCamCalData->SingleLsc.LscTable.MtkLcsData.MtkLscType = 1;//sensor
	pCamCalData->SingleLsc.LscTable.MtkLcsData.PixId = 2; //0,1,2,3: B,Gb,Gr,R

	CAM_CAL_LOG_DBG("sensor_id=%x start_addr=%u block_size=%u\n", pCamCalData->sensorID, start_addr, block_size);
	if (lsc_chk_ret) {
		CAM_CAL_LOG_DBG("lsc_chk_ret ok when system on, will not call read_lsc_info() this time\n");
	} else {
		ret = read_lsc_info(pdata,block_size);
		if(!ret)
		{
			return err;
		}
	}
	return CAM_CAL_ERR_NO_ERR;
}

