#include "driver.h"

static ULONG GMBusI2CDebugLevel = 100;
static ULONG GMBusI2CDebugCatagories = DBG_INIT || DBG_PNP || DBG_IOCTL;

NTSTATUS gmbus_wait_idle(PGMBUSI2C_CONTEXT pDevice) {
	UINT32 irq_enable = GMBUS_IDLE_EN;
	UINT32 gmbus2;

	write32(pDevice, GMBUS4, irq_enable);

	KeStallExecutionProcessor(2);

	LARGE_INTEGER StartTime;
	KeQuerySystemTimePrecise(&StartTime);

	while (true) {
		LARGE_INTEGER CurrentTime;
		KeQuerySystemTimePrecise(&CurrentTime);

		if (((CurrentTime.QuadPart - StartTime.QuadPart) / (10 * 1000)) >= 10) { //wait for up to 10 milliseconds
			GMBusI2CPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"gmbus_wait_idle timed out\n");
			return STATUS_IO_TIMEOUT;
		}

		gmbus2 = read32(pDevice, GMBUS2) & GMBUS_ACTIVE;

		if (!gmbus2)
			break;
	}

	write32(pDevice, GMBUS4, 0);
	return STATUS_SUCCESS;
}

NTSTATUS gmbus_wait(PGMBUSI2C_CONTEXT pDevice, UINT32 status, UINT32 irq_en)
{
	write32(pDevice, GMBUS4, irq_en);

	KeStallExecutionProcessor(2);

	LARGE_INTEGER StartTime;
	KeQuerySystemTimePrecise(&StartTime);

	UINT32 gmbus2;
	while (true) {
		LARGE_INTEGER CurrentTime;
		KeQuerySystemTimePrecise(&CurrentTime);

		if (((CurrentTime.QuadPart - StartTime.QuadPart) / (10 * 1000)) >= 50) { //wait for up to 50 milliseconds
			GMBusI2CPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"gmbus_wait timed out\n");
			return STATUS_IO_TIMEOUT;
		}

		gmbus2 = read32(pDevice, GMBUS2);
		if (gmbus2 & status)
			break;
	}

	write32(pDevice, GMBUS4, 0);

	return STATUS_SUCCESS;
}

NTSTATUS gmbus_xfer_read_chunk(PGMBUSI2C_CONTEXT pDevice, unsigned short addr, PMDL mdlChain,
	const unsigned int len, UINT32 gmbus1_index)
{
	write32(pDevice, GMBUS1, gmbus1_index | GMBUS_CYCLE_WAIT | (len << GMBUS_BYTE_COUNT_SHIFT) |
		(addr << GMBUS_SLAVE_ADDR_SHIFT) | GMBUS_SLAVE_READ | GMBUS_SW_RDY);

	unsigned int i = 0;
	while (i < len) {
		NTSTATUS status;
		UINT32 val, loop = 0;

		status = gmbus_wait(pDevice, GMBUS_HW_RDY, GMBUS_HW_RDY_EN);
		if (!NT_SUCCESS(status))
			return status;

		val = read32(pDevice, GMBUS3);

		do {
			if (i < len) {
				status = MdlChainSetByte(mdlChain, len, i, val & 0xff);
				if (!NT_SUCCESS(status))
					return status;
			}

			val >>= 8;
		} while (++i && ++loop < 4);
	}

	return STATUS_SUCCESS;
}

NTSTATUS gmbus_xfer_write_chunk(PGMBUSI2C_CONTEXT pDevice, unsigned short addr, PMDL mdlChain,
	const unsigned int len, UINT32 gmbus1_index) {
	unsigned int chunk_size = len;
	unsigned int i = 0;

	UINT32 val, loop;
	val = loop = 0;
	while (i < len && loop < 4) {
		UCHAR byte = 0;
		NTSTATUS status = MdlChainGetByte(mdlChain, len, i, &byte);
		if (!NT_SUCCESS(status))
			return status;

		val |= byte << (8 * loop++);
		i += 1;
	}

	write32(pDevice, GMBUS3, val);

	write32(pDevice, GMBUS1, gmbus1_index | GMBUS_CYCLE_WAIT | (chunk_size << GMBUS_BYTE_COUNT_SHIFT) | (addr << GMBUS_SLAVE_ADDR_SHIFT) | GMBUS_SLAVE_WRITE | GMBUS_SW_RDY);
	while (i < len) {
		NTSTATUS status;

		val = loop = 0;
		do {
			UCHAR byte = 0;
			if (i < len) {
				status = MdlChainGetByte(mdlChain, len, i, &byte);
				if (!NT_SUCCESS(status))
					return status;
			}

			val |= byte << (8 * loop);
		} while (++i && ++loop < 4);

		write32(pDevice, GMBUS3, val);

		status = gmbus_wait(pDevice, GMBUS_HW_RDY, GMBUS_HW_RDY_EN);
		if (!NT_SUCCESS(status))
			return status;
	}

	return STATUS_SUCCESS;
}

