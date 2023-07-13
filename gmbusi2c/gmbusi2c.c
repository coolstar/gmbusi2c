#include "driver.h"
#include "stdint.h"

#define bool int
#define MS_IN_US 1000

static ULONG GMBusI2CDebugLevel = 100;
static ULONG GMBusI2CDebugCatagories = DBG_INIT || DBG_PNP || DBG_IOCTL;

UINT32 read32(PGMBUSI2C_CONTEXT pDevice, UINT32 reg)
{
	return *(UINT32 *)((CHAR *)pDevice->MMIOAddress + reg);
}

void write32(PGMBUSI2C_CONTEXT pDevice, UINT32 reg, UINT32 val) {
	*(UINT32 *)((CHAR *)pDevice->MMIOAddress + reg) = val;
}

NTSTATUS
DriverEntry(
__in PDRIVER_OBJECT  DriverObject,
__in PUNICODE_STRING RegistryPath
)
{
	NTSTATUS               status = STATUS_SUCCESS;
	WDF_DRIVER_CONFIG      config;
	WDF_OBJECT_ATTRIBUTES  attributes;

	GMBusI2CPrint(DEBUG_LEVEL_INFO, DBG_INIT,
		"Driver Entry\n");

	WDF_DRIVER_CONFIG_INIT(&config, GMBusI2CEvtDeviceAdd);

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

	//
	// Create a framework driver object to represent our driver.
	//

	status = WdfDriverCreate(DriverObject,
		RegistryPath,
		&attributes,
		&config,
		WDF_NO_HANDLE
		);

	if (!NT_SUCCESS(status))
	{
		GMBusI2CPrint(DEBUG_LEVEL_ERROR, DBG_INIT,
			"WdfDriverCreate failed with status 0x%x\n", status);
	}

	return status;
}

static NTSTATUS GetBusInformation(
	_In_ WDFDEVICE FxDevice
) {
	PGMBUSI2C_CONTEXT pDevice = GetDeviceContext(FxDevice);
	WDFMEMORY outputMemory = WDF_NO_HANDLE;

	NTSTATUS status = STATUS_ACPI_NOT_INITIALIZED;
	char* propertyStr = "coolstar,bus-number";

	size_t inputBufferLen = sizeof(ACPI_GET_DEVICE_SPECIFIC_DATA) + strlen(propertyStr) + 1;
	ACPI_GET_DEVICE_SPECIFIC_DATA* inputBuffer = ExAllocatePoolWithTag(NonPagedPool, inputBufferLen, GMBUSI2C_POOL_TAG);
	if (!inputBuffer) {
		goto Exit;
	}
	RtlZeroMemory(inputBuffer, inputBufferLen);

	inputBuffer->Signature = IOCTL_ACPI_GET_DEVICE_SPECIFIC_DATA_SIGNATURE;

	unsigned char uuidend[] = { 0x8a, 0x91, 0xbc, 0x9b, 0xbf, 0x4a, 0xa3, 0x01 };

	inputBuffer->Section.Data1 = 0xdaffd814;
	inputBuffer->Section.Data2 = 0x6eba;
	inputBuffer->Section.Data3 = 0x4d8c;
	memcpy(inputBuffer->Section.Data4, uuidend, sizeof(uuidend)); //Avoid Windows defender false positive

	strcpy(inputBuffer->PropertyName, propertyStr);
	inputBuffer->PropertyNameLength = strlen(propertyStr) + 1;

	PACPI_EVAL_OUTPUT_BUFFER outputBuffer;
	size_t outputArgumentBufferSize = 8;
	size_t outputBufferSize = FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) + sizeof(ACPI_METHOD_ARGUMENT_V1) + outputArgumentBufferSize;

	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.ParentObject = FxDevice;
	status = WdfMemoryCreate(&attributes,
		NonPagedPoolNx,
		0,
		outputBufferSize,
		&outputMemory,
		&outputBuffer);
	if (!NT_SUCCESS(status)) {
		goto Exit;
	}

	WDF_MEMORY_DESCRIPTOR inputMemDesc;
	WDF_MEMORY_DESCRIPTOR outputMemDesc;
	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inputMemDesc, inputBuffer, (ULONG)inputBufferLen);
	WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(&outputMemDesc, outputMemory, NULL);

	status = WdfIoTargetSendInternalIoctlSynchronously(
		WdfDeviceGetIoTarget(FxDevice),
		NULL,
		IOCTL_ACPI_GET_DEVICE_SPECIFIC_DATA,
		&inputMemDesc,
		&outputMemDesc,
		NULL,
		NULL
	);
	if (!NT_SUCCESS(status)) {
		GMBusI2CPrint(
			DEBUG_LEVEL_ERROR,
			DBG_IOCTL,
			"Error getting device data - 0x%x\n",
			status);
		goto Exit;
	}

	if (outputBuffer->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE_V1 &&
		outputBuffer->Count < 1 &&
		outputBuffer->Argument->Type != ACPI_METHOD_ARGUMENT_INTEGER &&
		outputBuffer->Argument->DataLength < 1) {
		status = STATUS_ACPI_INVALID_ARGUMENT;
		goto Exit;
	}

	pDevice->busNumber = outputBuffer->Argument->Data[0];

