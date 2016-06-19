#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <libusb-1.0/libusb.h>

#define VENDOR_ID			0x03EB // atmel mfr default
#define VENDOR_NAME		"ATMEL VID: "
#define PRODUCT_ID		0x2423 // atmel vendor default
#define PRODUCT_NAME	"ATMEL PID: "
#define MFG_STRING		"Mohammad El-Sabae"
//#define PROD_STRING		"libusb test"
#define PROD_STRING		"libusb"

#ifdef DEBUG
#include <assert.h>
#define DBG_FXN(s) printf("%s(): ", __FUNCTION__); \
	if(s != NULL) printf("%s\r\n", s);
#define DEBUG_FXN(s) DBG_FXN(s)
#endif

#ifndef DEBUG
#define assert(c)		;
#define DBG_FXN(s)	;
#define DEBUG_FXN(s) ;
#endif

#define BUF_LEN				1024
typedef struct
{
	uint8_t iface;
	uint8_t iface_alt_setting;
	uint8_t addr_ep_in;
	uint8_t addr_ep_out;
	uint8_t bInterval;
	uint8_t wMaxPacketSize;
} TRANSFER;

typedef struct
{
	libusb_device *dev;
	libusb_device_handle *handle;
	struct libusb_device_descriptor desc_dev;

	uint8_t port;

	TRANSFER bulk;
	TRANSFER iso;
	TRANSFER intrpt;
} VENDOR_DEV;

bool device_is_ours(libusb_device * const dev);
bool device_is_ours(libusb_device * const dev)
{
	if(dev == NULL)
	{
		DBG_FXN("pointer passed in was NULL.");
		return false;
	}

	struct libusb_device_descriptor desc;
	ssize_t retval;
	retval = libusb_get_device_descriptor(dev, &desc);
	if(retval != LIBUSB_SUCCESS
			|| desc.idVendor != VENDOR_ID
			|| desc.idProduct != PRODUCT_ID)
	{
		DBG_FXN("VID or PID did not match.");
		return false;
	}

	libusb_device_handle *handle;
	retval = libusb_open(dev, &handle);
	if(retval != LIBUSB_SUCCESS)
	{
		DBG_FXN("failed to open the device");
		return false;
	}

	unsigned char s_cmp[127];
	libusb_get_string_descriptor_ascii(handle, desc.iManufacturer,
			s_cmp, 127);
	if(strcmp((char *) s_cmp, MFG_STRING) != 0)
	{
		DBG_FXN("Manufacturer did not match.");
		return false;
	}

	libusb_get_string_descriptor_ascii(handle, desc.iProduct,
			s_cmp, 127);
	if(strcmp((char *) s_cmp, PROD_STRING) != 0)
	{
		DBG_FXN("Product name did not match.");
		return false;
	}

	libusb_close(handle);
	return true;
}

VENDOR_DEV* open_device_and_fill_properties(libusb_device * const dev,
		const bool kernel_detach);