NTSTATUS i2c_xfer_single(
	PGMBUSI2C_CONTEXT pDevice,
	PPBC_TARGET pTarget,
	SPB_TRANSFER_DESCRIPTOR descriptor,
	PMDL mdlChain
) {
	NTSTATUS status = STATUS_SUCCESS;

	UINT32 gmbus0_source = 0;
	UINT32 reg0 = pDevice->busNumber | GMBUS_RATE_100KHZ;

	if (gmbus_wait_idle(pDevice)) { //Wait for idle in case IGD is using it
		GMBusI2CPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"gmbus timed out waiting for idle\n");
		status = STATUS_IO_TIMEOUT;
	}

	write32(pDevice, GMBUS0, gmbus0_source | reg0);

	if (descriptor.Direction == SpbTransferDirectionFromDevice) { //xfer read
		status = gmbus_xfer_read_chunk(pDevice,
			pTarget->Settings.Address,
			mdlChain,
			descriptor.TransferLength,
			0);
		if (!NT_SUCCESS(status)) {
			GMBusI2CPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"gmbus_xfer_read_chunk returned %x\n", status);
			goto clear_err;
		}
	}
	else {
		status = gmbus_xfer_write_chunk(pDevice,
			pTarget->Settings.Address,
			mdlChain,
			descriptor.TransferLength,
			0);
		if (!NT_SUCCESS(status)) {
			GMBusI2CPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"gmbus_xfer_write_chunk returned %x\n", status);
			goto clear_err;
		}
	}

	status = gmbus_wait(pDevice, GMBUS_HW_WAIT_PHASE, GMBUS_HW_WAIT_EN);
	if (!NT_SUCCESS(status))
		return status;

	//Generate a stop condition
	write32(pDevice, GMBUS1, GMBUS_CYCLE_STOP | GMBUS_SW_RDY);

	if (gmbus_wait_idle(pDevice)) {
		GMBusI2CPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"gmbus timed out waiting for idle\n");
		status = STATUS_IO_TIMEOUT;
	}

	return status;

clear_err:
	//Wait for bus to idle before clearing

	if (gmbus_wait_idle(pDevice)) {
		GMBusI2CPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"gmbus timed out waiting for idle\n");
		status = STATUS_IO_TIMEOUT;
	}

	//Toggle software clear itnerrupt but

	write32(pDevice, GMBUS1, GMBUS_SW_CLR_INT);
	write32(pDevice, GMBUS1, 0);
	write32(pDevice, GMBUS0, 0);

	return status;
}

NTSTATUS i2c_xfer(PGMBUSI2C_CONTEXT pDevice,
	_In_ SPBTARGET SpbTarget,
	_In_ SPBREQUEST SpbRequest,
	_In_ ULONG TransferCount) {
	NTSTATUS status = STATUS_SUCCESS;
	PPBC_TARGET pTarget = GetTargetContext(SpbTarget);
	
	status = (pDevice->GMBusI2CLockBus)(pDevice->GMBusI2CBusContext);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	UINT32 transferredLength = 0;

	SPB_TRANSFER_DESCRIPTOR descriptor;
	for (int i = 0; i < TransferCount; i++) {
		PMDL mdlChain;

		SPB_TRANSFER_DESCRIPTOR_INIT(&descriptor);
		SpbRequestGetTransferParameters(SpbRequest,
			i,
			&descriptor,
			&mdlChain);

		status = i2c_xfer_single(pDevice, pTarget, descriptor, mdlChain);
		if (!NT_SUCCESS(status)) {
			(pDevice->GMBusI2CUnlockBus)(pDevice->GMBusI2CBusContext);
			return status;
		}

		transferredLength += descriptor.TransferLength;
		WdfRequestSetInformation(SpbRequest, transferredLength);
	}

	(pDevice->GMBusI2CUnlockBus)(pDevice->GMBusI2CBusContext);

	return status;
}