Exit:
	if (inputBuffer) {
		ExFreePoolWithTag(inputBuffer, GMBUSI2C_POOL_TAG);
	}
	if (outputMemory != WDF_NO_HANDLE) {
		WdfObjectDelete(outputMemory);
	}
	return status;
}

NTSTATUS ConnectToArbitrator(
	_In_ WDFDEVICE FxDevice
) {
	PGMBUSI2C_CONTEXT pDevice = GetDeviceContext(FxDevice);
	WDF_OBJECT_ATTRIBUTES objectAttributes;

	WDF_OBJECT_ATTRIBUTES_INIT(&objectAttributes);
	objectAttributes.ParentObject = FxDevice;

	NTSTATUS status = WdfIoTargetCreate(FxDevice,
		&objectAttributes,
		&pDevice->busIoTarget
	);
	if (!NT_SUCCESS(status))
	{
		GMBusI2CPrint(
			DEBUG_LEVEL_ERROR,
			DBG_IOCTL,
			"Error creating IoTarget object - 0x%x\n",
			status);
		if (pDevice->busIoTarget)
			WdfObjectDelete(pDevice->busIoTarget);
		return status;
	}

	DECLARE_CONST_UNICODE_STRING(busDosDeviceName, L"\\DosDevices\\GMBUSI2C");

	WDF_IO_TARGET_OPEN_PARAMS openParams;
	WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(
		&openParams,
		&busDosDeviceName,
		(GENERIC_READ | GENERIC_WRITE));

	openParams.ShareAccess = FILE_SHARE_READ | FILE_SHARE_WRITE;
	openParams.CreateDisposition = FILE_OPEN;
	openParams.FileAttributes = FILE_ATTRIBUTE_NORMAL;

	GMBUSI2C_INTERFACE_STANDARD GMBusI2CInterface;
	RtlZeroMemory(&GMBusI2CInterface, sizeof(GMBusI2CInterface));

	status = WdfIoTargetOpen(pDevice->busIoTarget, &openParams);
	if (!NT_SUCCESS(status))
	{
		GMBusI2CPrint(
			DEBUG_LEVEL_ERROR,
			DBG_IOCTL,
			"Error opening IoTarget object - 0x%x\n",
			status);
		WdfObjectDelete(pDevice->busIoTarget);
		return status;
	}

	status = WdfIoTargetQueryForInterface(pDevice->busIoTarget,
		&GUID_GMBUSI2C_INTERFACE_STANDARD,
		(PINTERFACE)&GMBusI2CInterface,
		sizeof(GMBusI2CInterface),
		1,
		NULL);
	WdfIoTargetClose(pDevice->busIoTarget);
	pDevice->busIoTarget = NULL;
	if (!NT_SUCCESS(status)) {
		GMBusI2CPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfFdoQueryForInterface failed 0x%x\n", status);
		return status;
	}

	pDevice->GMBusI2CBusContext = GMBusI2CInterface.InterfaceHeader.Context;
	pDevice->GMBusI2CLockBus = GMBusI2CInterface.LockBus;
	pDevice->GMBusI2CUnlockBus = GMBusI2CInterface.UnlockBus;

	GMBusI2CInterface.GetResources(pDevice->GMBusI2CBusContext, &pDevice->MMIOAddress);

	GMBusI2CPrint(DEBUG_LEVEL_INFO, DBG_PNP,
		"MMIO acquired: %p\n", pDevice->MMIOAddress);

	return status;
}

