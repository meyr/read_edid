#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/i2c-dev.h>
//#include <linux/i2c.h>
//#include "smbus.h"
#include "edid.h"

#define I2CBUS 1
#define ADDRESS 0x50
#define FB_SYNC_HOR_HIGH_ACT    1       /* horizontal sync high active  */
#define FB_SYNC_VERT_HIGH_ACT   2       /* vertical sync high active    */
#define KHZ2PICOS(a) (1000000000UL/(a))

#define MISSING_FUNC_FMT   "Error: Adapter does not have %s capability\n"

static const unsigned char edid_v1_header[] = { 0x00, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0x00
};

struct fb_var_screeninfo {
	__u32 xres;			/* visible resolution		*/
	__u32 yres;
	__u32 xres_virtual;		/* virtual resolution		*/
	__u32 yres_virtual;
	__u32 xoffset;			/* offset from virtual to visible */
	__u32 yoffset;			/* resolution			*/
	__u32 height;			/* height of picture in mm    */
	__u32 width;			/* width of picture in mm     */
	/* Timing: All values in pixclocks, except pixclock (of course) */
	__u32 pixclock;			/* pixel clock in ps (pico seconds) */
	__u32 left_margin;		/* time from sync to picture	*/
	__u32 right_margin;		/* time from picture to sync	*/
	__u32 upper_margin;		/* time from sync to picture	*/
	__u32 lower_margin;
	__u32 hsync_len;		/* length of horizontal sync	*/
	__u32 vsync_len;		/* length of vertical sync	*/
	__u32 sync;			/* see FB_SYNC_*		*/
	__u32 vmode;			/* see FB_VMODE_*		*/
};

static void print_screeninfo(struct fb_var_screeninfo *var)
{
	if(var == NULL)
		return;

	printf("screen info:\n");
	printf("xres = %d , yres = %d\n",var->xres,var->yres);
	printf("pixclock = %d(pico seconds)\n",var->pixclock);
	printf("left_margin = %d , right_margin = %d\n",var->left_margin,var->right_margin);
	printf("upper_margin = %d , lower_margin = %d\n",var->upper_margin,var->lower_margin);
	printf("hsync_len = %d , vsync_len = %d\n",var->hsync_len,var->vsync_len);
}
static int edid_checksum(unsigned char *edid)
{
	unsigned char csum = 0, all_null = 0;
	int i, err = 0;

	for (i = 0; i < EDID_LENGTH; i++) {
		csum += edid[i];
		all_null |= edid[i];
	}

	if (csum == 0x00 && all_null) {
		/* checksum passed, everything's good */
		err = 1;
	}

	return err;
}

static int edid_check_header(unsigned char *edid)
{
	int i, err = 1;

	for (i = 0; i < 8; i++) {
		if (edid[i] != edid_v1_header[i])
			err = 0;
	}

	return err;
}

static int edid_is_timing_block(unsigned char *block)
{
	if ((block[0] != 0x00) || (block[1] != 0x00) ||
	    (block[2] != 0x00) || (block[4] != 0x00))
		return 1;
	else
		return 0;
}

int open_i2c_dev(int i2cbus, char *filename, size_t size, int quiet)
{
	int file;

	snprintf(filename, size, "/dev/i2c/%d", i2cbus);
	filename[size - 1] = '\0';
	file = open(filename, O_RDWR);

	if (file < 0 && (errno == ENOENT || errno == ENOTDIR)) {
		sprintf(filename, "/dev/i2c-%d", i2cbus);
		file = open(filename, O_RDWR);
	}

	if (file < 0 && !quiet) {
		if (errno == ENOENT) {
			fprintf(stderr, "Error: Could not open file "
				"`/dev/i2c-%d' or `/dev/i2c/%d': %s\n",
				i2cbus, i2cbus, strerror(ENOENT));
		} else {
			fprintf(stderr, "Error: Could not open file "
				"`%s': %s\n", filename, strerror(errno));
			if (errno == EACCES)
				fprintf(stderr, "Run as root?\n");
		}
	}

	return file;
}

int set_slave_addr(int file, int address, int force)
{
	/* With force, let the user read from/write to the registers
	   even when a driver is also running */
	if (ioctl(file, force ? I2C_SLAVE_FORCE : I2C_SLAVE, address) < 0) {
		fprintf(stderr,
			"Error: Could not set address to 0x%02x: %s\n",
			address, strerror(errno));
		return -errno;
	}

	return 0;
}

