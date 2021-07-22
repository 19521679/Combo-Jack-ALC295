/*
 * Accessing HD-audio verbs via hwdep interface
 * Version 0.3
 *
 * Copyright (c) 2008 Takashi Iwai <tiwai@suse.de>
 *
 * Licensed under GPL v2 or later.
 */

//
// Based on hda-verb from alsa-tools:
// https://www.alsa-project.org/main/index.php/Main_Page
//
// Conceptually derived from ALCPlugFix:
// https://github.com/goodwin/ALCPlugFix
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

// For driver
#include "hda_hwdep.h"
#include "rt298.h" // From Linux

#define AC_VERB_GET_STREAM_FORMAT		0x0a00
#define AC_VERB_GET_AMP_GAIN_MUTE		0x0b00
#define AC_VERB_GET_PROC_COEF			0x0c00
#define AC_VERB_GET_COEF_INDEX			0x0d00
#define AC_VERB_PARAMETERS			0x0f00
#define AC_VERB_GET_CONNECT_SEL			0x0f01
#define AC_VERB_GET_CONNECT_LIST		0x0f02
#define AC_VERB_GET_PROC_STATE			0x0f03
#define AC_VERB_GET_SDI_SELECT			0x0f04
#define AC_VERB_GET_POWER_STATE			0x0f05
#define AC_VERB_GET_CONV			0x0f06
#define AC_VERB_GET_PIN_WIDGET_CONTROL		0x0f07
#define AC_VERB_GET_UNSOLICITED_RESPONSE	0x0f08
#define AC_VERB_GET_PIN_SENSE			0x0f09
#define AC_VERB_GET_BEEP_CONTROL		0x0f0a
#define AC_VERB_GET_EAPD_BTLENABLE		0x0f0c
#define AC_VERB_GET_DIGI_CONVERT_1		0x0f0d
#define AC_VERB_GET_DIGI_CONVERT_2		0x0f0e
#define AC_VERB_GET_VOLUME_KNOB_CONTROL		0x0f0f
#define AC_VERB_GET_GPIO_DATA			0x0f15
#define AC_VERB_GET_GPIO_MASK			0x0f16
#define AC_VERB_GET_GPIO_DIRECTION		0x0f17
#define AC_VERB_GET_GPIO_WAKE_MASK		0x0f18
#define AC_VERB_GET_GPIO_UNSOLICITED_RSP_MASK	0x0f19
#define AC_VERB_GET_GPIO_STICKY_MASK		0x0f1a
#define AC_VERB_GET_CONFIG_DEFAULT		0x0f1c
#define AC_VERB_GET_SUBSYSTEM_ID		0x0f20

#define AC_VERB_SET_STREAM_FORMAT		0x200
#define AC_VERB_SET_AMP_GAIN_MUTE		0x300
#define AC_VERB_SET_PROC_COEF			0x400
#define AC_VERB_SET_COEF_INDEX			0x500
#define AC_VERB_SET_CONNECT_SEL			0x701
#define AC_VERB_SET_PROC_STATE			0x703
#define AC_VERB_SET_SDI_SELECT			0x704
#define AC_VERB_SET_POWER_STATE			0x705
#define AC_VERB_SET_CHANNEL_STREAMID		0x706
#define AC_VERB_SET_PIN_WIDGET_CONTROL		0x707
#define AC_VERB_SET_UNSOLICITED_ENABLE		0x708
#define AC_VERB_SET_PIN_SENSE			0x709
#define AC_VERB_SET_BEEP_CONTROL		0x70a
#define AC_VERB_SET_EAPD_BTLENABLE		0x70c
#define AC_VERB_SET_DIGI_CONVERT_1		0x70d
#define AC_VERB_SET_DIGI_CONVERT_2		0x70e
#define AC_VERB_SET_VOLUME_KNOB_CONTROL		0x70f
#define AC_VERB_SET_GPIO_DATA			0x715
#define AC_VERB_SET_GPIO_MASK			0x716
#define AC_VERB_SET_GPIO_DIRECTION		0x717
#define AC_VERB_SET_GPIO_WAKE_MASK		0x718
#define AC_VERB_SET_GPIO_UNSOLICITED_RSP_MASK	0x719
#define AC_VERB_SET_GPIO_STICKY_MASK		0x71a
#define AC_VERB_SET_CONFIG_DEFAULT_BYTES_0	0x71c
#define AC_VERB_SET_CONFIG_DEFAULT_BYTES_1	0x71d
#define AC_VERB_SET_CONFIG_DEFAULT_BYTES_2	0x71e
#define AC_VERB_SET_CONFIG_DEFAULT_BYTES_3	0x71f
#define AC_VERB_SET_CODEC_RESET			0x7ff