NTSTATUS
GetMMIOBar(
	_In_ WDFDEVICE FxDevice
)
{
	NTSTATUS status = STATUS_ACPI_NOT_INITIALIZED;
	ACPI_EVAL_INPUT_BUFFER_EX inputBuffer;
	RtlZeroMemory(&inputBuffer, sizeof(inputBuffer));

	inputBuffer.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX;
	status = RtlStringCchPrintfA(
		inputBuffer.MethodName,
		sizeof(inputBuffer.MethodName),
		"MMIO"
	);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	WDFMEMORY outputMemory;
	PACPI_EVAL_OUTPUT_BUFFER outputBuffer;
	size_t outputArgumentBufferSize = 32;
	size_t outputBufferSize = FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) + outputArgumentBufferSize;

	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.ParentObject = FxDevice;

	status = WdfMemoryCreate(&attributes,
		NonPagedPoolNx,
		0,
		outputBufferSize,
		&outputMemory,
		(PVOID*)&outputBuffer);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	RtlZeroMemory(outputBuffer, outputBufferSize);

	WDF_MEMORY_DESCRIPTOR inputMemDesc;
	WDF_MEMORY_DESCRIPTOR outputMemDesc;
	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inputMemDesc, &inputBuffer, (ULONG)sizeof(inputBuffer));
	WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(&outputMemDesc, outputMemory, NULL);

	status = WdfIoTargetSendInternalIoctlSynchronously(
		WdfDeviceGetIoTarget(FxDevice),
		NULL,
		IOCTL_ACPI_EVAL_METHOD_EX,
		&inputMemDesc,
		&outputMemDesc,
		NULL,
		NULL
	);
	if (!NT_SUCCESS(status)) {
		goto Exit;
	}

	if (outputBuffer->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE) {
		goto Exit;
	}

	if (outputBuffer->Count < 1) {
		goto Exit;
	}

	PGMBUSI2C_CONTEXT pDevice = GetDeviceContext(FxDevice);

	UINT64 mmioBAR = *(UINT64*)outputBuffer->Argument[0].Data;

	PHYSICAL_ADDRESS mmioPhys;
	mmioPhys.QuadPart = mmioBAR;

	pDevice->MMIOAddress = MmMapIoSpace(mmioPhys, GMBUSMMIO_SPACE, MmWriteCombined);
	GMBusI2CPrint(DEBUG_LEVEL_INFO, DBG_PNP,
		"MMIO Mapped to %p\n", pDevice->MMIOAddress);

Exit:
	if (outputMemory != WDF_NO_HANDLE) {
		WdfObjectDelete(outputMemory);
	}
	return status;
}

NTSTATUS
OnPrepareHardware(
_In_  WDFDEVICE     FxDevice,
_In_  WDFCMRESLIST  FxResourcesRaw,
_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

This routine caches the SPB resource connection ID.

Arguments:

FxDevice - a handle to the framework device object
FxResourcesRaw - list of translated hardware resources that
the PnP manager has assigned to the device
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	PGMBUSI2C_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;

	UNREFERENCED_PARAMETER(FxResourcesRaw);
	UNREFERENCED_PARAMETER(FxResourcesTranslated);

	if (!pDevice->IsArbitrator) {
		status = GetBusInformation(FxDevice);
		if (!NT_SUCCESS(status))
		{
			return status;
		}

		status = ConnectToArbitrator(FxDevice);
		if (!NT_SUCCESS(status)) {
			return status;
		}

		GMBusI2CPrint(DEBUG_LEVEL_INFO, DBG_PNP,
			"Bus Number: %d\n", pDevice->busNumber);
	}
	else {
		status = GetMMIOBar(FxDevice);
		if (!NT_SUCCESS(status)) {
			return status;
		}
	}

	return status;
}

NTSTATUS
OnReleaseHardware(
_In_  WDFDEVICE     FxDevice,
_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

Arguments:

FxDevice - a handle to the framework device object
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	NTSTATUS status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(FxResourcesTranslated);

	PGMBUSI2C_CONTEXT pDevice = GetDeviceContext(FxDevice);
	if (pDevice->IsArbitrator) {
		MmUnmapIoSpace(pDevice->MMIOAddress, GMBUSMMIO_SPACE);
	}

	return status;
}

NTSTATUS
OnD0Entry(
_In_  WDFDEVICE               FxDevice,
_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine allocates objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);
	NTSTATUS status = STATUS_SUCCESS;

	return status;
}