VENDOR_DEV* open_device_and_fill_properties(libusb_device * const dev,
		const bool kernel_detach)
{
	VENDOR_DEV *v_dev;
	v_dev = (VENDOR_DEV *) calloc(1, sizeof(VENDOR_DEV));

	if(v_dev == NULL)
	{
		DEBUG_FXN("pointer passed in was not NULL.");
		return NULL;
	}

	v_dev->dev = dev;

	ssize_t retval;
	struct libusb_config_descriptor *conf_desc = NULL;
	const struct libusb_interface *_if = NULL;
	const struct libusb_interface_descriptor *_if_desc = NULL;
	const struct libusb_endpoint_descriptor *_ep_desc = NULL;
	TRANSFER *p_transfer = NULL;
	retval = libusb_get_device_descriptor(v_dev->dev, &v_dev->desc_dev);

	v_dev->port = libusb_get_port_number(v_dev->dev);

	for(uint8_t i = 0; i < v_dev->desc_dev.bNumConfigurations; i++)
	{
		retval = libusb_get_config_descriptor(v_dev->dev, i, &conf_desc);
		if(retval != LIBUSB_SUCCESS)
		{
			if(conf_desc != NULL)
			{
				DEBUG_FXN("failed to allocate a config descriptor.");
				DEBUG_FXN("returning NULL pointer.");
				libusb_free_config_descriptor(conf_desc);
				libusb_close(v_dev->handle);
				free(v_dev);
				return NULL;
			}
		}

		for(uint8_t j = 0; j < conf_desc->bNumInterfaces; j++)
		{
			_if = &conf_desc->interface[j];
			for(uint8_t k = 0; k < _if->num_altsetting; k++)
			{
				_if_desc = &_if->altsetting[k];
				for(uint8_t l = 0; l < _if_desc->bNumEndpoints; l++)
				{
					_ep_desc = &_if_desc->endpoint[l];
					switch(_ep_desc->bmAttributes & 0x03)
					{
						case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS:
							p_transfer = &v_dev->iso;
							break;
						case LIBUSB_TRANSFER_TYPE_BULK:
							p_transfer = &v_dev->bulk;
							break;
						case LIBUSB_TRANSFER_TYPE_INTERRUPT:
							p_transfer = &v_dev->intrpt;
							break;
						case LIBUSB_TRANSFER_TYPE_CONTROL:
						case LIBUSB_TRANSFER_TYPE_BULK_STREAM:
						default:
							continue;
					}

					p_transfer->iface = j;
					p_transfer->iface_alt_setting = k;
					p_transfer->bInterval = _ep_desc->bInterval;
					p_transfer->wMaxPacketSize = _ep_desc->wMaxPacketSize;

					if(_ep_desc->bEndpointAddress & 0x80)
					{
						p_transfer->addr_ep_in = _ep_desc->bEndpointAddress & 0x0F;
					}

					if(!(_ep_desc->bEndpointAddress & 0x80))
					{
						p_transfer->addr_ep_out = _ep_desc->bEndpointAddress & 0x0F;
					}
				}
			}
		}

		if(conf_desc != NULL)
		{
			libusb_free_config_descriptor(conf_desc);
		}
	}

	libusb_open(v_dev->dev, &v_dev->handle);
	if(kernel_detach)
	{
		libusb_set_auto_detach_kernel_driver(v_dev->handle, true);
		libusb_claim_interface(v_dev->handle, v_dev->bulk.iface);
		libusb_claim_interface(v_dev->handle, v_dev->iso.iface);
		libusb_claim_interface(v_dev->handle, v_dev->intrpt.iface);

		libusb_set_interface_alt_setting(v_dev->handle,
				v_dev->intrpt.iface, v_dev->intrpt.iface_alt_setting);
		libusb_set_interface_alt_setting(v_dev->handle,
				v_dev->iso.iface, v_dev->iso.iface_alt_setting);
		libusb_set_interface_alt_setting(v_dev->handle,
				v_dev->bulk.iface, v_dev->bulk.iface_alt_setting);
	}

	return v_dev;
}

void release_device(VENDOR_DEV **v_dev);
void release_device(VENDOR_DEV **v_dev)
{
	if(v_dev == NULL || *v_dev == NULL)
	{
		DBG_FXN("pointer passed in was null");
		return;
	}

	if((*v_dev)->handle == NULL)
	{
		DBG_FXN("v_dev->handle was not NULL");
		return;
	}

	if((*v_dev)->dev == NULL)
	{
		DBG_FXN("v_dev->dev was not NULL");
		return;
	}

	libusb_release_interface((*v_dev)->handle, (*v_dev)->bulk.iface);
	libusb_release_interface((*v_dev)->handle, (*v_dev)->iso.iface);
	libusb_release_interface((*v_dev)->handle, (*v_dev)->intrpt.iface);
	libusb_close((*v_dev)->handle);
	
	// free transfers and streams and other async API stuff here
	//

	free(*v_dev);
	*v_dev = NULL;
}

