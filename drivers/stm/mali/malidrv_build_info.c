/***********************************************************************
 *
 * File: malidrv_build_info.c
 * Copyright (c) 2010 STMicroelectronics Limited.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
\***********************************************************************/
char *__malidrv_build_info(void)
{ return "malidrv:"
        " SOC=" MALIDRV_SOC
        " KERNELDIR=" MALIDRV_KERNELDIR
        " USING_MMU=" MALIDRV_USING_MMU
        " USING_UMP=" MALIDRV_USING_UMP
        " USING_MALI400=" MALIDRV_USING_MALI400
        " USING_MALI400_L2_CACHE=" MALIDRV_USING_MALI400_L2_CACHE
        " USING_MALI200=" MALIDRV_USING_MALI200
        " USING_GP2=" MALIDRV_USING_MALIGP2
        " USING_PMM=" MALIDRV_USING_PMM
        " USING_MALI_RUNTIME_PM=" MALIDRV_USING_MALI_RUNTIME_PM
        " API_VERSION=" MALIDRV_API_VERSION
        " REVISION=" MALIDRV_SVN_REV
        " BUILD_DATE=" MALIDRV_BUILD_DATE
        ;
}
