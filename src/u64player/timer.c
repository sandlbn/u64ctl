/* Ultimate64 SID Player - timer device management
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#include <devices/timer.h>
#include <exec/types.h>

#include <proto/dos.h>
#include <proto/exec.h>

#include "player.h"

/* Timer globals (extern declarations in player.h) */
struct MsgPort      *TimerPort   = NULL;
struct timerequest  *TimerReq    = NULL;
ULONG                TimerSig    = 0;
BOOL                 TimerRunning = FALSE;

BOOL StartTimerDevice(void)
{
    U64_DEBUG("Starting timer device...");

    if (!(TimerPort = CreateMsgPort())) {
        U64_DEBUG("Failed to create timer port");
        return FALSE;
    }

    if (!(TimerReq = (struct timerequest*)CreateIORequest(TimerPort, sizeof(*TimerReq)))) {
        U64_DEBUG("Failed to create timer request");
        DeleteMsgPort(TimerPort);
        TimerPort = NULL;
        return FALSE;
    }

    if (OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest*)TimerReq, 0)) {
        U64_DEBUG("Failed to open timer device");
        DeleteIORequest((struct IORequest*)TimerReq);
        DeleteMsgPort(TimerPort);
        TimerReq = NULL;
        TimerPort = NULL;
        return FALSE;
    }

    TimerSig = 1UL << TimerPort->mp_SigBit;
    TimerRunning = FALSE;

    U64_DEBUG("Timer device started successfully, signal mask: 0x%08x", (unsigned int)TimerSig);
    return TRUE;
}

void StopTimerDevice(void)
{
    U64_DEBUG("Stopping timer device...");
    StopPeriodicTimer();

    if (TimerReq) {
        CloseDevice((struct IORequest*)TimerReq);
        DeleteIORequest((struct IORequest*)TimerReq);
        TimerReq = NULL;
    }

    if (TimerPort) {
        DeleteMsgPort(TimerPort);
        TimerPort = NULL;
    }

    TimerSig = 0;
    TimerRunning = FALSE;
    U64_DEBUG("Timer device stopped");
}

void StartPeriodicTimer(void)
{
    if (!TimerReq || TimerRunning) return;

    U64_DEBUG("Starting periodic timer");

    TimerReq->tr_node.io_Command = TR_ADDREQUEST;
    TimerReq->tr_time.tv_secs    = 1;
    TimerReq->tr_time.tv_micro   = 0;

    SendIO((struct IORequest*)TimerReq);
    TimerRunning = TRUE;
}

void StopPeriodicTimer(void)
{
    if (!TimerReq || !TimerRunning) return;

    U64_DEBUG("Stopping periodic timer");

    if (!CheckIO((struct IORequest*)TimerReq)) {
        AbortIO((struct IORequest*)TimerReq);
    }
    WaitIO((struct IORequest*)TimerReq);

    TimerRunning = FALSE;
}

BOOL CheckTimerSignal(ULONG sigs)
{
    struct Message *msg;
    BOOL timer_fired = FALSE;

    if (!TimerPort || !TimerRunning || !(sigs & TimerSig)) {
        return FALSE;
    }

    while ((msg = GetMsg(TimerPort))) {
        timer_fired = TRUE;
        TimerRunning = FALSE;

        if (objApp && objApp->state == PLAYER_PLAYING) {
            objApp->current_time++;
            APP_UpdateCurrentSongDisplay();
            if (objApp->current_time >= objApp->total_time) {
                APP_Next();
            }

            if (objApp->state == PLAYER_PLAYING) {
                StartPeriodicTimer();
            }
        }
    }

    return timer_fired;
}