#define AC_PAR_VENDOR_ID		0x00
#define AC_PAR_SUBSYSTEM_ID		0x01
#define AC_PAR_REV_ID			0x02
#define AC_PAR_NODE_COUNT		0x04
#define AC_PAR_FUNCTION_TYPE		0x05
#define AC_PAR_AUDIO_FG_CAP		0x08
#define AC_PAR_AUDIO_WIDGET_CAP		0x09
#define AC_PAR_PCM			0x0a
#define AC_PAR_STREAM			0x0b
#define AC_PAR_PIN_CAP			0x0c
#define AC_PAR_AMP_IN_CAP		0x0d
#define AC_PAR_CONNLIST_LEN		0x0e
#define AC_PAR_POWER_STATE		0x0f
#define AC_PAR_PROC_CAP			0x10
#define AC_PAR_GPIO_CAP			0x11
#define AC_PAR_AMP_OUT_CAP		0x12
#define AC_PAR_VOL_KNB_CAP		0x13

//
// Global Variables
//

io_service_t VerbStubIOService;
io_connect_t DataConnection;
uint32_t connectiontype = 0;
bool run = true;

//
// Open connection to IOService
//

uint32_t OpenServiceConnection()
{
    //
    // Having a kernel-side server (VerbStub) and a user-side client (hda-verb) is really the only way to ensure that hda-
    // verb won't break when IOAudioFamily changes. This 2-component solution is necessary because we can't compile kernel
    // libraries into user-space programs on macOS and expect them to work generically.
    //
    // Additionally, if this program were made as a single executable that accessed device memory regions directly, it would
    // only be guaranteed to work for one machine on one BIOS version since memory regions change depending on hardware
    // configurations. This is why Raspberry Pis, STM32s, and other embedded platforms are nice to program on: They don't
    // change much between versions so programs can be made extremely lightweight. Linux also does a pretty good job
    // achieving a similar situation, since everything (devices, buses, etc.) on Linux is represented by an easily
    // accessible file (just look at how simple the hda-verb program in alsa-tools is! All it uses is ioctl).
    //
    
    CFMutableDictionaryRef dict = IOServiceMatching("com_XPS_VerbStub");
    
    // Use IOServiceGetMatchingService since we can reasonably expect "VerbStub" is the only IORegistryEntry of its kind.
    // Otherwise IOServiceGetMatchingServices with an iterating algorithm must be used to find the kernel extension.
    
    VerbStubIOService = IOServiceGetMatchingService(kIOMasterPortDefault, dict);
    
    // Hopefully the kernel extension loaded properly so it can be found.
    
    if (!VerbStubIOService)
    {
        fprintf(stderr, "Could not locate VerbStub kext. Ensure it is loaded; verbs cannot be sent otherwise.\n");
        return -1;
    }
    
    // Connect to the IOService object
    // Note: kern_return_t is just an int
    kern_return_t kernel_return_status = IOServiceOpen(VerbStubIOService, mach_task_self(), connectiontype, &DataConnection);
    
    if (kernel_return_status != kIOReturnSuccess)
    {
        fprintf(stderr, "Failed to open VerbStub IOService: %08x.\n", kernel_return_status);
        return -1;
    }
    
    return kernel_return_status; // 0 if successful
}

//
// Send verb command
//

static uint32_t VerbCommand(uint32_t command)
{
    //
    // Call the function ultimately responsible for sending commands in the kernel extension. That function will return the
    // response we also want.
    // https://lists.apple.com/archives/darwin-drivers/2008/Mar/msg00007.html
    //
    
    uint32_t inputCount = 1; // Number of input arguments
    uint32_t outputCount = 1; // Number of elements in output
    uint64_t input = command; // Array of input scalars
    uint64_t output; // Array of output scalars
    
    // IOConnectCallScalarMethod was introduced in Mac OS X 10.5
    
    kern_return_t kernel_return_status = IOConnectCallScalarMethod(DataConnection, connectiontype, &input, inputCount, &output, &outputCount);
    
    if (kernel_return_status != kIOReturnSuccess)
    {
        fprintf(stderr, "Error sending command.\n");
        return -1;
    }
    
    // Return command response
    return (uint32_t)output;
}

