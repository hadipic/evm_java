/**
 * @file lv_fs_fatfs.c
 * Compatible with LVGL v8.x and ESP-IDF v5.x
 */

#include "lvgl.h"
#include "ff.h"

/*********************
 *      DEFINES
 *********************/
#define DRIVE_LETTER 'S'

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void * fs_open(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode);
static lv_fs_res_t fs_close(lv_fs_drv_t * drv, void * file_p);
static lv_fs_res_t fs_read(lv_fs_drv_t * drv, void * file_p, void * buf, uint32_t btr, uint32_t * br);
static lv_fs_res_t fs_seek(lv_fs_drv_t * drv, void * file_p, uint32_t pos, lv_fs_whence_t whence);
static lv_fs_res_t fs_tell(lv_fs_drv_t * drv, void * file_p, uint32_t * pos_p);
static void * fs_dir_open(lv_fs_drv_t * drv, const char * path);
static lv_fs_res_t fs_dir_read(lv_fs_drv_t * drv, void * dir_p, char * fn);
static lv_fs_res_t fs_dir_close(lv_fs_drv_t * drv, void * dir_p);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
static bool fs_initialized = false;
void lv_fs_fatfs_init(void)
{
    
       if (fs_initialized) {
     //   ESP_LOGW("LVFS", "⚠️ File system already initialized");
        return;
    }
    static lv_fs_drv_t fs_drv;
    lv_fs_drv_init(&fs_drv);

    /*Set up fields...*/
    fs_drv.letter = DRIVE_LETTER;
      fs_drv.open_cb = fs_open;
    fs_drv.close_cb = fs_close;
    fs_drv.read_cb = fs_read;
    fs_drv.seek_cb = fs_seek;
    fs_drv.tell_cb = fs_tell;

    fs_drv.dir_close_cb = fs_dir_close;
    fs_drv.dir_open_cb = fs_dir_open;
    fs_drv.dir_read_cb = fs_dir_read;

    lv_fs_drv_register(&fs_drv);
      fs_initialized = true;
}


/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Open a file
 */
static void * fs_open(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode)
{
    LV_UNUSED(drv);
    
    BYTE flags = 0;
    if(mode == LV_FS_MODE_WR) flags = FA_WRITE | FA_OPEN_ALWAYS;
    else if(mode == LV_FS_MODE_RD) flags = FA_READ;
    else if(mode == (LV_FS_MODE_WR | LV_FS_MODE_RD)) flags = FA_READ | FA_WRITE | FA_OPEN_ALWAYS;

    FIL * f = lv_mem_alloc(sizeof(FIL));
    if(f == NULL) return NULL;

    FRESULT res = f_open(f, path, flags);
    if(res == FR_OK) {
        f_lseek(f, 0);
        return f;
    } else {
        lv_mem_free(f);
        return NULL;
    }
}

/**
 * Close an opened file
 */
static lv_fs_res_t fs_close(lv_fs_drv_t * drv, void * file_p)
{
    LV_UNUSED(drv);
    FIL * f = (FIL *)file_p;
    f_close(f);
    lv_mem_free(f);
    return LV_FS_RES_OK;
}

/**
 * Read data from an opened file
 */
static lv_fs_res_t fs_read(lv_fs_drv_t * drv, void * file_p, void * buf, uint32_t btr, uint32_t * br)
{
    LV_UNUSED(drv);
    FIL * f = (FIL *)file_p;
    UINT br_tmp;
    FRESULT res = f_read(f, buf, btr, &br_tmp);
    *br = br_tmp;
    if(res == FR_OK) return LV_FS_RES_OK;
    else return LV_FS_RES_UNKNOWN;
}

/**
 * Set the read write pointer.
 */
static lv_fs_res_t fs_seek(lv_fs_drv_t * drv, void * file_p, uint32_t pos, lv_fs_whence_t whence)
{
    LV_UNUSED(drv);
    FIL * f = (FIL *)file_p;
    
    switch(whence) {
        case LV_FS_SEEK_SET:
            f_lseek(f, pos);
            break;
        case LV_FS_SEEK_CUR:
            f_lseek(f, f_tell(f) + pos);
            break;
        case LV_FS_SEEK_END:
            f_lseek(f, f_size(f) + pos);
            break;
        default:
            return LV_FS_RES_INV_PARAM;
    }
    return LV_FS_RES_OK;
}

/**
 * Give the position of the read write pointer
 */
static lv_fs_res_t fs_tell(lv_fs_drv_t * drv, void * file_p, uint32_t * pos_p)
{
    LV_UNUSED(drv);
    FIL * f = (FIL *)file_p;
    *pos_p = f_tell(f);
    return LV_FS_RES_OK;
}

/**
 * Initialize a 'DIR' variable for directory reading
 */
static void * fs_dir_open(lv_fs_drv_t * drv, const char * path)
{
    LV_UNUSED(drv);
    
    FF_DIR * d = lv_mem_alloc(sizeof(FF_DIR));
    if(d == NULL) return NULL;

    FRESULT res = f_opendir(d, path);
    if(res != FR_OK) {
        lv_mem_free(d);
        return NULL;
    }
    return d;
}

/**
 * Read the next filename from a directory.
 */
static lv_fs_res_t fs_dir_read(lv_fs_drv_t * drv, void * dir_p, char * fn)
{
    LV_UNUSED(drv);
    FF_DIR * d = (FF_DIR *)dir_p;
    
    FILINFO fno;
    FRESULT res;

    do {
        res = f_readdir(d, &fno);
        if(res != FR_OK || fno.fname[0] == 0) {
            fn[0] = '\0';
            break;
        }

        if(fno.fattrib & AM_DIR) {
            fn[0] = '/';
            strcpy(&fn[1], fno.fname);
        } else {
            strcpy(fn, fno.fname);
        }
    } while(strcmp(fn, "/.") == 0 || strcmp(fn, "/..") == 0);

    return (fn[0] != '\0') ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

/**
 * Close the directory reading
 */
static lv_fs_res_t fs_dir_close(lv_fs_drv_t * drv, void * dir_p)
{
    LV_UNUSED(drv);
    FF_DIR * d = (FF_DIR *)dir_p;
    f_closedir(d);
    lv_mem_free(d);
    return LV_FS_RES_OK;
}