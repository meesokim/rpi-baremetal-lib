#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "uart.h"
#include "atag.h"
#include "fb.h"
#include "console.h"
#include "block.h"
#include "vfs.h"
#include "memchunk.h"

#define UNUSED(x) (void)(x)

uint32_t _atags;
uint32_t _arm_m_type;

char rpi_boot_name[] = "rpi_boot";

static char *boot_cfg_names[] =
{
	"/boot/rpi_boot.cfg",
	"/boot/grub/grub.cfg",
	0
};

void atag_cb(struct atag *tag)
{
	switch(tag->hdr.tag)
	{
		case ATAG_CORE:
			puts("ATAG_CORE");
			if(tag->hdr.size == 5)
			{
				puts("flags");
				puthex(tag->u.core.flags);
				puts("");

				puts("pagesize");
				puthex(tag->u.core.pagesize);
				puts("");

				puts("rootdev");
				puthex(tag->u.core.rootdev);
				puts("");
			}
			break;

		case ATAG_MEM:
			puts("ATAG_MEM");
			
			puts("start");
			puthex(tag->u.mem.start);
			puts("");
			
			puts("size");
			puthex(tag->u.mem.size);
			puts("");

			{
				uint32_t start = tag->u.mem.start;
				uint32_t size = tag->u.mem.size;

				if(start < 0x100000)
					start = 0x100000;
				size -= 0x100000;
				chunk_register_free(start, size);
			}
			
			break;

		case ATAG_NONE:
			break;

		default:
			puts("Unknown ATAG");
			puthex(tag->hdr.tag);
			break;
	};

	puts("");
}

void console_test();
int sd_card_init(struct block_device **dev);
int read_mbr(struct block_device *, struct block_device ***, int *);

extern int (*stdout_putc)(int);
extern int (*stderr_putc)(int);
extern int (*stream_putc)(int, FILE*);
extern int console_putc(int);
extern int def_stream_putc(int, FILE*);

int cfg_parse(char *buf);

void kernel_main(uint32_t boot_dev, uint32_t arm_m_type, uint32_t atags)
{
	_atags = atags;
	_arm_m_type = arm_m_type;
	UNUSED(boot_dev);

	// First use the serial console
	stdout_putc = uart_putc;
	stderr_putc = uart_putc;
	stream_putc = def_stream_putc;	

	/* puts("Hello World!");
	puthex(0xdeadbeef);
	puts("");

	puts("Boot device:");
	puthex(boot_dev);
	puts("");

	puts("Machine type:");
	puthex(arm_m_type);
	puts("");

	puts("ATAGS:");
	puthex(atags);
	puts("");

	puts(""); */

	// Dump ATAGS
	parse_atags(atags, atag_cb);

	int result = fb_init();
	if(result == 0)
		puts("Successfully set up frame buffer");
	else
	{
		puts("Error setting up framebuffer:");
		puthex(result);
	}

	// Switch to the framebuffer for output
	stdout_putc = console_putc;

	printf("Welcome to Rpi bootloader\n");
	printf("ARM system type is %x\n", arm_m_type);

	struct block_device *sd_dev;

	if(sd_card_init(&sd_dev) == 0)
		read_mbr(sd_dev, (void*)0, (void*)0);

	// List devices
	printf("MAIN: device list: ");
	char **devs = vfs_get_device_list();
	while(*devs)
		printf("%s ", *devs++);
	printf("\n");

	// Look for a boot configuration file, starting with the default device,
	// then iterating through all devices
	
	FILE *f = (void*)0;
	
	// Default device
	char **fname = boot_cfg_names;
	while(*fname)
	{
		f = fopen(*fname, "r");
		if(f)
			break;
		fname++;
	}

	if(!f)
	{
		// Try other devices
		char **dev = vfs_get_device_list();
		while(*dev)
		{
			int dev_len = strlen(*dev);
			
			fname = boot_cfg_names;
			while(*fname)
			{
				int fname_len = strlen(*fname);
				char *new_str = (char *)malloc(dev_len + fname_len + 3);
				new_str[0] = 0;
				strcat(new_str, "(");
				strcat(new_str, *dev);
				strcat(new_str, ")");
				strcat(new_str, *fname);

				f = fopen(new_str, "r");
				free(new_str);

				if(f)
					break;
			}

			if(f)
				break;
		}
	}

	if(!f)
	{
		printf("No bootloader configuration file found\n");
	}
	else
	{	
		printf("Found bootloader configuration\n");
		char *buf = (char *)malloc(f->len);
		fread(buf, 1, f->len, f);
		fclose(f);
		cfg_parse(buf);
	}
}
