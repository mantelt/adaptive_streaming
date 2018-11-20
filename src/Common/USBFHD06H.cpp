/*
 * USBFHD06H.cpp
 *
 *  Created on: Nov 20, 2018
 *      Author: thomas
 */

#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/ioctl.h>
#include <unistd.h>
#include "USBFHD06H.h"

USBFHD06H::USBFHD06H(std::string device) :
 _device(device)
{
}


USBFHD06H::~USBFHD06H() {
}

bool USBFHD06H::set_bitrate(const uint& bitrate) {
	int fd = open(_device.c_str(), O_RDWR);
	if (fd < 0) {
		std::cerr << "could not open device " << _device << " to control bitrate!" << std::endl;
	}

	int err = 0;
	__u8 ctrldata[11]={0};

	//uvc_xu_control parmeters
	uint8_t xu_unit= 0;
	uint8_t xu_selector= 0;
	uint16_t xu_size= 11;
	uint8_t *xu_data= ctrldata;

	xu_unit = XU_RERVISION_USR_ID;
	xu_selector = XU_RERVISION_USR_H264_CTRL;

	// Switch command
	xu_data[0] = 0x9A;				// Tag
	xu_data[1] = 0x02;				// H264_BitRate

	if ((err=XU_Set_Cur(fd, xu_unit, xu_selector, xu_size, xu_data)) < 0)
	{
		std::cerr << "XU_H264_Set_BitRate ==> Switch cmd : ioctl(UVCIOC_CTRL_SET) FAILED (" << err << ")" << std::endl;
		close(fd);
		return false;
	}

	// Bit Rate = BitRate_Ctrl_Num*512*fps*8/1000 (Kbps)
	xu_data[0] = ((int)bitrate & 0x00FF0000)>>16;
	xu_data[1] = ((int)bitrate & 0x0000FF00)>>8;
	xu_data[2] = ((int)bitrate & 0x000000FF);

	if ((err=XU_Set_Cur(fd, xu_unit, xu_selector, xu_size, xu_data)) < 0)
	{
		std::cerr << "XU_H264_Set_BitRate ==> ioctl(UVCIOC_CTRL_SET) FAILED (" << err << ")" << std::endl;
		return false;
	}

	return true;
}

bool USBFHD06H::get_bitrate(uint& bitrate) {
	int fd = open(_device.c_str(), O_RDWR);
	if (fd < 0) {
		std::cerr << "could not open device " << _device << " to control bitrate!" << std::endl;
	}

	uint8_t ctrldata[11]={0};
	int err = 0;

	//uvc_xu_control parmeters
	uint8_t xu_unit= 0;
	uint8_t xu_selector= 0;
	uint16_t xu_size= 11;
	uint8_t *xu_data= ctrldata;


	xu_unit = XU_RERVISION_USR_ID;
	xu_selector = XU_RERVISION_USR_H264_CTRL;

	// switch command
	xu_data[0] = 0x9A;				// Tag
	xu_data[1] = 0x02;				// H264_BitRate
	if ((err=XU_Set_Cur(fd, xu_unit, xu_selector, xu_size, xu_data)) < 0)
	{
		std::cerr << "XU_H264_Get_BitRate ==> Switch cmd : ioctl(UVCIOC_CTRL_SET) FAILED (" << err << ")" << std::endl;
		return false;
	}

	// Get Bit rate ctrl number
	memset(xu_data, 0, xu_size);
	if ((err=XU_Get_Cur(fd, xu_unit, xu_selector, xu_size, xu_data)) < 0)
	{
		std::cerr << "XU_H264_Get_BitRate ==> ioctl(UVCIOC_CTRL_GET) FAILED (" << err << ")" << std::endl;
		return false;
	}

	int BitRate_CtrlNum =  ( xu_data[0]<<16 )| ( xu_data[1]<<8 )| (xu_data[2]) ;

	bitrate = BitRate_CtrlNum;

	return true;

}

int USBFHD06H::XU_Set_Cur(int fd, uint8_t xu_unit, uint8_t xu_selector, uint16_t xu_size, uint8_t *xu_data)
{
	int err=0;
	struct uvc_xu_control_query xctrl;
	xctrl.unit = xu_unit;
	xctrl.selector = xu_selector;
	xctrl.query = UVC_SET_CUR;
	xctrl.size = xu_size;
	xctrl.data = xu_data;
	err=ioctl(fd, UVCIOC_CTRL_QUERY, &xctrl);
	return err;
}

int USBFHD06H::XU_Get_Cur(int fd, uint8_t xu_unit, uint8_t xu_selector, uint16_t xu_size, uint8_t *xu_data)
{
	int err=0;
	struct uvc_xu_control_query xctrl;
	xctrl.unit = xu_unit;
	xctrl.selector = xu_selector;
	xctrl.query = UVC_GET_CUR;
	xctrl.size = xu_size;
	xctrl.data = xu_data;
	err=ioctl(fd, UVCIOC_CTRL_QUERY, &xctrl);
	return err;
}
