/* Ultimate64 Control - Command Line Interface
 * Shared definitions for the u64ctl CLI tool.
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#ifndef U64_CLI_H
#define U64_CLI_H

#include <exec/types.h>

#include "ultimate64_amiga.h"

/* Template for ReadArgs */
#define TEMPLATE                                                              \
  "HOST/K,COMMAND/A,FILE/K,ADDRESS/K,TEXT/K,DRIVE/K,MODE/K,"                  \
  "PASSWORD/K,SONG/K/N,VERBOSE/S,QUIET/S"

#define ENV_ULTIMATE64_HOST "Ultimate64/Host"
#define ENV_ULTIMATE64_PASSWORD "Ultimate64/Password"
#define ENV_ULTIMATE64_PORT "Ultimate64/Port"

/* Default values */
#define DEFAULT_HOST "192.168.1.64"
#define DEFAULT_PORT "80"

/* Argument indices */
enum
{
  ARG_HOST = 0,
  ARG_COMMAND,
  ARG_FILE,
  ARG_ADDRESS,
  ARG_TEXT,
  ARG_DRIVE,
  ARG_MODE,
  ARG_PASSWORD,
  ARG_SONG,
  ARG_VERBOSE,
  ARG_QUIET,
  ARG_COUNT
};

/* Command types */
typedef enum
{
  U64CMD_UNKNOWN = 0,
  U64CMD_INFO,
  U64CMD_RESET,
  U64CMD_REBOOT,
  U64CMD_POWEROFF,
  U64CMD_PAUSE,
  U64CMD_RESUME,
  U64CMD_MENU,
  U64CMD_LOAD,
  U64CMD_RUN,
  U64CMD_TYPE,
  U64CMD_MOUNT,
  U64CMD_UNMOUNT,
  U64CMD_DRIVES,
  U64CMD_PEEK,
  U64CMD_POKE,
  U64CMD_PLAYSID,
  U64CMD_PLAYMOD,
  U64CMD_CONFIG,
  U64CMD_SETHOST,
  U64CMD_SETPASSWORD,
  U64CMD_SETPORT,
  U64CMD_CLEARCONFIG,
  U64CMD_LISTCONFIG,     /* List all configuration categories */
  U64CMD_SHOWCONFIG,     /* Show items in a specific category */
  U64CMD_GETCONFIG,      /* Get value of specific configuration item */
  U64CMD_SETCONFIG,      /* Set value of specific configuration item */
  U64CMD_SAVECONFIG,     /* Save current config to flash */
  U64CMD_LOADCONFIG,     /* Load config from flash */
  U64CMD_RESETCONFIG,    /* Reset config to defaults */
} U64CommandType;

/* Command table entry */
typedef struct
{
  const char *name;
  U64CommandType type;
  const char *description;
  BOOL implemented;
} Command;

/* Command table (defined in args.c) */
extern const Command commands[];

/* Module globals (defined in main.c) */
extern BOOL verbose;
extern BOOL quiet;

/* output.c */
void PrintError(const char *format, ...);
void PrintInfo(const char *format, ...);
void PrintVerbose(const char *format, ...);
void PrintUsage(void);

/* args.c */
U64CommandType ParseCommand(const char *cmd);
U64MountMode ParseMountMode(const char *mode);

/* config.c */
void LoadSettings(char **host, char **password, UWORD *port);
BOOL SaveSettings(CONST_STRPTR host, CONST_STRPTR password, UWORD port);

/* commands.c */
int ExecuteCommand(U64Connection *conn, U64CommandType cmd, LONG *args,
                   char *env_host, char *env_password, UWORD env_port,
                   char *host_arg, char *password_arg);

#endif /* U64_CLI_H */
