/* Ultimate64 SID Player - configuration load/save
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#include <exec/memory.h>
#include <exec/types.h>

#include <proto/dos.h>
#include <proto/exec.h>

#include <string.h>

#include "player.h"
#include "env_utils.h"

/* Configuration functions */
BOOL LoadConfig(struct ObjApp *obj)
{
    STRPTR env_host, env_password, env_dir;

    env_host = U64_ReadEnvVar(ENV_ULTIMATE64_HOST);
    if (env_host) {
        strcpy(obj->host, env_host);
        FreeVec(env_host);
    } else {
        obj->host[0] = '\0';
    }

    env_password = U64_ReadEnvVar(ENV_ULTIMATE64_PASSWORD);
    if (env_password) {
        strcpy(obj->password, env_password);
        FreeVec(env_password);
    } else {
        obj->password[0] = '\0';
    }

    env_dir = U64_ReadEnvVar(ENV_ULTIMATE64_SID_DIR);
    if (env_dir) {
        strncpy(obj->last_sid_dir, env_dir, sizeof(obj->last_sid_dir) - 1);
        obj->last_sid_dir[sizeof(obj->last_sid_dir) - 1] = '\0';
        FreeVec(env_dir);
    } else {
        strcpy(obj->last_sid_dir, "");
    }

    return TRUE;
}

BOOL SaveConfig(struct ObjApp *obj)
{
    U64_WriteEnvVar(ENV_ULTIMATE64_HOST, obj->host, TRUE);
    if (strlen(obj->password) > 0) {
        U64_WriteEnvVar(ENV_ULTIMATE64_PASSWORD, obj->password, TRUE);
    } else {
        DeleteVar(ENV_ULTIMATE64_PASSWORD, GVF_GLOBAL_ONLY);
        DeleteFile("ENVARC:" ENV_ULTIMATE64_PASSWORD);
    }

    if (strlen(obj->last_sid_dir) > 0) {
        U64_WriteEnvVar(ENV_ULTIMATE64_SID_DIR, obj->last_sid_dir, TRUE);
    }

    return TRUE;
}
