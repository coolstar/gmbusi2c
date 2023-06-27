#if !defined(_GMBUSI2CI2C_H_)
#define _GMBUSI2CI2C_H_

#pragma warning(disable:4200)  // suppress nameless struct/union warning
#pragma warning(disable:4201)  // suppress nameless struct/union warning
#pragma warning(disable:4214)  // suppress bit field types other than int warning
#include <initguid.h>
#include <wdm.h>

#pragma warning(default:4200)
#pragma warning(default:4201)
#pragma warning(default:4214)
#include <wdf.h>
#include <acpiioct.h>
#include <SPBCx.h>

#define RESHUB_USE_HELPER_ROUTINES
#include <reshub.h>
#include <pshpack1.h>

typedef struct _PNP_I2C_SERIAL_BUS_DESCRIPTOR {
    PNP_SERIAL_BUS_DESCRIPTOR SerialBusDescriptor;
    ULONG ConnectionSpeed;
    USHORT SlaveAddress;
    // follwed by optional Vendor Data
    // followed by PNP_IO_DESCRIPTOR_RESOURCE_NAME
} PNP_I2C_SERIAL_BUS_DESCRIPTOR, * PPNP_I2C_SERIAL_BUS_DESCRIPTOR;

#include <poppack.h>

#include "gmbusi2c.h"

//
// String definitions
//

#define DRIVERNAME                 "gmbusi2c.sys: "

#define GMBUSI2C_POOL_TAG            (ULONG) 'GMIC'
#define GMBUSI2C_HARDWARE_IDS        L"CoolStar\\LINK0000\0\0"
#define GMBUSI2C_HARDWARE_IDS_LENGTH sizeof(GMBUSI2CI2C_HARDWARE_IDS)

#define GMBUSMMIO_SPACE 2 * 1024 * 1024

#define true 1
#define false 0

typedef
NTSTATUS
(*PGMBUSI2C_BUS_LOCK)(
    IN      PVOID Context
    );

typedef
NTSTATUS
(*PGMBUSI2C_BUS_UNLOCK)(
    IN      PVOID Context
    );

typedef
NTSTATUS
(*PGMBUSI2C_BAR0_ADDR)(
    IN      PVOID Context,
    OUT     PVOID *MMIOAddr
    );

DEFINE_GUID(GUID_GMBUSI2C_INTERFACE_STANDARD,
    0xf23b6099, 0xb638, 0x4fa9, 0x82, 0xca, 0x26, 0xe2, 0xf4, 0xeb, 0x76, 0x49);

//
// Interface for getting and setting power level etc.,
//
typedef struct _GMBUSI2C_INTERFACE_STANDARD {
    INTERFACE                        InterfaceHeader;
    PGMBUSI2C_BAR0_ADDR              GetResources;
    PGMBUSI2C_BUS_LOCK               LockBus;
    PGMBUSI2C_BUS_UNLOCK             UnlockBus;
} GMBUSI2C_INTERFACE_STANDARD, * PGMBUSI2C_INTERFACE_STANDARD;

typedef struct _GMBUSI2CI2C_CONTEXT
{
	//
	// Handle back to the WDFDEVICE
	//

	WDFDEVICE FxDevice;

    BOOLEAN IsArbitrator;

    PVOID MMIOAddress;

    //Arbitrator Only

    WDFWAITLOCK GMBusLock;

    //Links only past this

    PVOID GMBusI2CBusContext;

    WDFIOTARGET busIoTarget;

    PGMBUSI2C_BUS_LOCK GMBusI2CLockBus;

    PGMBUSI2C_BUS_UNLOCK GMBusI2CUnlockBus;

    UINT8 busNumber;

} GMBUSI2C_CONTEXT, *PGMBUSI2C_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(GMBUSI2C_CONTEXT, GetDeviceContext)

//
// Function definitions
//

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_UNLOAD GMBusI2CDriverUnload;

EVT_WDF_DRIVER_DEVICE_ADD GMBusI2CEvtDeviceAdd;

EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL GMBusI2CEvtInternalDeviceControl;

UINT32 read32(PGMBUSI2C_CONTEXT pDevice, UINT32 reg);
void write32(PGMBUSI2C_CONTEXT pDevice, UINT32 reg, UINT32 val);

NTSTATUS i2c_xfer(PGMBUSI2C_CONTEXT pDevice,
    _In_ SPBTARGET SpbTarget,
    _In_ SPBREQUEST SpbRequest,
    _In_ ULONG TransferCount);

//
// Helper macros
//

#define DEBUG_LEVEL_ERROR   1
#define DEBUG_LEVEL_INFO    2
#define DEBUG_LEVEL_VERBOSE 3

#define DBG_INIT  1
#define DBG_PNP   2
#define DBG_IOCTL 4

#if 0
#define GMBusI2CPrint(dbglevel, dbgcatagory, fmt, ...) {          \
    if (GMBusI2CDebugLevel >= dbglevel &&                         \
        (GMBusI2CDebugCatagories && dbgcatagory))                 \
		    {                                                           \
        DbgPrint(DRIVERNAME);                                   \
        DbgPrint(fmt, __VA_ARGS__);                             \
		    }                                                           \
}
#else
#define GMBusI2CPrint(dbglevel, fmt, ...) {                       \
}
#endif
#endif