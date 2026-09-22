/**********************************************************************/
/*   ____  ____                                                       */
/*  /   /\/   /                                                       */
/* /___/  \  /                                                        */
/* \   \   \/                                                         */
/*  \   \        Copyright (c) 2003-2020 Xilinx, Inc.                 */
/*  /   /        All Right Reserved.                                  */
/* /---/   /\                                                         */
/* \   \  /  \                                                        */
/*  \___\/\___\                                                       */
/**********************************************************************/


/* NOTE: DO NOT EDIT. AUTOMATICALLY GENERATED FILE. CHANGES WILL BE LOST. */

#ifndef DPI_H
#define DPI_H
#ifdef __cplusplus
#define DPI_LINKER_DECL  extern "C" 
#else
#define DPI_LINKER_DECL
#endif

#include "svdpi.h"



/* Imported (by SV) function */
DPI_LINKER_DECL DPI_DLLESPEC 
 void dpi_c_reset_model(
);


/* Imported (by SV) function */
DPI_LINKER_DECL DPI_DLLESPEC 
 void dpi_c_process_order(
	const svLogicVecVal in_txn[SV_PACKED_DATA_NELEMS(256)] ,
	svLogicVecVal out_report[SV_PACKED_DATA_NELEMS(256)]);


/* Imported (by SV) function */
DPI_LINKER_DECL DPI_DLLESPEC 
 int dpi_c_get_best_bid(
);


/* Imported (by SV) function */
DPI_LINKER_DECL DPI_DLLESPEC 
 int dpi_c_get_best_ask(
);


/* Imported (by SV) function */
DPI_LINKER_DECL DPI_DLLESPEC 
 int dpi_c_get_free_count(
);


/* Imported (by SV) function */
DPI_LINKER_DECL DPI_DLLESPEC 
 int dpi_c_get_level_qty(
	char side ,
	int price);


/* Imported (by SV) function */
DPI_LINKER_DECL DPI_DLLESPEC 
 int dpi_c_is_level_active(
	char side ,
	int price);


#endif
