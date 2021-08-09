/* RetroArch - A frontend for libretro.
 * Copyright (C) 2010-2018 - Francisco Javier Trujillo Mata - fjtrujy
 *
 * RetroArch is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Found-
 * ation, either version 3 of the License, or (at your option) any later version.
 *
 * RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. See the GNU General Public License for more details.
 * * You should have received a copy of the GNU General Public License along with RetroArch.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <kernel.h>

#include <sbv_patches.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <ps2_devices.h>
#include <ps2_irx_variables.h>
#include <loadfile.h>
#include <elf-loader.h>

#if defined(SCREEN_DEBUG)
#include <debug.h>
#endif

#include <file/file_path.h>
#include <string/stdstring.h>

#include "../frontend_driver.h"
#include "../../defaults.h"
#include "../../file_path_special.h"
#include "../../verbosity.h"
#include "../../paths.h"


static enum frontend_fork ps2_fork_mode = FRONTEND_FORK_NONE;
static char cwd[FILENAME_MAX];

static void create_path_names(void)
{
   char user_path[FILENAME_MAX];

   snprintf(user_path, sizeof(user_path), "%sretroarch", cwd);
   fill_pathname_basedir(g_defaults.dirs[DEFAULT_DIR_PORT], cwd, sizeof(g_defaults.dirs[DEFAULT_DIR_PORT]));

   /* Content in the same folder */

   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_CORE], cwd,
         "cores", sizeof(g_defaults.dirs[DEFAULT_DIR_CORE]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_CORE_INFO], cwd,
         "info", sizeof(g_defaults.dirs[DEFAULT_DIR_CORE_INFO]));

   /* user data */
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_ASSETS], user_path,
         "assets", sizeof(g_defaults.dirs[DEFAULT_DIR_ASSETS]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_DATABASE], user_path,
         "database/rdb", sizeof(g_defaults.dirs[DEFAULT_DIR_DATABASE]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_CURSOR], user_path,
         "database/cursors", sizeof(g_defaults.dirs[DEFAULT_DIR_CURSOR]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_CHEATS], user_path,
         "cheats", sizeof(g_defaults.dirs[DEFAULT_DIR_CHEATS]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_MENU_CONFIG], user_path,
         "config", sizeof(g_defaults.dirs[DEFAULT_DIR_MENU_CONFIG]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_CORE_ASSETS], user_path,
         "downloads", sizeof(g_defaults.dirs[DEFAULT_DIR_CORE_ASSETS]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_PLAYLIST], user_path,
         "playlists", sizeof(g_defaults.dirs[DEFAULT_DIR_PLAYLIST]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_REMAP], user_path,
         "remaps", sizeof(g_defaults.dirs[DEFAULT_DIR_REMAP]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_SRAM], user_path,
         "savefiles", sizeof(g_defaults.dirs[DEFAULT_DIR_SRAM]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_SAVESTATE], user_path,
         "savestates", sizeof(g_defaults.dirs[DEFAULT_DIR_SAVESTATE]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_SYSTEM], user_path,
         "system", sizeof(g_defaults.dirs[DEFAULT_DIR_SYSTEM]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_CACHE], user_path,
         "temp", sizeof(g_defaults.dirs[DEFAULT_DIR_CACHE]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_OVERLAY], user_path,
         "overlays", sizeof(g_defaults.dirs[DEFAULT_DIR_OVERLAY]));
#ifdef HAVE_VIDEO_LAYOUT
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_VIDEO_LAYOUT], user_path,
         "layouts", sizeof(g_defaults.dirs[DEFAULT_DIR_VIDEO_LAYOUT]));
#endif
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_THUMBNAILS], user_path,
         "thumbnails", sizeof(g_defaults.dirs[DEFAULT_DIR_THUMBNAILS]));
   fill_pathname_join(g_defaults.dirs[DEFAULT_DIR_LOGS], user_path,
         "logs", sizeof(g_defaults.dirs[DEFAULT_DIR_LOGS]));

   /* history and main config */
   strlcpy(g_defaults.dirs[DEFAULT_DIR_CONTENT_HISTORY],
         user_path, sizeof(g_defaults.dirs[DEFAULT_DIR_CONTENT_HISTORY]));
   fill_pathname_join(g_defaults.path_config, user_path,
         FILE_PATH_MAIN_CONFIG, sizeof(g_defaults.path_config));

#ifndef IS_SALAMANDER
   dir_check_defaults("custom.ini");
#endif
}

static void reset_IOP()
{
   SifInitRpc(0);
#if !defined(DEBUG) || defined(BUILD_FOR_PCSX2)
   /* Comment this line if you don't wanna debug the output */
   while(!SifIopReset(NULL, 0)){};
#endif

   while(!SifIopSync()){};
   SifInitRpc(0);
   sbv_patch_enable_lmb();
   sbv_patch_disable_prefix_check();
}

