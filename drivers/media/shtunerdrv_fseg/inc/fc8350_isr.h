/*****************************************************************************
	Copyright(c) 2017 FCI Inc. All Rights Reserved

	File name : fc8350_isr.h

	Description : header of interrupt service routine

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

	History :
	----------------------------------------------------------------------
*******************************************************************************/
#ifndef __FC8350_ISR__
#define __FC8350_ISR__

#ifdef __cplusplus
extern "C" {
#endif

extern ulong fc8350_ac_user_data;
extern ulong fc8350_ts_user_data;
extern s32 (*fc8350_ac_callback)(ulong userdata
	, u8 bufid, u8 *data, s32 length);
extern s32 (*fc8350_ts_callback)(ulong userdata
	, u8 bufid, u8 *data, s32 length);
extern void fc8350_isr(HANDLE handle);

#ifdef __cplusplus
}
#endif
#endif /* __FC8350_ISR__ */