//
// Close connection to IOService
//

void CloseServiceConnection()
{
    // Done with the VerbStub IOService object, so we don't need to hold on to it anymore
    IOObjectRelease(VerbStubIOService);
}

//
// UPDATE COEFFICIENTS
//

static uint32_t UPDATE_COEF(uint32_t index, uint32_t mask, uint32_t value)
{
    struct hda_verb_ioctl SetCoefIndex;
    struct hda_verb_ioctl GetProcCoef;
    struct hda_verb_ioctl SetProcCoef;
    
    int nid = RT298_VENDOR_REGISTERS; // Vendor Register 0x20 in rt298.h
    uint32_t tmp;
    
    SetCoefIndex.verb = HDA_VERB(nid, AC_VERB_SET_COEF_INDEX, index); // Verb to set the coefficient index desired
    SetCoefIndex.res = VerbCommand(SetCoefIndex.verb); // Go to index desired
    if(SetCoefIndex.res != 0) // If uninitialized expect response of -1 (0xFFFFFFFF for uint32_t)
    {
        fprintf(stderr, "Received weird response 0x%x for command 0x%x\n", SetCoefIndex.res, SetCoefIndex.verb);
        return -1; // Fail
    }
    
    GetProcCoef.verb = HDA_VERB(nid, AC_VERB_GET_PROC_COEF, 0x00); // Get Processing Coefficient payload is always 0
    GetProcCoef.res = VerbCommand(GetProcCoef.verb); // Get original data
    
    tmp = GetProcCoef.res & ~mask;
    tmp |= value & mask;
    
    if (tmp != GetProcCoef.res)
    {
        // New data to write!
        SetCoefIndex.res = VerbCommand(SetCoefIndex.verb); // Reset back to the index desired (indices automatically iterate after a read)
        
        SetProcCoef.verb = HDA_VERB(nid, AC_VERB_SET_PROC_COEF, tmp);
        SetProcCoef.res = VerbCommand(SetProcCoef.verb); // Send new processing coefficient
        if (SetProcCoef.res != 0)
        {
            fprintf(stderr, "Received strange response 0x%x for command 0x%x\n", SetCoefIndex.res, SetCoefIndex.verb);
            return -1; // Fail
        }
    }
    // Return value just written (or not) as reported by register
    SetCoefIndex.res = VerbCommand(SetCoefIndex.verb); // Go back to register
    GetProcCoef.res = VerbCommand(GetProcCoef.verb); // Get register data

    // Return processing coefficient
    return GetProcCoef.res;
}

//
// Unplugged Settings
//

static uint32_t unplugged()
{
    fprintf(stderr, "Jack Status: unplugged.\n");
    UPDATE_COEF(0x4f, 0xfcc0, 0xc400);
    UPDATE_COEF(0x50, 0x2000, 0x2000);
    UPDATE_COEF(0x56, 0x0006, 0x0006);
    UPDATE_COEF(0x66, 0x0008, 0);
    UPDATE_COEF(0x67, 0x2000, 0);
    VerbCommand(HDA_VERB(0x22, AC_VERB_SET_CONNECT_SEL, 0x05)); // Mac: Need to manually switch the selector back to internal microphone
    
    return 0; // Success
}

//
// Headphones Settings
//

static uint32_t headphones()
{
    fprintf(stderr, "Jack Status: headphones plugged in.\n");
    UPDATE_COEF(0x4f, 0xfcc0, 0xc400); /* Set to TRS type */
    VerbCommand(HDA_VERB(0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24)); // 0x20 corresponds to IN (0x20) + HiZ (0x00) -- Note: HiZ means "High Impedance"
    UPDATE_COEF(0x50, 0x2000, 0x2000);
    UPDATE_COEF(0x56, 0x0006, 0x0006);
    UPDATE_COEF(0x66, 0x0008, 0);
    UPDATE_COEF(0x67, 0x2000, 0);
    
    return 0; // Success
}

//
// Line-In Settings
//