static void load_modules()
{
   /* I/O Files */
   SifExecModuleBuffer(&iomanX_irx, size_iomanX_irx, 0, NULL, NULL);
   SifExecModuleBuffer(&fileXio_irx, size_fileXio_irx, 0, NULL, NULL);
   SifExecModuleBuffer(&sio2man_irx, size_sio2man_irx, 0, NULL, NULL);

   /* Memory Card */
   SifExecModuleBuffer(&mcman_irx, size_mcman_irx, 0, NULL, NULL);
   SifExecModuleBuffer(&mcserv_irx, size_mcserv_irx, 0, NULL, NULL);

   /* USB */
   SifExecModuleBuffer(&usbd_irx, size_usbd_irx, 0, NULL, NULL);
   SifExecModuleBuffer(&bdm_irx, size_bdm_irx, 0, NULL, NULL);
   SifExecModuleBuffer(&bdmfs_vfat_irx, size_bdmfs_vfat_irx, 0, NULL, NULL);
   SifExecModuleBuffer(&usbmass_bd_irx, size_usbmass_bd_irx, 0, NULL, NULL);

#if !defined(DEBUG)
   /* CDFS */
   SifExecModuleBuffer(&cdfs_irx, size_cdfs_irx, 0, NULL, NULL);
#endif

#ifndef IS_SALAMANDER
   /* Controllers */
   SifExecModuleBuffer(&mtapman_irx, size_mtapman_irx, 0, NULL, NULL);
   SifExecModuleBuffer(&padman_irx, size_padman_irx, 0, NULL, NULL);

   /* Audio */
   SifExecModuleBuffer(&libsd_irx, size_libsd_irx, 0, NULL, NULL);
   SifExecModuleBuffer(&audsrv_irx, size_audsrv_irx, 0, NULL, NULL);
#endif
}

static void frontend_ps2_get_env(int *argc, char *argv[],
      void *args, void *params_data)
{
   int i;
   create_path_names();

#ifndef IS_SALAMANDER
   if (!string_is_empty(argv[1]))
   {
      static char path[FILENAME_MAX] = {0};
      struct rarch_main_wrap      *args =
         (struct rarch_main_wrap*)params_data;

      if (args)
      {
         strlcpy(path, argv[1], sizeof(path));

         args->touched        = true;
         args->no_content     = false;
         args->verbose        = false;
         args->config_path    = NULL;
         args->sram_path      = NULL;
         args->state_path     = NULL;
         args->content_path   = path;
         args->libretro_path  = NULL;

         RARCH_LOG("argv[0]: %s\n", argv[0]);
         RARCH_LOG("argv[1]: %s\n", argv[1]);

         RARCH_LOG("Auto-start game %s.\n", argv[1]);
      }
   }
#endif

#ifndef IS_SALAMANDER
   dir_check_defaults("custom.ini");
#endif
}

static void frontend_ps2_init(void *data)
{
   reset_IOP();
#if defined(SCREEN_DEBUG)
   init_scr();
   scr_printf("Starting RetroArch...\n");
#endif
   load_modules();


#ifndef IS_SALAMANDER
   /* Initializes audsrv library */
   if (audsrv_init())
   {
      RARCH_ERR("audsrv library not initalizated\n");
   }

   /* Initializes pad un multitap libraries */
   if (mtapInit() != 1)
   {
      RARCH_ERR("mtapInit library not initalizated\n");
   }
   if (padInit(0) != 1)
   {
      RARCH_ERR("padInit library not initalizated\n");
   }
#endif

   getcwd(cwd, sizeof(cwd));
#if !defined(IS_SALAMANDER) && !defined(DEBUG)
   // If it is not salamander we need to go one level up for set the CWD.
   path_parent_dir(cwd);
#endif

#if !defined(DEBUG)
   waitUntilDeviceIsReady(cwd);
#endif
}

static void frontend_ps2_deinit(void *data)
{
}

static void frontend_ps2_exec(const char *path, bool should_load_game)
{
   int args = 0;
   static char *argv[1];
   RARCH_LOG("Attempt to load executable: [%s].\n", path);
#ifndef IS_SALAMANDER
   if (should_load_game && !path_is_empty(RARCH_PATH_CONTENT))
   {
      args++;
      argv[0] = (char *)path_get(RARCH_PATH_CONTENT);
   }
#endif
   LoadELFFromFile(path, args, argv);
}

#ifndef IS_SALAMANDER
static bool frontend_ps2_set_fork(enum frontend_fork fork_mode)
{
   switch (fork_mode)
   {
      case FRONTEND_FORK_CORE:
         RARCH_LOG("FRONTEND_FORK_CORE\n");
         ps2_fork_mode  = fork_mode;
         break;
      case FRONTEND_FORK_CORE_WITH_ARGS:
         RARCH_LOG("FRONTEND_FORK_CORE_WITH_ARGS\n");
         ps2_fork_mode  = fork_mode;
         break;
      case FRONTEND_FORK_RESTART:
         RARCH_LOG("FRONTEND_FORK_RESTART\n");
         /* NOTE: We don't implement Salamander, so just turn
          * this into FRONTEND_FORK_CORE. */
         ps2_fork_mode  = FRONTEND_FORK_CORE;
         break;
      case FRONTEND_FORK_NONE:
      default:
         return false;
   }

   return true;
}
#endif