static int check_funcs(int file, int size, int pec)
{
	unsigned long funcs;

	/* check adapter functionality */
	if (ioctl(file, I2C_FUNCS, &funcs) < 0) {
		fprintf(stderr, "Error: Could not get the adapter "
			"functionality matrix: %s\n", strerror(errno));
		return -1;
	}

	switch(size) {
	case I2C_SMBUS_BYTE:
		if (!(funcs & I2C_FUNC_SMBUS_READ_BYTE)) {
			fprintf(stderr, MISSING_FUNC_FMT, "SMBus receive byte");
			return -1;
		}
		if (!(funcs & I2C_FUNC_SMBUS_WRITE_BYTE)) {
			fprintf(stderr, MISSING_FUNC_FMT, "SMBus send byte");
			return -1;
		}
		break;

	case I2C_SMBUS_BYTE_DATA:
		if (!(funcs & I2C_FUNC_SMBUS_READ_BYTE_DATA)) {
			fprintf(stderr, MISSING_FUNC_FMT, "SMBus read byte");
			return -1;
		}
		break;

	case I2C_SMBUS_WORD_DATA:
		if (!(funcs & I2C_FUNC_SMBUS_READ_WORD_DATA)) {
			fprintf(stderr, MISSING_FUNC_FMT, "SMBus read word");
			return -1;
		}
		break;

	case I2C_SMBUS_BLOCK_DATA:
		if (!(funcs & I2C_FUNC_SMBUS_READ_BLOCK_DATA)) {
			fprintf(stderr, MISSING_FUNC_FMT, "SMBus block read");
			return -1;
		}
		break;

	case I2C_SMBUS_I2C_BLOCK_DATA:
		if (!(funcs & I2C_FUNC_SMBUS_READ_I2C_BLOCK)) {
			fprintf(stderr, MISSING_FUNC_FMT, "I2C block read");
			return -1;
		}
		break;
	}

	if (pec
	 && !(funcs & (I2C_FUNC_SMBUS_PEC | I2C_FUNC_I2C))) {
		fprintf(stderr, "Warning: Adapter does "
			"not seem to support PEC\n");
	}

	return 0;
}

void print_edid(unsigned char *edid)
{
	int i;
	for(i = 0;i<EDID_LENGTH;i++)
		printf("%02x ",*(edid+i));

	printf("\n");
	
}

int fb_parse_edid(unsigned char *edid, struct fb_var_screeninfo *var)
{
	int i;
	unsigned char *block;

	if (edid == NULL || var == NULL)
		return 1;

	if (!(edid_checksum(edid)))
		return 1;

	if (!(edid_check_header(edid)))
		return 1;

	block = edid + DETAILED_TIMING_DESCRIPTIONS_START;

	for (i = 0; i < 4; i++, block += DETAILED_TIMING_DESCRIPTION_SIZE) {
		if (edid_is_timing_block(block)) {
			var->xres = var->xres_virtual = H_ACTIVE;
			var->yres = var->yres_virtual = V_ACTIVE;
			var->height = var->width = 0;
			var->right_margin = H_SYNC_OFFSET;
			var->left_margin = (H_ACTIVE + H_BLANKING) -
				(H_ACTIVE + H_SYNC_OFFSET + H_SYNC_WIDTH);
			var->upper_margin = V_BLANKING - V_SYNC_OFFSET -
				V_SYNC_WIDTH;
			var->lower_margin = V_SYNC_OFFSET;
			var->hsync_len = H_SYNC_WIDTH;
			var->vsync_len = V_SYNC_WIDTH;
			var->pixclock = PIXEL_CLOCK;
			var->pixclock /= 1000;
			var->pixclock = KHZ2PICOS(var->pixclock);

			if (HSYNC_POSITIVE)
				var->sync |= FB_SYNC_HOR_HIGH_ACT;
			if (VSYNC_POSITIVE)
				var->sync |= FB_SYNC_VERT_HIGH_ACT;
			return 0;
		}
	}
	return 1;
}

int main(int argc,char argv[])
{
	int res,file,i,j,pec = 0;
	char filename[20];
	unsigned char edid[EDID_LENGTH];
	struct fb_var_screeninfo *var;

	/* open file */
	file = open_i2c_dev(I2CBUS,filename,sizeof(filename),0);
	/* check function */
	check_funcs(file,I2C_FUNC_SMBUS_READ_BYTE_DATA,pec);
	/* set slave address */
	set_slave_addr(file,ADDRESS,0);
	/* point to 0x00 */
	res = i2c_smbus_write_byte(file,0x00);
	if(res != 0){
		fprintf(stderr,"Error: Write start address 0x00 failed , return code %d\n",res);
		goto end;
	}
	/* read out all data */
	for(i=0;i<EDID_LENGTH;i++){
		edid[i] = i2c_smbus_read_byte(file);
		if(res < 0)
			perror("read edid");
	}
	/* parse edid */
	var = (struct fb_var_screeninfo *)malloc(sizeof(struct fb_var_screeninfo));
	res = fb_parse_edid(edid,var);
	/* print screen info */
	print_screeninfo(var);
	/* print out edid value */
	//print_edid(edid);
end:
	close(file);
	return 1;
}
