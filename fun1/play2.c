// Re-writing of play.c by Oleksiy Grechnyev

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>

#include <bcm_host.h>

#include <IL/OMX_Core.h>
#include <IL/OMX_Component.h>
#include <IL/OMX_Video.h>
#include <IL/OMX_Broadcom.h>

// Dunno where this is originally stolen from...
#define OMX_INIT_STRUCTURE(a) \
    memset(&(a), 0, sizeof(a)); \
    (a).nSize = sizeof(a); \
    (a).nVersion.nVersion = OMX_VERSION; \
    (a).nVersion.s.nVersionMajor = OMX_VERSION_MAJOR; \
    (a).nVersion.s.nVersionMajor = OMX_VERSION_MINOR; \
    (a).nVersion.s.nRevision = OMX_VERSION_REVISION; \
    (a).nVersion.s.nStep = OMX_VERSION_STEP 

// Global used to quit
static int wannaQuit = 0;

// App context
typedef struct {
    OMX_HANDLETYPE camera;
    OMX_BUFFERHEADERTYPE *cameraPPBufferIn;
    int cameraReady;
    OMX_HANDLETYPE render;
    OMX_HANDLETYPE nullSink;
    int flushed;
    VCOS_SEMAPHORE_T handlerLock;
} appCtx;

