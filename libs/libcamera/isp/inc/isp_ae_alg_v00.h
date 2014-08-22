/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _ISP_AE_V00_H_
#define _ISP_AE_V00_H_
/*----------------------------------------------------------------------------*
*				Dependencies				*
*---------------------------------------------------------------------------*/
#include <sys/types.h>
/*---------------------------------------------------------------------------*
*				Compiler Flag				*
*---------------------------------------------------------------------------*/
#ifdef __cplusplus
extern "C"
{
#endif

/*---------------------------------------------------------------------------*
*				Micro Define				*
*----------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*
*				Data Structures 				*
*---------------------------------------------------------------------------*/

struct isp_ae_v00_init_param
{
	void* context_ptr;
};

struct isp_ae_v00_deinit_param
{
	uint32_t reserved;
};

struct isp_ae_v00_calc_param
{
	uint32_t cur_lum;
};

struct isp_ae_v00_calc_out_param
{
	int32_t cur_index;
	int32_t cur_lum;
	int32_t cur_ev;
	int32_t fase_end;
};

struct isp_ae_v00_flash_alg_param
{
	uint32_t prv_lum;
	uint32_t cur_lum;
	uint32_t prv_index;
	uint32_t cur_index;
	int32_t target_lum;
	int32_t target_zone;
};

struct isp_ae_v00_frame_info
{
	uint8_t* weight_ptr;
	uint32_t fix_fps;
	uint32_t frame_line;
	uint32_t line_time;
	uint32_t* e_ptr;
	uint16_t* g_ptr;
	int32_t max_index;
	int32_t min_index;
};

struct isp_ae_v00_context
{
	int32_t ev;
	uint32_t alg_mode;

	uint32_t cur_gain;
	uint32_t cur_exposure;
	uint32_t cur_dummy;
	int32_t cur_lum;
	int32_t target_lum;
	int32_t target_zone;
	uint32_t quick_mode;
	uint32_t anti_exposure;

	uint8_t* weight_ptr;
	uint32_t fix_fps;
	uint32_t frame_line;
	uint32_t line_time;
	uint32_t* e_ptr;
	uint16_t* g_ptr;
	int32_t max_index;
	int32_t min_index;

	int32_t cur_index;

	uint32_t flash_eb;
	struct isp_ae_v00_flash_alg_param flash;

	int32_t(*set_exposure)(uint32_t modult_id, uint32_t exposure, uint32_t dummy);
	int32_t(*set_gain)(uint32_t modult_id, uint32_t gain);
	int32_t(*ae_fast_callback)(uint32_t modult_id, int32_t eb);
	int32_t(*flash_calc)(uint32_t handler_id, struct isp_ae_v00_flash_alg_param* v00_flash_ptr);
	int32_t(*real_gain)(uint32_t gain);

};

uint32_t isp_ae_v00_init_context(uint32_t handler_id, void* param_ptr);
uint32_t isp_ae_v00_init(uint32_t handler_id, void* param_ptr);
uint32_t isp_ae_v00_deinit(uint32_t handler_id, void* param_ptr);
uint32_t isp_ae_v00_calculation(uint32_t handler_id, void* in_param_ptr, void* out_param_ptr);
uint32_t isp_ae_v00_change_expos_gain(uint32_t handler_id, uint32_t shutter, uint32_t dummy, uint32_t again, uint32_t dgain);
uint32_t isp_ae_v00_update_expos_gain(uint32_t handler_id);
int32_t isp_ae_v00_set_exposure(uint32_t handler_id);
int32_t isp_ae_v00_set_gain(uint32_t handler_id);

int32_t isp_ae_v00_flash(uint32_t handler_id, uint32_t eb);
int32_t isp_ae_v00_set_alg(uint32_t handler_id, uint32_t mode);
int32_t isp_ae_v00_set_ev(uint32_t handler_id, int32_t evv);
int32_t isp_ae_v00_set_index(uint32_t handler_id, uint32_t index);
int32_t isp_ae_v00_set_frame_info(uint32_t handler_id, struct isp_ae_v00_frame_info* param_ptr);

/*----------------------------------------------------------------------------*
*					Compiler Flag				*
*----------------------------------------------------------------------------*/
#ifdef	__cplusplus
}
#endif
/*-----------------------------------------------------------------------------*/
#endif
// End