static uint32_t linein()
{
    fprintf(stderr, "Jack Status: line-in device plugged in.\n");
    UPDATE_COEF(0x4f, 0x000c, 0x0);
    //set 0x21 pin_w 0 here
    VerbCommand(HDA_VERB(0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, 0)); // Disable headphone output
    UPDATE_COEF(0x50, 0x2000, 0);
    UPDATE_COEF(0x56, 0x0006, 0);
    UPDATE_COEF(0x4f, 0xfcc0, 0xc400); /* Set to TRS type */
    UPDATE_COEF(0x66, 0x0008, 0x0008);
    UPDATE_COEF(0x67, 0x2000, 0x2000);
    //set 0x1a pin_w 0x21 here
    VerbCommand(HDA_VERB(0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x21)); // 0x21 corresponds to IN (0x20) + VREF 50 (0x01)
    if (VerbCommand(HDA_VERB(0x1a, AC_VERB_GET_POWER_STATE, 0x00)) != 0) // Mac: Check power state of node 0x1a since it's frequently in D3
    {
        VerbCommand(HDA_VERB(0x1a, AC_VERB_SET_POWER_STATE, 0x00)); // Mac: Power on node 0x1a if in D3
    }
    VerbCommand(HDA_VERB(0x22, AC_VERB_SET_CONNECT_SEL, 0x02)); // Mac: Need to manually switch the selector to line-in node
    
    return 0; // Success
}

//
// Headset: CTIA (iPhone-style plug)
//

static uint32_t headsetCTIA()
{
    fprintf(stderr, "Jack Status: headset (CTIA/iPhone) plugged in.\n");
    UPDATE_COEF(0x8e, 0x0070, 0x0020); /* Headset output enable */
    UPDATE_COEF(0x4f, 0xfcc0, 0xd400);
    usleep(300000); //unit = microseconds; Note: msleep is in kernel only
    UPDATE_COEF(0x50, 0x2000, 0x2000);
    UPDATE_COEF(0x56, 0x0006, 0x0006);
    UPDATE_COEF(0x66, 0x0008, 0);
    UPDATE_COEF(0x67, 0x2000, 0);
    VerbCommand(HDA_VERB(0x22, AC_VERB_SET_CONNECT_SEL, 0x00)); // Mac: Need to manually switch the selector to headset node
    
    return 0; // Success
}

//
// Headset: OMTP (Nokia-style plug)
//

static uint32_t headsetOMTP()
{
    fprintf(stderr, "Jack Status: headset (OMTP/Nokia) plugged in.\n");
    UPDATE_COEF(0x8e, 0x0070, 0x0010); /* Headset output enable */
    UPDATE_COEF(0x4f, 0xfcc0, 0xe400);
    usleep(300000);
    UPDATE_COEF(0x50, 0x2000, 0x2000);
    UPDATE_COEF(0x56, 0x0006, 0x0006);
    UPDATE_COEF(0x66, 0x0008, 0);
    UPDATE_COEF(0x67, 0x2000, 0);
    VerbCommand(HDA_VERB(0x22, AC_VERB_SET_CONNECT_SEL, 0x00)); // Mac: Need to manually switch the selector to headset node
    
    return 0; // Success
}

//
// Headset Auto-Detection (CTIA/OMTP)
//

static uint32_t headsetcheck()
{
    struct hda_verb_ioctl SetCoefIndex;
    struct hda_verb_ioctl GetProcCoef;
    uint32_t retval;
    
    int nid = RT298_VENDOR_REGISTERS;
//    bool is_ctia;
    
    fprintf(stderr, "Jack Status: headset plugged in. Checking type...\n");
    UPDATE_COEF(0x8e, 0x0070, 0x0020); /* Headset output enable */
    VerbCommand(HDA_VERB(0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24)); // 0x24 corresponds to IN (0x20) + VREF 80 (0x04)
    UPDATE_COEF(0x4f, 0xfcc0, 0xd400); /* Check Type */
    usleep(350000);
    // Read register 0x50
    SetCoefIndex.verb = HDA_VERB(nid, AC_VERB_SET_COEF_INDEX, 0x50); // Verb to set the coefficient index desired
    SetCoefIndex.res = VerbCommand(SetCoefIndex.verb); // Go to index desired
    
    GetProcCoef.verb = HDA_VERB(nid, AC_VERB_GET_PROC_COEF, 0x00); // Get Processing Coefficient payload is always 0
    GetProcCoef.res = VerbCommand(GetProcCoef.verb); // Get data
    
    // Check if register 0x50 reports a CTIA- or OMTP-style headset
    if ((GetProcCoef.res & 0x0070) == 0x0070)
    {
      retval = headsetCTIA();
    }
    else
    {
      retval = headsetOMTP();
    }
    
//    is_ctia = (GetProcCoef.res & 0x0070) == 0x0070; <-- Wow.
    
    return retval; // Success
}