NTSTATUS
OnD0Exit(
_In_  WDFDEVICE               FxDevice,
_In_  WDF_POWER_DEVICE_STATE  FxTargetState
)
/*++

Routine Description:

This routine destroys objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxTargetState - target power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxTargetState);

	NTSTATUS status = STATUS_SUCCESS;

	return status;
}

NTSTATUS OnTargetConnect(
	_In_  WDFDEVICE  SpbController,
	_In_  SPBTARGET  SpbTarget
) {
	UNREFERENCED_PARAMETER(SpbController);

	PPBC_TARGET pTarget = GetTargetContext(SpbTarget);

	//
	// Get target connection parameters.
	//

	SPB_CONNECTION_PARAMETERS params;
	SPB_CONNECTION_PARAMETERS_INIT(&params);

	SpbTargetGetConnectionParameters(SpbTarget, &params);
	PRH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER  connection = (PRH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER)params.ConnectionParameters;

	if (connection->PropertiesLength < sizeof(PNP_SERIAL_BUS_DESCRIPTOR)) {
		GMBusI2CPrint(DEBUG_LEVEL_INFO, DBG_PNP,
			"Invalid connection properties (length = %lu, "
			"expected = %Iu)\n",
			connection->PropertiesLength,
			sizeof(PNP_SERIAL_BUS_DESCRIPTOR));
		return STATUS_INVALID_PARAMETER;
	}

	PPNP_SERIAL_BUS_DESCRIPTOR descriptor = (PPNP_SERIAL_BUS_DESCRIPTOR)connection->ConnectionProperties;
	if (descriptor->SerialBusType != I2C_SERIAL_BUS_TYPE) {
		GMBusI2CPrint(DEBUG_LEVEL_INFO, DBG_PNP,
			"Bus type %c not supported, only I2C\n",
			descriptor->SerialBusType);
		return STATUS_INVALID_PARAMETER;
	}

	PPNP_I2C_SERIAL_BUS_DESCRIPTOR i2cDescriptor = (PPNP_I2C_SERIAL_BUS_DESCRIPTOR)connection->ConnectionProperties;
	pTarget->Settings.Address = (ULONG)i2cDescriptor->SlaveAddress;

	USHORT I2CFlags = i2cDescriptor->SerialBusDescriptor.TypeSpecificFlags;

	pTarget->Settings.AddressMode = ((I2CFlags & I2C_SERIAL_BUS_SPECIFIC_FLAG_10BIT_ADDRESS) == 0) ? AddressMode7Bit : AddressMode10Bit;
	pTarget->Settings.ConnectionSpeed = i2cDescriptor->ConnectionSpeed;

	if (pTarget->Settings.AddressMode != AddressMode7Bit) {
		return STATUS_NOT_SUPPORTED;
	}

	return STATUS_SUCCESS;
}

VOID OnSpbIoRead(
	_In_ WDFDEVICE SpbController,
	_In_ SPBTARGET SpbTarget,
	_In_ SPBREQUEST SpbRequest,
	_In_ size_t Length
)
{
	PGMBUSI2C_CONTEXT pDevice = GetDeviceContext(SpbController);

	NTSTATUS status = i2c_xfer(pDevice, SpbTarget, SpbRequest, 1);
	SpbRequestComplete(SpbRequest, status);
}

VOID OnSpbIoWrite(
	_In_ WDFDEVICE SpbController,
	_In_ SPBTARGET SpbTarget,
	_In_ SPBREQUEST SpbRequest,
	_In_ size_t Length
)
{
	PGMBUSI2C_CONTEXT pDevice = GetDeviceContext(SpbController);

	NTSTATUS status = i2c_xfer(pDevice, SpbTarget, SpbRequest, 1);
	SpbRequestComplete(SpbRequest, status);
}

VOID OnSpbIoSequence(
	_In_ WDFDEVICE SpbController,
	_In_ SPBTARGET SpbTarget,
	_In_ SPBREQUEST SpbRequest,
	_In_ ULONG TransferCount
)
{

	PGMBUSI2C_CONTEXT pDevice = GetDeviceContext(SpbController);

	NTSTATUS status = i2c_xfer(pDevice, SpbTarget, SpbRequest, TransferCount);
	SpbRequestComplete(SpbRequest, status);
}

NTSTATUS
GetDeviceHID(
	_In_ WDFDEVICE FxDevice
)
{
	NTSTATUS status = STATUS_ACPI_NOT_INITIALIZED;
	ACPI_EVAL_INPUT_BUFFER_EX inputBuffer;
	RtlZeroMemory(&inputBuffer, sizeof(inputBuffer));

	inputBuffer.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX;
	status = RtlStringCchPrintfA(
		inputBuffer.MethodName,
		sizeof(inputBuffer.MethodName),
		"_HID"
	);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	WDFMEMORY outputMemory;
	PACPI_EVAL_OUTPUT_BUFFER outputBuffer;
	size_t outputArgumentBufferSize = 32;
	size_t outputBufferSize = FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) + outputArgumentBufferSize;

	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.ParentObject = FxDevice;

	status = WdfMemoryCreate(&attributes,
		NonPagedPoolNx,
		0,
		outputBufferSize,
		&outputMemory,
		(PVOID*)&outputBuffer);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	RtlZeroMemory(outputBuffer, outputBufferSize);

	WDF_MEMORY_DESCRIPTOR inputMemDesc;
	WDF_MEMORY_DESCRIPTOR outputMemDesc;
	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inputMemDesc, &inputBuffer, (ULONG)sizeof(inputBuffer));
	WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(&outputMemDesc, outputMemory, NULL);

	status = WdfIoTargetSendInternalIoctlSynchronously(
		WdfDeviceGetIoTarget(FxDevice),
		NULL,
		IOCTL_ACPI_EVAL_METHOD_EX,
		&inputMemDesc,
		&outputMemDesc,
		NULL,
		NULL
	);
	if (!NT_SUCCESS(status)) {
		goto Exit;
	}

	if (outputBuffer->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE) {
		goto Exit;
	}

	if (outputBuffer->Count < 1) {
		goto Exit;
	}

	PGMBUSI2C_CONTEXT pDevice = GetDeviceContext(FxDevice);
	UINT32 chipId;

	if (strncmp(outputBuffer->Argument[0].Data, "BOOT0001", outputBuffer->Argument[0].DataLength) == 0) {
		pDevice->IsArbitrator = TRUE;
	}
	else if (strncmp(outputBuffer->Argument[0].Data, "BOOT0002", outputBuffer->Argument[0].DataLength) == 0) {
		pDevice->IsArbitrator = FALSE;
	}
	else {
		status = STATUS_ACPI_INVALID_ARGUMENT;
	}

Exit:
	if (outputMemory != WDF_NO_HANDLE) {
		WdfObjectDelete(outputMemory);
	}
	return status;
}

NTSTATUS GMBusI2CArbGetResources(
	PGMBUSI2C_CONTEXT pDevice,
	PVOID* MMIOAddr
) {
	if (!pDevice || !pDevice->IsArbitrator) {
		return STATUS_INVALID_PARAMETER;
	}
	if (!MMIOAddr) {
		return STATUS_INVALID_PARAMETER;
	}
	*MMIOAddr = pDevice->MMIOAddress;
}

NTSTATUS GMBusI2CArbLock(
	PGMBUSI2C_CONTEXT pDevice
) {
	if (!pDevice || !pDevice->IsArbitrator) {
		return STATUS_INVALID_PARAMETER;
	}
	return WdfWaitLockAcquire(pDevice->GMBusLock, NULL);
}

NTSTATUS GMBusI2CArbUnlock(
	PGMBUSI2C_CONTEXT pDevice
) {
	if (!pDevice || !pDevice->IsArbitrator) {
		return STATUS_INVALID_PARAMETER;
	}
	WdfWaitLockRelease(pDevice->GMBusLock, NULL);
	return STATUS_SUCCESS;
}

NTSTATUS
GMBusI2CEvtDeviceAdd(
IN WDFDRIVER       Driver,
IN PWDFDEVICE_INIT DeviceInit
)
{
	NTSTATUS                      status = STATUS_SUCCESS;
	WDF_OBJECT_ATTRIBUTES         attributes;
	WDFDEVICE                     device;
	PGMBUSI2C_CONTEXT             devContext;

	UNREFERENCED_PARAMETER(Driver);

	PAGED_CODE();

	GMBusI2CPrint(DEBUG_LEVEL_INFO, DBG_PNP,
		"GMBusI2CEvtDeviceAdd called\n");

	status = SpbDeviceInitConfig(DeviceInit);
	if (!NT_SUCCESS(status)) {
		GMBusI2CPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"SpbDeviceInitConfig failed with status code 0x%x\n", status);
		return status;
	}

	{
		WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
		WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);

		pnpCallbacks.EvtDevicePrepareHardware = OnPrepareHardware;
		pnpCallbacks.EvtDeviceReleaseHardware = OnReleaseHardware;
		pnpCallbacks.EvtDeviceD0Entry = OnD0Entry;
		pnpCallbacks.EvtDeviceD0Exit = OnD0Exit;

		WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);
	}

	//
	// Setup the device context
	//

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, GMBUSI2C_CONTEXT);

	//
	// Create a framework device object.This call will in turn create
	// a WDM device object, attach to the lower stack, and set the
	// appropriate flags and attributes.
	//

	status = WdfDeviceCreate(&DeviceInit, &attributes, &device);

	if (!NT_SUCCESS(status))
	{
		GMBusI2CPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfDeviceCreate failed with status code 0x%x\n", status);

		return status;
	}

	{
		WDF_DEVICE_STATE deviceState;
		WDF_DEVICE_STATE_INIT(&deviceState);

		deviceState.NotDisableable = WdfFalse;
		WdfDeviceSetDeviceState(device, &deviceState);
	}

	devContext = GetDeviceContext(device);

	devContext->FxDevice = device;

	status = GetDeviceHID(device);

	if (!NT_SUCCESS(status))
	{
		GMBusI2CPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"Failed to get HID with status code 0x%x\n", status);

		return status;
	}

	GMBusI2CPrint(DEBUG_LEVEL_INFO, DBG_PNP,
		"Arbitrator? %d\n", devContext->IsArbitrator);

	if (devContext->IsArbitrator) { //Arbitrator. Setup protocol
		DECLARE_CONST_UNICODE_STRING(dosDeviceName, L"\\DosDevices\\GMBUSI2C");

		status = WdfDeviceCreateSymbolicLink(device,
			&dosDeviceName
		);
		if (!NT_SUCCESS(status)) {
			GMBusI2CPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
				"WdfDeviceCreateSymbolicLink failed 0x%x\n", status);
			return status;
		}

		WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
		attributes.ParentObject = device;
		status = WdfWaitLockCreate(&attributes, &devContext->GMBusLock);
		if (!NT_SUCCESS(status)) {
			GMBusI2CPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
				"WdfWaitLockCreate failed 0x%x\n", status);
			return status;
		}

		WDF_QUERY_INTERFACE_CONFIG  qiConfig;

		GMBUSI2C_INTERFACE_STANDARD GMBusInterface;
		RtlZeroMemory(&GMBusInterface, sizeof(GMBusInterface));

		GMBusInterface.InterfaceHeader.Size = sizeof(GMBusInterface);
		GMBusInterface.InterfaceHeader.Version = 1;
		GMBusInterface.InterfaceHeader.Context = (PVOID)devContext;

		//
		// Let the framework handle reference counting.
		//
		GMBusInterface.InterfaceHeader.InterfaceReference = WdfDeviceInterfaceReferenceNoOp;
		GMBusInterface.InterfaceHeader.InterfaceDereference = WdfDeviceInterfaceDereferenceNoOp;

		GMBusInterface.GetResources = GMBusI2CArbGetResources;
		GMBusInterface.LockBus = GMBusI2CArbLock;
		GMBusInterface.UnlockBus = GMBusI2CArbUnlock;

		WDF_QUERY_INTERFACE_CONFIG_INIT(&qiConfig,
			(PINTERFACE)&GMBusInterface,
			&GUID_GMBUSI2C_INTERFACE_STANDARD,
			NULL);

		status = WdfDeviceAddQueryInterface(device, &qiConfig);
		if (!NT_SUCCESS(status)) {
			GMBusI2CPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
				"WdfDeviceAddQueryInterface failed 0x%x\n", status);

			return status;
		}
	} else { //Link. Initialize SPB Context
		//
		// Bind a SPB controller object to the device.
		//

		SPB_CONTROLLER_CONFIG spbConfig;
		SPB_CONTROLLER_CONFIG_INIT(&spbConfig);

		spbConfig.PowerManaged = WdfTrue;
		spbConfig.EvtSpbTargetConnect = OnTargetConnect;
		spbConfig.EvtSpbIoRead = OnSpbIoRead;
		spbConfig.EvtSpbIoWrite = OnSpbIoWrite;
		spbConfig.EvtSpbIoSequence = OnSpbIoSequence;

		status = SpbDeviceInitialize(devContext->FxDevice, &spbConfig);
		if (!NT_SUCCESS(status))
		{
			GMBusI2CPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
				"SpbDeviceInitialize failed with status code 0x%x\n", status);

			return status;
		}

		WDF_OBJECT_ATTRIBUTES targetAttributes;
		WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&targetAttributes, PBC_TARGET);

		SpbControllerSetTargetAttributes(devContext->FxDevice, &targetAttributes);
	}

	return status;
}