void print_device_properties(const VENDOR_DEV *const v_dev);
void print_device_properties(const VENDOR_DEV *const v_dev)
{
	if(v_dev == NULL)
	{
		DEBUG_FXN("pointer passed in was NULL.");
		return;
	}

	if(v_dev->dev == NULL)
	{
		DEBUG_FXN("v_dev->dev was NULL.");
		return;
	}

	if(v_dev->handle == NULL)
	{
		DEBUG_FXN("v_dev->handle was NULL.");
		return;
	}

	unsigned char dev_info[127];

	libusb_get_string_descriptor_ascii(v_dev->handle,
			v_dev->desc_dev.iManufacturer, dev_info, 127);
	printf("%s%s\r\n", "Manufacturer: ", dev_info);

	libusb_get_string_descriptor_ascii(v_dev->handle,
			v_dev->desc_dev.iProduct, dev_info, 127);
	printf("%s%s\r\n", "Product Name: ", dev_info);

	libusb_get_string_descriptor_ascii(v_dev->handle,
			v_dev->desc_dev.iSerialNumber, dev_info, 127);
	printf("%s%s\r\n", "Product Serial: ", dev_info);

	printf("%s%u\r\n", "bLength: ",
			v_dev->desc_dev.bLength);
	printf("%s%u\r\n", "bDescriptorType: ",
			v_dev->desc_dev.bDescriptorType);
	printf("%s%X\r\n", "bcdUSB: ",
			v_dev->desc_dev.bcdUSB);
	printf("%s%u\r\n", "bDeviceClass: ",
			v_dev->desc_dev.bDeviceClass);
	printf("%s%u\r\n", "bDeviceSubClass: ",
			v_dev->desc_dev.bDeviceSubClass);
	printf("%s%u\r\n", "bDeviceProtocol: ",
			v_dev->desc_dev.bDeviceProtocol);
	printf("%s%u\r\n", "bMaxPacketSize0: ",
			v_dev->desc_dev.bMaxPacketSize0);
	printf("%s%X\r\n", VENDOR_NAME "idVendor: ",
			v_dev->desc_dev.idVendor);
	printf("%s%X\r\n", PRODUCT_NAME "idProduct: ",
			v_dev->desc_dev.idProduct);
	printf("%s%X\r\n", "bcdDevice: ",
			v_dev->desc_dev.bcdDevice);
	printf("%s%u\r\n", "bNumConfigurations: ",
			v_dev->desc_dev.bNumConfigurations);

	int speed;
	speed = libusb_get_device_speed(v_dev->dev);

	switch(speed)
	{
		case LIBUSB_SPEED_LOW:
			printf("%s\r\n", "Low speed 1.5 MBit/s");
			break;
		case LIBUSB_SPEED_FULL:
			printf("%s\r\n", "Full speed 12 MBit/s");
			break;
		case LIBUSB_SPEED_HIGH:
			printf("%s\r\n", "High speed 480 MBit/s");
			break;
		case LIBUSB_SPEED_SUPER:
			printf("%s\r\n", "Super speed 5000 MBit/s");
			break;
		case LIBUSB_SPEED_UNKNOWN:
		default:
			printf("%s\r\n", "Unknown speed reported.");
			break;
	}

	printf("%s%u\r\n", "Device located on port: ", v_dev->port);
}


int main()
{
	bool hascap_api = false;
	//	bool hotplug = false;
	//	bool hid = false;
	bool kernel_detach = false;

	hascap_api = libusb_has_capability(LIBUSB_CAP_HAS_CAPABILITY);

	if(hascap_api)
	{
		//hotplug = libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG);
		//hid = libusb_has_capability(LIBUSB_CAP_HAS_HID_ACCESS);
		kernel_detach = libusb_has_capability(
				LIBUSB_CAP_SUPPORTS_DETACH_KERNEL_DRIVER);
	}

	libusb_context *ctx = NULL;
	libusb_init(&ctx);
	libusb_set_debug(ctx, LIBUSB_LOG_LEVEL_WARNING);

	ssize_t retval;
	libusb_device **list = NULL;
	VENDOR_DEV *due_usb = NULL;

	retval = libusb_get_device_list(ctx, &list);

	if(retval < LIBUSB_SUCCESS)
	{
		printf("%s\r\n", "failed to retrive devices.");
		libusb_free_device_list(list, true);
		return -1;
	}

	for(int8_t i = retval - 1; i >= 0; i--)
	{
		if(due_usb == NULL && device_is_ours(list[i]))
		{
			due_usb = open_device_and_fill_properties(list[i], kernel_detach);
		}

		libusb_unref_device(list[i]);
	}

	libusb_free_device_list(list, true);

	if(due_usb == NULL || due_usb->dev == NULL || due_usb->handle == NULL)
	{
		printf("%s\r\n", "failed to find a vendor device.");
		return -1;
	}

	print_device_properties(due_usb);

	int transferred;
	char buffer[BUF_LEN];
	uint64_t counter = 0;

	while(true)
	{
		snprintf(buffer, 100, "deadbeef: %" PRIu64, ++counter);

		libusb_bulk_transfer(due_usb->handle,
				LIBUSB_ENDPOINT_IN | due_usb->bulk.addr_ep_in,
				(unsigned char *) buffer, BUF_LEN, &transferred,
				due_usb->bulk.bInterval);

		if(memcmp(buffer, "deadbeef: ", 10) == 0)
		{
			printf("%s\r\n", "device hasn't responded");
		}

		if(memcmp(buffer, "deadbeef: ", 10) != 0)
		{
			printf("host received: %s\n", buffer);
		}
	}

	if(due_usb != NULL)
	{
		printf("%s\r\n", "got here");
		release_device(&due_usb);
	}

	libusb_exit(ctx);
	return 0;
}