//====================================================
static void say(const char * msg){
    fprintf(stderr, "%s\n", msg);
}
//====================================================
static void die(const char * msg){
    fputs(msg, stderr);
    exit(1);
}
//====================================================
static void dieOmx(OMX_ERRORTYPE err, const char * msg){
    fprintf(stderr, "Error 0x%08x ; %s \n", err, msg);
    exit(1);
}
//====================================================
// Check for errors
static void check(OMX_ERRORTYPE r,  const char * msg){
    if (r != OMX_ErrorNone)
        dieOmx(r, msg);
}
//====================================================
// Change a state of a component
static void changeState(OMX_HANDLETYPE hand, OMX_STATETYPE state) {
    check(OMX_SendCommand(hand, OMX_CommandStateSet, state, NULL), "Change State");
    
    OMX_STATETYPE state2;
    for(;;){
        OMX_GetState(hand, &state2);
        if (state == state2)
            break;
        usleep(10000);
    }
}
//====================================================
static void blockUntilPortChanged(OMX_HANDLETYPE hand, OMX_U32 port, OMX_BOOL desired){
    OMX_PARAM_PORTDEFINITIONTYPE portdef;
    OMX_INIT_STRUCTURE(portdef);
    portdef.nPortIndex = port;
    for (;;){
        check(OMX_GetParameter(hand, OMX_IndexParamPortDefinition, &portdef), "Get port def");
        if (portdef.bEnabled == desired)
            break;
        usleep(10000);
    }
}
//====================================================
static void disablePort(OMX_HANDLETYPE hand, OMX_U32 port){
    check(OMX_SendCommand(hand, OMX_CommandPortDisable, port, NULL), "Disbale Port");
    blockUntilPortChanged(hand, port, OMX_FALSE);
}
//---
static void enablePort(OMX_HANDLETYPE hand, OMX_U32 port){
    check(OMX_SendCommand(hand, OMX_CommandPortEnable, port, NULL), "Enable Port");
    blockUntilPortChanged(hand, port, OMX_TRUE);
}
//====================================================
// Lock/unlock the semaphore/mutex
static void lock(appCtx *pCtx){
    vcos_semaphore_wait(&pCtx->handlerLock);
}
//---
// Lock/unlock the semaphore/mutex
static void unlock(appCtx *pCtx){
    vcos_semaphore_post(&pCtx->handlerLock);
}
//====================================================
static void flush(OMX_HANDLETYPE hand, OMX_U32 port, appCtx *pCtx){
    
    check(OMX_SendCommand(hand, OMX_CommandFlush, port, NULL), "Flush");
    
    // Block until flushed
    int quit = 0;
    for (;;){
        lock(pCtx);
        if (pCtx->flushed) {
            pCtx->flushed = 0;
            quit = 1;
        }
        unlock(pCtx);
        if (quit)
            break;
        usleep(10000);
    }
}
//====================================================
// Signal handler
static void signalHandler(int signal){
    wannaQuit = 1;
}
//====================================================
// OMX event handler -- important, I guess
static OMX_ERRORTYPE eventHandler(
        OMX_HANDLETYPE hand,
        OMX_PTR pAD,
        OMX_EVENTTYPE e,
        OMX_U32 nD1,
        OMX_U32 nD2,
        OMX_PTR pED){
    appCtx *pCtx = (appCtx *)pAD;
    switch (e){
        case OMX_EventCmdComplete:
            lock(pCtx);
            if (nD1 == OMX_CommandFlush) 
                pCtx->flushed = 1;
            unlock(pCtx);
            break;
        case OMX_EventParamOrConfigChanged:
            lock(pCtx);
            if (nD2 == OMX_IndexParamCameraDeviceNumber)
                pCtx->cameraReady = 1;
            unlock(pCtx);
            break;
        case OMX_EventError:
            dieOmx(nD1, "Error Event");
        default: ;
    }
    
    return OMX_ErrorNone;
}
//====================================================
static void initComponentHandle(
        const char *name,
        OMX_HANDLETYPE *pHand,
        OMX_PTR pAppData,
        OMX_CALLBACKTYPE *pCall){
    // Create handle    
    check(OMX_GetHandle(pHand, (char *)name, pAppData, pCall), "Get Handle");
    
    // Disable ports - WHY ???
    OMX_INDEXTYPE types[] = {
        OMX_IndexParamAudioInit,
        OMX_IndexParamVideoInit,
        OMX_IndexParamImageInit,
        OMX_IndexParamOtherInit
    };
    OMX_PORT_PARAM_TYPE ports;
    OMX_INIT_STRUCTURE(ports);
    OMX_GetParameter(*pHand, OMX_IndexParamVideoInit, &ports);
    for (int i = 0; i < 4; ++i)
        if (OMX_GetParameter(*pHand, types[i], &ports) == OMX_ErrorNone) 
            for (OMX_U32 ind = ports.nStartPortNumber; ind < ports.nStartPortNumber + ports.nPorts; ++ind) 
                disablePort(*pHand, ind);
}
//====================================================
int main(){
    bcm_host_init();
    check(OMX_Init(), "Init");
    // Init context
    appCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    if (vcos_semaphore_create(&ctx.handlerLock, "handlerLock", 1) != VCOS_SUCCESS)
       die("Cannot Create Semaphore");
       
    // Init handles
    say("Creating handles ...");
    OMX_CALLBACKTYPE callbacks;
    memset(&ctx, 0, sizeof(callbacks));
    callbacks.EventHandler = eventHandler;
    
    initComponentHandle("OMX.broadcom.camera", &ctx.camera, &ctx, &callbacks);
    initComponentHandle("OMX.broadcom.video_render", &ctx.render, &ctx, &callbacks);
    initComponentHandle("OMX.broadcom.null_sink", &ctx.nullSink, &ctx, &callbacks);
    
    say("Configuring camera ...");
    // Camera Callback whatever
    OMX_CONFIG_REQUESTCALLBACKTYPE cbtype;
    OMX_INIT_STRUCTURE(cbtype);
    cbtype.nPortIndex = OMX_ALL;
    cbtype.nIndex = OMX_IndexParamCameraDeviceNumber;
    cbtype.bEnable = OMX_TRUE;
    check(OMX_SetConfig(ctx.camera, OMX_IndexConfigRequestCallback, &cbtype), "Camera Set Callback");
    // Device number ?
    OMX_PARAM_U32TYPE device;
    OMX_INIT_STRUCTURE(device);
    device.nPortIndex = OMX_ALL;
    device.nU32 = 0;
    check(OMX_SetParameter(ctx.camera, OMX_IndexParamCameraDeviceNumber, &device), "Camera Device Number");
    
    // Ensure the camera is ready
    while (!ctx.cameraReady)
        usleep(10000);
    
    say("Setting up tunnels ...");
    check(OMX_SetupTunnel(ctx.camera, 70, ctx.nullSink, 240), "Tuinnel Cam -> NullSink");
    check(OMX_SetupTunnel(ctx.camera, 71, ctx.render, 90), "Tuinnel Cam -> Render");
    
    say("Switching components to idle ...");
    changeState(ctx.camera, OMX_StateIdle);
    changeState(ctx.render, OMX_StateIdle);
    changeState(ctx.nullSink, OMX_StateIdle);
    
    say("Enabling ports ...");
    enablePort(ctx.camera, 73);
    enablePort(ctx.camera, 70);
    enablePort(ctx.camera, 71);
    enablePort(ctx.render, 90);
    enablePort(ctx.nullSink, 240);
    
    say("Allocating buffers ...");
    OMX_PARAM_PORTDEFINITIONTYPE cameraPortdef;
    OMX_INIT_STRUCTURE(cameraPortdef);
    cameraPortdef.nPortIndex = 73;
    check(OMX_GetParameter(ctx.camera, OMX_IndexParamPortDefinition, &cameraPortdef), "Get port Def 73");
    check(OMX_AllocateBuffer(ctx.camera, &ctx.cameraPPBufferIn, 73, NULL, cameraPortdef.nBufferSize), "Allocate Buffer");
    
    say("Switching components to executing ...");
    changeState(ctx.camera, OMX_StateExecuting);
    changeState(ctx.render, OMX_StateExecuting);
    changeState(ctx.nullSink, OMX_StateExecuting);
    
    say("Starting the capture ...");
    OMX_CONFIG_PORTBOOLEANTYPE capture;
    OMX_INIT_STRUCTURE(capture);
    capture.nPortIndex = 71;
    capture.bEnabled = OMX_TRUE;
    check(OMX_SetParameter(ctx.camera, OMX_IndexConfigPortCapturing, &capture), "Start Capture");
    
    say("Enter capture and playback loop, press Ctrl-C to quit...");
    signal(SIGINT,  signalHandler);
    signal(SIGTERM,  signalHandler);
    signal(SIGQUIT,  signalHandler);
    
    while (!wannaQuit)
        usleep(10000);
    
    return 0;
}