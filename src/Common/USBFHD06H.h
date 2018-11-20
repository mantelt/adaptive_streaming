/*
 * USBFHD06H.h
 *
 *  Created on: Nov 20, 2018
 *      Author: Thomas Mantel (thomas.mantel@mavt.ethz.ch)
 */

#pragma once
#include <linux/uvcvideo.h>
#include <stdint.h>
#include <string>

#define XU_RERVISION_USR_ID			0x04
#define XU_RERVISION_USR_H264_CTRL	0x02
#define UVC_SET_CUR					UVC_CTRL_FLAG_SET_CUR;
#define UVC_GET_CUR					UVC_CTRL_FLAG_GET_CUR | UVC_CTRL_FLAG_GET_RES | UVC_CTRL_FLAG_RESTORE


class USBFHD06H {
public:
	USBFHD06H(std::string device);
	virtual ~USBFHD06H();

	bool set_bitrate(const uint& bitrate);
	bool get_bitrate(uint& bitrate);

private:
	std::string _device;

	int XU_Set_Cur(int fd, uint8_t xu_unit, uint8_t xu_selector, uint16_t xu_size, uint8_t *xu_data);
	int XU_Get_Cur(int fd, uint8_t xu_unit, uint8_t xu_selector, uint16_t xu_size, uint8_t *xu_data);
};

