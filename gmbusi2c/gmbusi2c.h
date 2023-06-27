#ifndef __CROS_EC_REGS_H__
#define __CROS_EC_REGS_H__

#define BIT(nr) (1UL << (nr))

typedef enum ADDRESS_MODE
{
    AddressMode7Bit,
    AddressMode10Bit
}
ADDRESS_MODE, * PADDRESS_MODE;

typedef struct PBC_TARGET_SETTINGS
{
    // TODO: Update this structure to include other
    //       target settings needed to configure the
    //       controller (i.e. connection speed, phase/
    //       polarity for SPI).

    ADDRESS_MODE                  AddressMode;
    USHORT                        Address;
    ULONG                         ConnectionSpeed;
}
PBC_TARGET_SETTINGS, * PPBC_TARGET_SETTINGS;

/////////////////////////////////////////////////
//
// Context definitions.
//
/////////////////////////////////////////////////

typedef struct PBC_TARGET   PBC_TARGET, * PPBC_TARGET;

struct PBC_TARGET
{
    // TODO: Update this structure with variables that 
    //       need to be stored in the target context.

    // Handle to the SPB target.
    SPBTARGET                      SpbTarget;

    // Target specific settings.
    PBC_TARGET_SETTINGS            Settings;
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PBC_TARGET, GetTargetContext);

#define I2C_SERIAL_BUS_TYPE 0x01
#define I2C_SERIAL_BUS_SPECIFIC_FLAG_10BIT_ADDRESS 0x0001

//Start IGD Registers

/* PCH */

#define PCH_DISPLAY_BASE	0xc0000u

#define GMBUS_MMIO_BASE PCH_DISPLAY_BASE

#define GMBUS_PIN_VGADDC	2
#define GMBUS_PIN_PANEL		3

// clock/port select
#define GMBUS0 GMBUS_MMIO_BASE + 0x5100
#define   GMBUS_AKSV_SELECT		(1 << 11)
#define   GMBUS_RATE_100KHZ		(0 << 8)
#define   GMBUS_RATE_50KHZ		(1 << 8)
#define   GMBUS_RATE_400KHZ		(2 << 8) /* reserved on Pineview */
#define   GMBUS_RATE_1MHZ		(3 << 8) /* reserved on Pineview */
#define   GMBUS_HOLD_EXT		(1 << 7) /* 300ns hold time, rsvd on Pineview */
#define   GMBUS_BYTE_CNT_OVERRIDE	(1 << 6)

// command/status
#define GMBUS1 GMBUS_MMIO_BASE + 0x5104
#define   GMBUS_SW_CLR_INT		(1 << 31)
#define   GMBUS_SW_RDY			(1 << 30)
#define   GMBUS_ENT			(1 << 29) /* enable timeout */
#define   GMBUS_CYCLE_NONE		(0 << 25)
#define   GMBUS_CYCLE_WAIT		(1 << 25)
#define   GMBUS_CYCLE_INDEX		(2 << 25)
#define   GMBUS_CYCLE_STOP		(4 << 25)
#define   GMBUS_BYTE_COUNT_SHIFT	16
#define   GMBUS_BYTE_COUNT_MAX		256U
#define   GEN9_GMBUS_BYTE_COUNT_MAX	511U
#define   GMBUS_SLAVE_INDEX_SHIFT	8
#define   GMBUS_SLAVE_ADDR_SHIFT	1
#define   GMBUS_SLAVE_READ		(1 << 0)
#define   GMBUS_SLAVE_WRITE		(0 << 0)

// status
#define GMBUS2 GMBUS_MMIO_BASE + 0x5108
#define   GMBUS_INUSE			(1 << 15)
#define   GMBUS_HW_WAIT_PHASE		(1 << 14)
#define   GMBUS_STALL_TIMEOUT		(1 << 13)
#define   GMBUS_INT			(1 << 12)
#define   GMBUS_HW_RDY			(1 << 11)
#define   GMBUS_SATOER			(1 << 10)
#define   GMBUS_ACTIVE			(1 << 9)

// data buffer bytes
#define GMBUS3 GMBUS_MMIO_BASE + 0x510c

// interrupt mask
#define GMBUS4 GMBUS_MMIO_BASE + 0x5110
#define   GMBUS_SLAVE_TIMEOUT_EN	(1 << 4)
#define   GMBUS_NAK_EN			(1 << 3)
#define   GMBUS_IDLE_EN			(1 << 2)
#define   GMBUS_HW_WAIT_EN		(1 << 1)
#define   GMBUS_HW_RDY_EN		(1 << 0)

#define GMBUS5 GMBUS_MMIO_BASE + 0x5120
#define   GMBUS_2BYTE_INDEX_EN		(1 << 31)

NTSTATUS
MdlChainGetByte(
    PMDL pMdlChain,
    size_t Length,
    size_t Index,
    UCHAR* pByte);

NTSTATUS
MdlChainSetByte(
    PMDL pMdlChain,
    size_t Length,
    size_t Index,
    UCHAR Byte
);

#endif /* __CROS_EC_REGS_H__ */