static void frontend_ps2_exitspawn(char *s, size_t len, char *args)
{
   bool should_load_content = false;
#ifndef IS_SALAMANDER
   if (ps2_fork_mode == FRONTEND_FORK_NONE)
      return;

   switch (ps2_fork_mode)
   {
      case FRONTEND_FORK_CORE_WITH_ARGS:
         should_load_content = true;
         break;
      case FRONTEND_FORK_NONE:
      default:
         break;
   }
#endif
   frontend_ps2_exec(s, should_load_content);
}

static int frontend_ps2_get_rating(void) { return 10; }

enum frontend_architecture frontend_ps2_get_arch(void)
{
    return FRONTEND_ARCH_MIPS;
}

static int frontend_ps2_parse_drive_list(void *data, bool load_content)
{
#ifndef IS_SALAMANDER
   file_list_t *list = (file_list_t*)data;
   enum msg_hash_enums enum_idx = load_content ?
      MENU_ENUM_LABEL_FILE_DETECT_CORE_LIST_PUSH_DIR :
      MENU_ENUM_LABEL_FILE_BROWSER_DIRECTORY;

   menu_entries_append_enum(list,
         rootDevicePath(BOOT_DEVICE_MC0),
         msg_hash_to_str(MENU_ENUM_LABEL_FILE_DETECT_CORE_LIST_PUSH_DIR),
         enum_idx,
         FILE_TYPE_DIRECTORY, 0, 0);
   menu_entries_append_enum(list,
         rootDevicePath(BOOT_DEVICE_MC1),
         msg_hash_to_str(MENU_ENUM_LABEL_FILE_DETECT_CORE_LIST_PUSH_DIR),
         enum_idx,
         FILE_TYPE_DIRECTORY, 0, 0);
   menu_entries_append_enum(list,
         rootDevicePath(BOOT_DEVICE_CDFS),
         msg_hash_to_str(MENU_ENUM_LABEL_FILE_DETECT_CORE_LIST_PUSH_DIR),
         enum_idx,
         FILE_TYPE_DIRECTORY, 0, 0);
   menu_entries_append_enum(list,
         rootDevicePath(BOOT_DEVICE_MASS),
         msg_hash_to_str(MENU_ENUM_LABEL_FILE_DETECT_CORE_LIST_PUSH_DIR),
         enum_idx,
         FILE_TYPE_DIRECTORY, 0, 0);
   menu_entries_append_enum(list,
         rootDevicePath(BOOT_DEVICE_HOST),
         msg_hash_to_str(MENU_ENUM_LABEL_FILE_DETECT_CORE_LIST_PUSH_DIR),
         enum_idx,
         FILE_TYPE_DIRECTORY, 0, 0);
#if defined(DEBUG) && !defined(BUILD_FOR_PCSX2)
   menu_entries_append_enum(list,
         "host:",
         msg_hash_to_str(MENU_ENUM_LABEL_FILE_DETECT_CORE_LIST_PUSH_DIR),
         enum_idx,
         FILE_TYPE_DIRECTORY, 0, 0);
#endif
#endif

   return 0;
}

frontend_ctx_driver_t frontend_ctx_ps2 = {
   frontend_ps2_get_env,         /* get_env */
   frontend_ps2_init,            /* init */
   frontend_ps2_deinit,          /* deinit */
   frontend_ps2_exitspawn,       /* exitspawn */
   NULL,                         /* process_args */
   frontend_ps2_exec,            /* exec */
#ifdef IS_SALAMANDER
   NULL,                         /* set_fork */
#else
   frontend_ps2_set_fork,        /* set_fork */
#endif
   NULL,                         /* shutdown */
   NULL,                         /* get_name */
   NULL,                         /* get_os */
   frontend_ps2_get_rating,      /* get_rating */
   NULL,                         /* load_content */
   frontend_ps2_get_arch,        /* get_architecture */
   NULL,                         /* get_powerstate */
   frontend_ps2_parse_drive_list,/* parse_drive_list */
   NULL,                         /* get_total_mem */
   NULL,                         /* get_free_mem */
   NULL,                         /* install_signal_handler */
   NULL,                         /* get_sighandler_state */
   NULL,                         /* set_sighandler_state */
   NULL,                         /* destroy_sighandler_state */
   NULL,                         /* attach_console */
   NULL,                         /* detach_console */
   NULL,                         /* get_lakka_version */
   NULL,                         /* set_screen_brightness */
   NULL,                         /* watch_path_for_changes */
   NULL,                         /* check_for_path_changes */
   NULL,                         /* set_sustained_performance_mode */
   NULL,                         /* get_cpu_model_name */
   NULL,                         /* get_user_language */
   NULL,                         /* is_narrator_running */
   NULL,                         /* accessibility_speak */
   "ps2",                        /* ident */
   NULL                          /* get_video_driver */
};
