/*
 * NPLL - USB protocol definitions
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _NPLL_USB_PROTOCOL_H
#define _NPLL_USB_PROTOCOL_H

#include <npll/types.h>
#include <npll/utils.h>

#define USB_DIR_OUT                 0x00u
#define USB_DIR_IN                  0x80u
#define USB_TYPE_STANDARD           0x00u
#define USB_TYPE_CLASS              0x20u
#define USB_TYPE_VENDOR             0x40u
#define USB_RECIP_DEVICE            0x00u
#define USB_RECIP_INTERFACE         0x01u
#define USB_RECIP_ENDPOINT          0x02u
#define USB_RECIP_OTHER             0x03u

#define USB_REQ_GET_STATUS          0u
#define USB_REQ_CLEAR_FEATURE       1u
#define USB_REQ_SET_FEATURE         3u
#define USB_REQ_SET_ADDRESS         5u
#define USB_REQ_GET_DESCRIPTOR      6u
#define USB_REQ_SET_DESCRIPTOR      7u
#define USB_REQ_GET_CONFIGURATION   8u
#define USB_REQ_SET_CONFIGURATION   9u
#define USB_REQ_GET_INTERFACE       10u
#define USB_REQ_SET_INTERFACE       11u

#define USB_DT_DEVICE               1u
#define USB_DT_CONFIGURATION        2u
#define USB_DT_STRING               3u
#define USB_DT_INTERFACE            4u
#define USB_DT_ENDPOINT             5u
#define USB_DT_HUB                  0x29u
#define USB_DT_SS_ENDPOINT_COMP     0x30u
#define USB_DT_PIPE_USAGE           0x24u

#define USB_CLASS_HID               3u
#define USB_CLASS_MASS_STORAGE      8u
#define USB_CLASS_HUB               9u
#define USB_SUBCLASS_SCSI           6u
#define USB_PROTOCOL_HID_KEYBOARD   1u
#define USB_PROTOCOL_BOT            0x50u
#define USB_PROTOCOL_UAS            0x62u

#define USB_HID_REQ_SET_IDLE        0x0au
#define USB_HID_REQ_SET_PROTOCOL    0x0bu
#define USB_HID_PROTOCOL_BOOT       0u

#define USB_ENDPOINT_NUMBER_MASK    0x0fu
#define USB_ENDPOINT_DIR_MASK       0x80u
#define USB_ENDPOINT_XFER_MASK      0x03u
#define USB_ENDPOINT_XFER_CONTROL   0u
#define USB_ENDPOINT_XFER_ISOC      1u
#define USB_ENDPOINT_XFER_BULK      2u
#define USB_ENDPOINT_XFER_INT       3u

#define USB_FEATURE_ENDPOINT_HALT   0u
#define USB_FEATURE_PORT_POWER      8u
#define USB_FEATURE_PORT_RESET      4u
#define USB_FEATURE_C_PORT_CONNECTION 16u
#define USB_FEATURE_C_PORT_ENABLE   17u
#define USB_FEATURE_C_PORT_RESET    20u

#define USB_PORT_STAT_CONNECTION    BIT(0)
#define USB_PORT_STAT_ENABLE        BIT(1)
#define USB_PORT_STAT_LOW_SPEED     BIT(9)
#define USB_PORT_STAT_HIGH_SPEED    BIT(10)

struct usbSetupPacket {
	u8 requestType;
	u8 request;
	u16 value;
	u16 index;
	u16 length;
} __attribute__((packed));

struct usbDescriptorHeader {
	u8 length;
	u8 descriptorType;
} __attribute__((packed));

struct usbDeviceDescriptor {
	u8 length, descriptorType;
	u16 usbVersion;
	u8 deviceClass, deviceSubclass, deviceProtocol, maxPacketSize0;
	u16 vendorId, productId, deviceVersion;
	u8 manufacturer, product, serialNumber, numConfigurations;
} __attribute__((packed));

struct usbConfigurationDescriptor {
	u8 length, descriptorType;
	u16 totalLength;
	u8 numInterfaces, configurationValue, configuration, attributes, maxPower;
} __attribute__((packed));

struct usbInterfaceDescriptor {
	u8 length, descriptorType, interfaceNumber, alternateSetting;
	u8 numEndpoints, interfaceClass, interfaceSubclass, interfaceProtocol, interface;
} __attribute__((packed));

struct usbEndpointDescriptor {
	u8 length, descriptorType, endpointAddress, attributes;
	u16 maxPacketSize;
	u8 interval;
} __attribute__((packed));

/* Bulk-only transport */
#define USB_BOT_CBW_SIGNATURE 0x43425355u
#define USB_BOT_CSW_SIGNATURE 0x53425355u
#define USB_BOT_REQ_RESET     0xffu
#define USB_BOT_REQ_GET_MAX_LUN 0xfeu
struct usbBotCbw {
	u32 signature, tag, transferLength;
	u8 flags, lun, cdbLength, cdb[16];
} __attribute__((packed));
struct usbBotCsw {
	u32 signature, tag, residue;
	u8 status;
} __attribute__((packed));

/* USB Attached SCSI IU identifiers */
#define USB_UAS_IU_COMMAND       0x01u
#define USB_UAS_IU_SENSE         0x03u
#define USB_UAS_IU_RESPONSE      0x04u
#define USB_UAS_IU_TASK_MGMT     0x05u
#define USB_UAS_IU_READ_READY    0x06u
#define USB_UAS_IU_WRITE_READY   0x07u

/* SCSI commands used by both transports */
#define SCSI_TEST_UNIT_READY     0x00u
#define SCSI_REQUEST_SENSE       0x03u
#define SCSI_INQUIRY             0x12u
#define SCSI_READ_CAPACITY_10    0x25u
#define SCSI_READ_10             0x28u
#define SCSI_WRITE_10            0x2au
#define SCSI_SYNCHRONIZE_CACHE_10 0x35u
#define SCSI_SERVICE_ACTION_IN_16 0x9eu
#define SCSI_READ_16             0x88u
#define SCSI_WRITE_16            0x8au

#endif /* _NPLL_USB_PROTOCOL_H */