//
// Jack unplug monitor
//

void JackBehavior()
{
    int nid, verb, param;
    nid = RT298_HP_OUT;
    verb = AC_VERB_GET_PIN_SENSE;
    param = 0x00;
    
    while(run && ((VerbCommand(HDA_VERB(nid, verb, param)) & 0x80000000) == 0x80000000)) // Poll headphone jack state
    {
        sleep(1); // Polling frequency (seconds): use usleep for microseconds if finer-grained control is needed
    }
    if (run) // If process is killed, maintain current state
    {
        fprintf(stderr, "Unplugged.\n");
        unplugged(); // Clean up, jack's been unplugged or process was killed
    }
}

//
// Pop-up menu
//


uint32_t CFPopUpMenu()
{
    
    uint32_t status;
    
    fprintf(stderr, "Headset selected.\n"); // %lu\n", kCFUserNotificationOtherResponse);
    status = headsetcheck();
    return status;
}
//
// Respect OS signals
//

void sigHandler(int signo)
{
    fprintf(stderr, "\nsigHandler: Received signal %d\n", signo); // Technically this print is not async-safe, but so far haven't run into any issues
    switch (signo)
    {
        // Need to be sure object gets released correctly on any kind of quit
        // notification, otherwise the program's left still running!
        case SIGINT: // CTRL + c or Break key
        case SIGTERM: // Shutdown/Restart
        case SIGHUP: // "Hang up" (legacy)
        case SIGKILL: // Kill
        case SIGTSTP: // Close terminal from x button
            run = false;
            break; // SIGTERM, SIGINT mean we must quit, so do it gracefully
        default:
            break;
    }
}

//
// Main
//

int main()
{
    fprintf(stderr, "Starting jack watcher.\n");
    
    // Set up error handler
    signal(SIGHUP, sigHandler);
    signal(SIGTERM, sigHandler);
    signal(SIGINT, sigHandler);
    signal(SIGKILL, sigHandler);
    signal(SIGTSTP, sigHandler);
    
    // Local variables
    kern_return_t ServiceConnectionStatus;
	int version;
	int nid, verb, param;
	struct hda_verb_ioctl val;

    // Mac version of hda-verb
	version = 0x2710; // Darwin

    // If this error appears, the program was compiled incorrectly
	if (version < HDA_HWDEP_VERSION) {
		fprintf(stderr, "Invalid version number 0x%x\n", version);
		return 1;
	}
    
    // Establish user-kernel connection
    ServiceConnectionStatus = OpenServiceConnection();
    if (ServiceConnectionStatus != kIOReturnSuccess)
    {
        while ((ServiceConnectionStatus != kIOReturnSuccess) && run)
        {
            fprintf(stderr, "Error establshing IOService connection. Retrying in 1 second...\n");
            sleep (1);
            ServiceConnectionStatus = OpenServiceConnection();
        }
    }
    
    // Set up jack monitor verb command
    nid = RT298_HP_OUT;
    verb = AC_VERB_GET_PIN_SENSE;
    param = 0x00;
    
	fprintf(stderr, "nid = 0x%x, verb = 0x%x, param = 0x%x\n",
		nid, verb, param);

//    fprintf(stderr, "TEST Update Coef Command = 0x%x\n", UPDATE_COEF(0x4f, 0xfcc0, 0xd400));
    
    // Properly format jack monitor verb command
	val.verb = HDA_VERB(nid, verb, param);

    // This  is just a test send
//    fprintf(stderr, "Verb Command = 0x%x\n", val.verb);
//	val.res = VerbCommand(val.verb);
//	fprintf(stderr, "Response = 0x%x\n", val.res);

    while(run) // Poll headphone jack state
    {
        val.res = VerbCommand(val.verb);
        if (val.res == -1) // 0xFFFFFFFF means jack not ready yet
        {
            fprintf(stderr, "Jack not ready. Checking again in 1 second...\n");
        }
        else if ((val.res & 0x80000000) == 0x80000000)
        {
            {
                fprintf(stderr, "Jack sense detected! Displaying menu...\n");
                if (CFPopUpMenu() == 0)
                {
                    JackBehavior();
                }
                else
                {
                    break;
                }
            }
        }
        
        sleep(1); // Sleep delay (seconds): use usleep for microseconds if fine-grained control is needed
    }
    
    // All done here, clean up and exit safely
    CloseServiceConnection();
    fprintf(stderr, "Exiting safely!\n");
    
	return 0;
}
