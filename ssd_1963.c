/*
 * SSD1963 Framebuffer
 *
 * The Solomon Systech SSD1963 chip drive TFT screen for UDAS20 application
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/io.h>
#include <asm-generic/io.h>
#include <asm-generic/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/tty.h>
#include <linux/err.h>

#include "test_image.h"
#include "test_image2.h"
#include "test_image3.h"
#include "fonts_ssd1963.h"
#include "clocktest-udas.h"
#include "colorbands-udas.h"
#include "gradient-udas.h"
#include "sharpness-udas.h"

//############### frame buffer requirements ##############
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h> /* copy_from_user, copy_to_user */
#include <linux/slab.h>

char * framebuffer = NULL;

static int fbinit(void);
static void fbexit(void);
//########################################################

#define DISP_COL_MIN	0
#define DISP_COL_MAX	479

#define DISP_ROW_MIN	0
#define DISP_ROW_MAX	271

#define DISP_RES_HOR	(DISP_COL_MAX + 1)
#define DISP_RES_VER	(DISP_ROW_MAX + 1)

#define DISP_PIX_TOT	(DISP_RES_HOR * DISP_RES_VER)

#define DISP_BLK		0x0000

#define DISP_RED_MIN	0x0800
#define DISP_RED_MAX	0xf800

#define DISP_GRN_MIN	0x0020
#define DISP_GRN_MAX	0x07e0

#define DISP_BLU_MIN	0x0001
#define DISP_BLU_MAX	0x001f

#define DISP_CYA_MIN	(DISP_BLU_MIN | DISP_GRN_MIN)
#define DISP_CYA_MAX	(DISP_BLU_MAX | DISP_GRN_MAX)

#define DISP_MAG_MIN	(DISP_BLU_MIN | DISP_RED_MIN)
#define DISP_MAG_MAX	(DISP_BLU_MAX | DISP_RED_MAX)

#define DISP_YEL_MIN	(DISP_GRN_MIN | DISP_RED_MIN)
#define DISP_YEL_MAX	(DISP_GRN_MAX | DISP_RED_MAX)

#define DISP_WHT_MIN	(DISP_BLU_MIN | DISP_GRN_MIN | DISP_RED_MIN)
#define DISP_WHT_MAX	(DISP_BLU_MAX | DISP_GRN_MAX | DISP_RED_MAX)

#define DISP_FONT_8		0
#define DISP_FONT_12	1
#define DISP_FONT_16	2
#define DISP_FONT_20	3
#define DISP_FONT_24	4

#define DISP_RENDER_RESULT_FULL	0
#define DISP_RENDER_RESULT_PART	1
#define DISP_RENDER_RESULT_NONE	2

#define LCD_PWR_EN      116 //on the schematic as BOOT_R

#define LCD_RS          112
#define LCD_WRn         113
#define LCD_RDn         114
#define LCD_D00         96
#define LCD_D01         97
#define LCD_D02         98
#define LCD_D03         99
#define LCD_D04         100
#define LCD_D05         101
#define LCD_D06         102
#define LCD_D07         103
#define LCD_D08         104
#define LCD_D09         105
#define LCD_D10         106
#define LCD_D11         107
#define LCD_D12         108
#define LCD_D13         109
#define LCD_D14         110
#define LCD_D15         111
#define LCD_CSn         115
#define LCD_RSTn_R      83
#define LCD_TE_R        85
#define LCD_DISP_ON_R   84

#define PWR_ENA()   (__gpio_set_value(LCD_PWR_EN, 0))
#define PWN_DIS()   (__gpio_set_value(LCD_PWR_EN, 1))

#define RST_ENA()	(__gpio_set_value(LCD_RSTn_R, 0))
#define RST_DIS()	(__gpio_set_value(LCD_RSTn_R, 1))

#define DISP_DIS()	(__gpio_set_value(LCD_DISP_ON_R, 0))
#define DISP_ENA()	(__gpio_set_value(LCD_DISP_ON_R, 1))

//#define BL_DIS()	(__gpio_set_value())
//#define BL_ENA()	(__gpio_set_value())
//#define BL_TOG()	(__gpio_set_value())

#define CS_ENA()	(__gpio_set_value(LCD_CSn, 0))
#define CS_DIS()	(__gpio_set_value(LCD_CSn, 1))

#define CMD_ENA()	(__gpio_set_value(LCD_RS, 0))
#define DATA_ENA()	(__gpio_set_value(LCD_RS, 1))

#define WR_ENA()	(__gpio_set_value(LCD_WRn, 0))
#define WR_DIS()	(__gpio_set_value(LCD_WRn, 1))

#define RD_ENA()	(__gpio_set_value(LCD_RDn, 0))
#define RD_DIS()	(__gpio_set_value(LCD_RDn, 1))

//module parameters
static int p_updates = 0;
module_param_named(updates, p_updates, int, 0664);
static int p_state = 0;
module_param_named(state, p_state, int, 0444);
static int p_img = 0;
module_param_named(image, p_img, int, 0664);
static int p_col = 0;
module_param_named(startColumn, p_col, int, 0664);
static int p_row = 0;
module_param_named(startRow, p_row, int, 0664);
static int p_width = 0;
module_param_named(width, p_width, int, 0664);
static int p_height = 0;
module_param_named(height, p_height, int, 0664);
static int p_arraySize = 0;
module_param_named(arraySize, p_arraySize, int, 0664);

// These static vars are initialized in DispInit()
static unsigned int	CurBackColor;
static unsigned int	CurForeColor;
static int		    CurFontType;
static sFONT	    CurFontStruct;

static struct delayed_work ssd1963_work;

#define SSD1963_PERIOD      (HZ / 10)
static void ssd1963_update_all(void);
static void ssd1963_update(struct work_struct *unused);

int DispFilledRectRender(int PosX, int PosY, int Width, int Height);
void DispBackColorSet(unsigned int Color);
void DispForeColorSet(unsigned int Color);
void DispFontSet(int Font);

static void ColSet(unsigned int StartCol, unsigned int EndCol);
static void RowSet(unsigned int StartRow, unsigned int EndRow);
static void CmdWrite(char val);
static void DataWrite(unsigned int val);

void DispInit(void)
{

	// PA0 - /RST signal, active low (asserted)
    gpio_direction_output(LCD_RSTn_R, 0);

	// PA1 - DISP signal, active high (deasserted)
    gpio_direction_output(LCD_DISP_ON_R, 0);

	// PA4 - BL_E signal, active high (deasserted)
    //gpio_direction_output(, 0);

	// PA5 - /CS signal, active low (deasserted)
    gpio_direction_output(LCD_CSn, 1);

	// PA6 - RS signal, 0:cmd, 1:data (data)
    gpio_direction_output(LCD_RS, 1);

	// PA7 - /WR signal, active low (deasserted)
    gpio_direction_output(LCD_WRn, 1);

	// PA8 - /RD signal, active low (deasserted)
    gpio_direction_output(LCD_RDn, 1);

	// PB[7:0] - DB[0:7] (low)
    gpio_direction_output(LCD_D00, 0);
    gpio_direction_output(LCD_D01, 0);
    gpio_direction_output(LCD_D02, 0);
    gpio_direction_output(LCD_D03, 0);
    gpio_direction_output(LCD_D04, 0);
    gpio_direction_output(LCD_D05, 0);
    gpio_direction_output(LCD_D06, 0);
    gpio_direction_output(LCD_D07, 0);

	// PC[7:0] - DB[15:8] (low)
    gpio_direction_output(LCD_D08, 0);
    gpio_direction_output(LCD_D09, 0);
    gpio_direction_output(LCD_D10, 0);
    gpio_direction_output(LCD_D11, 0);
    gpio_direction_output(LCD_D12, 0);
    gpio_direction_output(LCD_D13, 0);
    gpio_direction_output(LCD_D14, 0);
    gpio_direction_output(LCD_D15, 0);

    // LCD power enable
    gpio_direction_output(LCD_PWR_EN, 0);

	// PC8 - debug pin (low)
    //gpio_direction_output(, 0);

	// Assert the chip select (forever)
	CS_ENA();

	// Reset the LCD module
	RST_DIS();
	mdelay(50);
	RST_ENA();
	mdelay(30);
	RST_DIS();
	mdelay(100);

	// Set PLL: M=35, N=2 => (10MHz * 36) / 3 = 120MHz
	CmdWrite(0xE2);
	DataWrite(0x23);
	DataWrite(0x02);
	DataWrite(0x04);

	// Start the PLL, use reference clock as system clock
	CmdWrite(0xE0);
	DataWrite(0x01);
	mdelay(1);

	// PLL has locked, use the PLL as the system clock
	CmdWrite(0xE0);
	DataWrite(0x03);
	mdelay(5);

	// Soft reset, preserves registers 0xE0-0xE5
	CmdWrite(0x01);
	mdelay(5);

	// Set pixel clock: PCLK = (PLL * (FPR + 1)) / 0x100000
	// FPR = 0x1FFFF => PCLK = 120MHz / 8 = 15MHz
	CmdWrite(0xE6);
	DataWrite(0x01);
	DataWrite(0xFF);
	DataWrite(0xFF);

	// Set LCD mode
	CmdWrite(0xB0);
	// 24-bit LCD data, no FRC, no dithering, pixel data latch on falling edge,
	// HSYNC active low, VSYNC active low
	DataWrite(0x20);
	// TFT mode
	DataWrite(0x00);
	// Set panel horizontal size: HDP = 479 => 480 pixels
	DataWrite((479 >> 8) & 0xFF);
	DataWrite(479 & 0xFF);
	// Set panel vertical size: VDP = 271 => 272 pixels
	DataWrite((271 >> 8) & 0xFF);
	DataWrite(271 & 0xFF);
	// Set even/odd RGB sequence for serial TFT (not applicable)
	DataWrite(0x00);

	// Set HSYNC parameters (in pixels)
	CmdWrite(0xB4);
	// Set horizontal total period: HT = 525
	DataWrite((525 >> 8) & 0xFF);
	DataWrite( 525       & 0xFF);
	// Set HSYNC pulse width + horizontal back porch: HPS = 43
	DataWrite((43 >> 8) & 0xFF);
	DataWrite( 43       & 0xFF);
	// Set HSYNC pulse width: HPW = 41
	DataWrite(41);
	// Set horizontal front porch: LPS = 2
	DataWrite((2 >> 8) & 0xFF);
	DataWrite( 2       & 0xFF);
	// Set HSYNC pulse sub-pixel start position: LPSPP = 0
	DataWrite(0x00);

	// Set VSYNC parameters (in lines)
	CmdWrite(0xB6);
	// Set vertical total period: VT = 286
	DataWrite((286 >> 8) & 0xFF);
	DataWrite( 286       & 0xFF);
	// Set VSYNC pulse width + vertical back porch: VPS = 12
	DataWrite((12 >> 8) & 0xFF);
	DataWrite( 12       & 0xFF);
	// Set VSYNC pulse width: VPW = 10
	DataWrite(10);
	// Set vertical front porch: FPS = 2
	DataWrite((2 >> 8) & 0xFF);
	DataWrite( 2       & 0xFF);

	// Set address mode
	CmdWrite(0x36);
	// Page address order: top-to-bottom
	// Column address order: left-to-right
	// Page/Column order: normal
	// Line address order: refresh from top line to bottom line
	// RGB order: RGB
	// Line data latch order: left-to-right
	// Flip horizontal: flip
	// Flip vertical: flip
	DataWrite(0x03);	// 0x00 for normal horizontal & vertical (not flipped)

	// Set pixel data interface: 16-bit 565 format
	CmdWrite(0xF0);
	DataWrite(0x03);

	// Delay before use
	mdelay(5);

	// Optional - fill the entire display with black
	DispForeColorSet(DISP_BLK);
	DispFilledRectRender(DISP_COL_MIN, DISP_ROW_MIN, DISP_RES_HOR, DISP_RES_VER);

	// Optional - set up default colors and font
	DispBackColorSet(DISP_BLK);
	DispForeColorSet(DISP_WHT_MAX);
	DispFontSet(DISP_FONT_24);

	// Enable the display in hardware
	DISP_ENA();

	// Enable the display in software
//	CmdWrite(0x29);

	// Turn on the LED backlight
	//BL_ENA();
}

void DispOff(void)
{
	// Disable the display in software
	CmdWrite(0x28);
}

void DispOn(void)
{
	// Enable the display in software
	CmdWrite(0x29);
}

void DispBackColorSet(unsigned int Color)
{
	CurBackColor = Color;
}

unsigned int DispBackColorGet(void)
{
	return CurBackColor;
}

void DispForeColorSet(unsigned int Color)
{
	CurForeColor = Color;
}

unsigned int DispForeColorGet(void)
{
	return CurForeColor;
}

void DispFontSet(int Font)
{
	CurFontType = Font;
	switch (Font)
	{
	case DISP_FONT_8:
		CurFontStruct = Font8;
		break;
	case DISP_FONT_12:
		CurFontStruct = Font12;
		break;
	case DISP_FONT_16:
		CurFontStruct = Font16;
		break;
	case DISP_FONT_20:
		CurFontStruct = Font20;
		break;
	case DISP_FONT_24:
		CurFontStruct = Font24;
		break;
	default:
		CurFontType = DISP_FONT_20;
		CurFontStruct = Font20;
		break;
	}
}

int DispFontGet(void)
{
	return CurFontType;
}

#define GPIO_ORIG       1
#define GPIO_WRITEL     0
#define GPIO_POINTER    0

int DispRectCopy(int PosX, int PosY, int Width, int Height, const char * ByteArray)
{
	int		StartPosX;
	int		EndPosX;
	int		StartPosY;
	int		EndPosY;
	int		PixelCount;
	int		RetVal = DISP_RENDER_RESULT_FULL;

#if GPIO_WRITEL
    void __iomem *GPIO4_BADDR;
#elif GPIO_POINTER
    volatile u32 *GPIO4_BADDR = 0x3023000;
#endif
#if GPIO_WRITEL || GPIO_POINTER
    u32 reg, val;
#endif

	StartPosX = (PosX > DISP_COL_MIN) ? PosX : DISP_COL_MIN;
	EndPosX = ((PosX + Width - 1) < DISP_COL_MAX) ? (PosX + Width - 1) : DISP_COL_MAX;
	StartPosY = (PosY > DISP_ROW_MIN) ? PosY : DISP_ROW_MIN;
	EndPosY = ((PosY + Height - 1) < DISP_ROW_MAX) ? (PosY + Height - 1) : DISP_ROW_MAX;

	if ((EndPosX < DISP_COL_MIN) || (StartPosX > DISP_COL_MAX) ||
		(EndPosY < DISP_ROW_MIN) || (StartPosY > DISP_ROW_MAX))
	{
		return DISP_RENDER_RESULT_NONE;
	}

	PixelCount = (EndPosX - StartPosX + 1) * (EndPosY - StartPosY + 1);
	if (PixelCount < (Width * Height))
	{
		RetVal = DISP_RENDER_RESULT_PART;
	}

	// Copy the rectangle
	ColSet(StartPosX, EndPosX);
	RowSet(StartPosY, EndPosY);
	CmdWrite(0x2C);		// memory write

#if GPIO_WRITEL
    GPIO4_BADDR = ioremap(0x3023000, 1);
#endif
	while (PixelCount--)
	{
#if GPIO_ORIG
        //original method
        DataWrite((*(ByteArray + 1) << 8) | *ByteArray);	// byte array is little endian
#elif GPIO_WRITEL
        //writel method
        reg = readl(GPIO4_BADDR) & (~0x20000);
        writel(reg, GPIO4_BADDR);   // assert write

        reg &= 0xFFFF0000;
        reg |= val & 0xFFFF;
        writel(reg, GPIO4_BADDR);   //set new GPIO levels

        reg |= 0x20000;
        writel(reg, GPIO4_BADDR);   //deassert write to latch data

#elif GPIO_POINTER
        //pointers method
        val = (*(ByteArray + 1) << 8) | *ByteArray;
        reg = readl(GPIO4_BADDR) & (~0x20000);
        *GPIO4_BADDR = reg;         // assert write

        reg &= 0xFFFF0000;
        reg |= val & 0xFFFF;
        *GPIO4_BADDR = reg;         //set new GPIO levels
        
        
        reg |= 0x20000;
        *GPIO4_BADDR = reg;         //deassert write to latch data
#endif

		ByteArray += 2;
	}
#if GPIO_WRITEL
    iounmap(0x3023000);
#endif

	return RetVal;
}

int DispFilledRectRender(int PosX, int PosY, int Width, int Height)
{
	int		StartPosX;
	int		EndPosX;
	int		StartPosY;
	int		EndPosY;
	int		PixelCount;
	int		RetVal = DISP_RENDER_RESULT_FULL;

	StartPosX = (PosX > DISP_COL_MIN) ? PosX : DISP_COL_MIN;
	EndPosX = ((PosX + Width - 1) < DISP_COL_MAX) ? (PosX + Width - 1) : DISP_COL_MAX;
	StartPosY = (PosY > DISP_ROW_MIN) ? PosY : DISP_ROW_MIN;
	EndPosY = ((PosY + Height - 1) < DISP_ROW_MAX) ? (PosY + Height - 1) : DISP_ROW_MAX;

	if ((EndPosX < DISP_COL_MIN) || (StartPosX > DISP_COL_MAX) ||
		(EndPosY < DISP_ROW_MIN) || (StartPosY > DISP_ROW_MAX))
	{
		return DISP_RENDER_RESULT_NONE;
	}

	PixelCount = (EndPosX - StartPosX + 1) * (EndPosY - StartPosY + 1);
	if (PixelCount < (Width * Height))
	{
		RetVal = DISP_RENDER_RESULT_PART;
	}

	// Render the rectangle
	ColSet(StartPosX, EndPosX);
	RowSet(StartPosY, EndPosY);
	CmdWrite(0x2C);		// memory write
	while (PixelCount--)
	{
		DataWrite(CurForeColor);
	}

	return RetVal;
}


int DispCharRender(int PosX, int PosY, char Char)
{
	char            BitMask;
	int			    CurByte;
	unsigned int    CurCol;
	unsigned int    CurRow;
	int			    FontColStart;
	int			    FontColEnd;
	int			    FontRowStart;
	int			    FontRowEnd;
	int			    FontRowBytes;
	char            *FontBytePtr;

	// Determine the bounding rectangle for the complete character
	FontColStart = PosX;
	FontColEnd = PosX + CurFontStruct.Width - 1;
	FontRowStart = PosY;
	FontRowEnd = PosY + CurFontStruct.Height - 1;

	// Check if the character is completely outside of the display
	if ((FontColEnd < DISP_COL_MIN) || (FontColStart > DISP_COL_MAX) ||
		(FontRowEnd < DISP_ROW_MIN) || (FontRowStart > DISP_ROW_MAX))
	{
		return DISP_RENDER_RESULT_NONE;
	}

	// If the character is non-printable, convert it to 0x7f
	Char = (Char < 0x20) ? 0x7f : Char;
	Char = (Char > 0x7f) ? 0x7f : Char;

	// Offset the character value by 0x20 to index into the font table
	Char -= 0x20;

	// Constrain the portion of the character to be rendered within the display
	FontColStart = (FontColStart < DISP_COL_MIN) ? DISP_COL_MIN : FontColStart;
	FontColEnd   = (FontColEnd   > DISP_COL_MAX) ? DISP_COL_MAX : FontColEnd;
	FontRowStart = (FontRowStart < DISP_ROW_MIN) ? DISP_ROW_MIN : FontRowStart;
	FontRowEnd   = (FontRowEnd   > DISP_ROW_MAX) ? DISP_ROW_MAX : FontRowEnd;

	// Determine the number of bytes per row and the first byte in the table for the char
	FontRowBytes = (CurFontStruct.Width / 8) + 1;
	FontBytePtr = (char *) CurFontStruct.table +					// start of table
				  (Char * CurFontStruct.Height * FontRowBytes) +	// start of char
				  ((FontRowStart - PosY) * FontRowBytes);			// first displayed row

	ColSet(FontColStart, FontColEnd);
	RowSet(FontRowStart, FontRowEnd);

	CmdWrite(0x2C);		// memory write

	for (CurRow = FontRowStart; CurRow <= FontRowEnd; CurRow++)
	{
		CurCol = PosX;
		for (CurByte = 0; CurByte < FontRowBytes; CurByte++)
		{
			for (BitMask = 0x80; BitMask; BitMask >>= 1, CurCol++)
			{
				if ((CurCol >= FontColStart) && (CurCol <= FontColEnd))
				{
					DataWrite(((*FontBytePtr) & BitMask ? CurForeColor : CurBackColor));
				}
			}
			FontBytePtr++;
		}
	}

	// Determine if a partial character was rendered
	if (((FontColEnd - FontColStart + 1) < CurFontStruct.Width) ||
		((FontRowEnd - FontRowStart + 1) < CurFontStruct.Height))
	{
		return DISP_RENDER_RESULT_PART;
	}

	return DISP_RENDER_RESULT_FULL;
}

static void ColSet(unsigned int StartCol, unsigned int EndCol)
{
	CmdWrite(0x2A);
	DataWrite(StartCol >> 8);
	DataWrite(StartCol);
	DataWrite(EndCol >> 8);
	DataWrite(EndCol);
}


static void RowSet(unsigned int StartRow, unsigned int EndRow)
{
	CmdWrite(0x2B);
	DataWrite(StartRow >> 8);
	DataWrite(StartRow);
	DataWrite(EndRow >> 8);
	DataWrite(EndRow);
}

static void DataWriteLower(unsigned int val)
{
    unsigned int temp = val, i = 0;
    for(i = 0; i < 8; i++)
    {
        __gpio_set_value(LCD_D00 + i, temp & 0x01);
        temp = temp >> 1;
    }
}

static void DataWriteUpper(unsigned int val)
{
    unsigned int temp = val, i = 0;
    for(i = 0; i < 8; i++)
    {
        __gpio_set_value(LCD_D08 + i, temp & 0x01);
        temp = temp >> 1;
    }
}

static void CmdWrite(char val)
{
	CMD_ENA();									// assert command mode
	WR_ENA();									// assert write
	//LL_GPIO_WriteOutputPort(GPIOB, val);		// put Val[7:0] on DB[7:0]
    DataWriteLower(val);
	WR_DIS();									// deassert write to latch data
	DATA_ENA();									// assert data mode
}

#define DATA_ORIG       0
#define DATA_ARRAY      1
#define DATA_WRITEL     0
#define DATA_POINTER    0

#if (!GPIO_ORIG && !DATA_ORIG)
    #error Must choose either GPIO_ORIG or DATA_ORIG!
#endif

#if DATA_ARRAY
struct gpio_descs *gpio_os;
#endif

static void DataWrite(unsigned int val)
{
#if DATA_ORIG
    // Data mode is the default, no need to enable it
	WR_ENA();       // assert write
    //GPIOC->ODR = val >> 8;						// put Val[15:8] on DB[15:8]
    DataWriteUpper(val >> 8);
    //GPIOB->ODR = val;							// put Val[7:0] on DB[7:0]
    DataWriteLower(val);
    // Stay in data mode (default)
    WR_DIS();	
#elif DATA_ARRAY
    unsigned int os[16];
    unsigned int i;
    
    WR_ENA();       // assert write
    if(gpio_os)
    {
        for(i=0;i<16;i++)
        {
            os[i] = (val >> i) & 0x1;
        }
        gpiod_set_array_value(16, gpio_os->desc, os);
    }
    WR_DIS();	
#elif DATA_WRITEL
    void __iomem *GPIO4_BADDR = 0x3023000;
    GPIO4_BADDR = ioremap(0x3023000, 1);
    u32 reg = readl(GPIO4_BADDR) & (~0x20000);

    writel(reg, GPIO4_BADDR);    // assert write
    reg &= 0xFFFF0000;
    reg |= val & 0xFFFF;
    writel(reg, GPIO4_BADDR);
    reg |= 0x20000;
    writel(reg, GPIO4_BADDR);    // deassert write to latch data
    
    iounmap(0x3023000);
#elif DATA_POINTER
    volatile u32 *GPIO4_BADDR = 0x3023000;
    //GPIO4_BADDR = ioremap(0x3023000, 1);
    u32 reg = *GPIO4_BADDR & (~0x20000);

    *GPIO4_BADDR = reg;     // assert write
    reg &= 0xFFFF0000;
    reg |= val & 0xFFFF;
    *GPIO4_BADDR = reg;     //set GPIOs
    reg |= 0x20000;
    *GPIO4_BADDR = reg;     // deassert write to latch data
    //iounmap(0x3023000);
#endif
}

//#####################################################################################################

struct ssd1963 {
	struct device *dev;
	//volatile unsigned short *ctrl_io;
	//volatile unsigned short *data_io;
	//struct fb_info *info;
	//unsigned int pages_count;
	//struct ssd1963_page *pages;
	//nsigned long pseudo_palette[25];
};



static void ssd1963_update_all()
{
	p_updates++;    

    schedule_delayed_work(&ssd1963_work, SSD1963_PERIOD);
}

static void ssd1963_update(struct work_struct *unused)
{
    p_updates++;

    if(p_img == 2)
    {
        //pull image data from frame buffer
        if(framebuffer)
        {
            DispRectCopy(p_col, p_row, p_width, p_height, framebuffer);
        }
        else
            printk(KERN_ALERT "framebuffer addess is null!\n");
    }
    else if(p_img == 3)
    {
        //display original sample image
        DispRectCopy(0, 0, DISP_RES_HOR, DISP_RES_VER, ImageArray);
    }
    else if(p_img == 4)
    {
        //display 2nd sample image
        DispRectCopy(0, 0, DISP_RES_HOR, DISP_RES_VER, Image2Array);
    }
    else if(p_img == 5)
    {
        //display clock test image
        DispRectCopy(0, 0, DISP_RES_HOR, DISP_RES_VER, ClocktestImage);
    }
    else if(p_img == 6)
    {
        //display color band test image
        DispRectCopy(0, 0, DISP_RES_HOR, DISP_RES_VER, ColorbandsImage);
    }
    else if(p_img == 7)
    {
        //display gradient test image
        DispRectCopy(0, 0, DISP_RES_HOR, DISP_RES_VER, GradientImage);
    }
    else if(p_img == 8)
    {
        //display sharpness test image
        DispRectCopy(0, 0, DISP_RES_HOR, DISP_RES_VER, SharpnessImage);
    }
    else if(p_img > 0)
    {
        //display splash image
        DispRectCopy(0, 0, DISP_RES_HOR, DISP_RES_VER, Image3Array);
    }
    p_img = 0;

    schedule_delayed_work(&ssd1963_work, SSD1963_PERIOD);

    return;
}

static int __init ssd1963_probe(struct platform_device *dev)
{
    int ret = 0;
    struct ssd1963 *item;

    printk(KERN_ALERT "COLOR LCD driver probing (printk)\n");
	dev_err(&dev->dev, "%s\n", __func__);

	item = devm_kzalloc(&dev->dev,
				sizeof(struct ssd1963),
				GFP_KERNEL);
	if (!item) {
		dev_err(&dev->dev,
			"%s: unable to kzalloc for ssd1963\n", __func__);
		ret = -ENOMEM;
		goto out;
	}

    //item->dev = &dev->dev;
	//dev_set_drvdata(&dev->dev, item);
	platform_set_drvdata(dev, item);

//###############################################################3
    /* Done in UBoot    
    // Initialize hardware
	DispInit();
    // Copy the CliniComp test image, enable the display
	DispRectCopy(0, 0, DISP_RES_HOR, DISP_RES_VER, Image3Array);
	DispOn(); */

#if DATA_ARRAY
    gpio_os = gpiod_get_array(&dev->dev, "lcd-pin-data", GPIOD_OUT_LOW);
    if(IS_ERR(gpio_os))
        dev_err(&dev->dev, "Unable to get LCD data pin array! %d\n", PTR_ERR(gpio_os));
    else
        printk(KERN_ALERT "Got LCD data pin array\n");
#endif

    INIT_DELAYED_WORK(&ssd1963_work, ssd1963_update);

    // Kick off main loop
	ssd1963_update_all();

    printk(KERN_ALERT "COLOR LCD driver probed\n");

    return ret;

//#################################################################3

/*out_item:
	kfree(item);*/
out:
    printk(KERN_ALERT "COLOR LCD driver failed :(\n");
	return ret;
}


static int ssd1963_remove(struct platform_device *device)
{
	struct ssd1963 *item = platform_get_drvdata(device);
	if (item) {
		kfree(item);
	}
	return 0;
}

static const struct of_device_id ssd1963_ids[] = {
	{ .compatible = "solomon,ssd1963", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ssd1963_ids); //this line necessary to enable auto-loading the module if found in device tree

static struct platform_driver ssd1963_driver = {
	.probe = ssd1963_probe,
	.remove = ssd1963_remove,
	.driver = {
		   .name = "ssd1963fb",
		   .of_match_table	= ssd1963_ids,
		   .owner = THIS_MODULE,
		   },
};

static int __init ssd1963_init(void)
{
	int ret = 0;
	
	printk(KERN_ALERT "COLOR LCD driver init (printk)\n");

	pr_debug("%s\n", __func__);

	ret = platform_driver_register(&ssd1963_driver);

	if (ret) {
		pr_err("%s: unable to platform_driver_register\n", __func__);
	}

    fbinit(); //frame buffer init

	return ret;
}
module_init(ssd1963_init);

static void __exit ssd1963_exit(void)
{
    cancel_delayed_work(&ssd1963_work);
    flush_scheduled_work();

    fbexit(); //frame buffer exit

	platform_driver_unregister(&ssd1963_driver);    
}
module_exit(ssd1963_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Nick Bourdon, nick.bourdon@claritydesign.com");
MODULE_DESCRIPTION("SSD1963 Driver for OSD043T3491-19");

//############################ frame buffer ###############################
// Adapted from https://stackoverflow.com/questions/10760479/how-to-mmap-a-linux-kernel-buffer-to-user-space
//#########################################################################

/* https://cirosantilli.com/linux-kernel-module-cheat#mmap */
#ifndef PAGE_SIZE
#define PAGE_SIZE       4096
#endif
#define BUFFER_PAGES    64
#define PAGES_ORDER     6   //2^6 = 64 pages
#define BUFFER_SIZE     (PAGE_SIZE * BUFFER_PAGES)

static const char *filename = "udas_fb";

struct mmap_info {
    char *data;
};

struct mmap_info info_global;

/* After unmap. */
static void vm_close(struct vm_area_struct *vma)
{
    //pr_info("ssd1963: vm_close\n");
}

/* First page access. */
static vm_fault_t vm_fault(struct vm_fault *vmf)
{
    struct page *page;
    struct mmap_info *info;

    if(vmf == NULL)
    {
        pr_err("ssd1963, vm_fault: vmf is null!\n");
        return -1;
    }
    if(vmf->vma == NULL)
    {
        pr_err("ssd1963, vm_fault: vmf->vma is null!\n");
        return -1;        
    }
    //pr_info("ssd1963: vm_fault at 0x%lx (offset: 0x%lx)\n", vmf->address, vmf->pgoff);
    info = (struct mmap_info *)vmf->vma->vm_private_data;
    if (info)
    {
        if (info->data) {
            page = virt_to_page(info->data + (PAGE_SIZE * vmf->pgoff));
            //pr_info("count = %d\n", page_count(page));
            get_page(page);
            vmf->page = page;
        }
        else
        {
            pr_err("ssd1963, vm_fault: info->data is null!\n");
            return -1;
        }
    }
    else
    {
        pr_err("ssd1963, vm_fault: info is null!\n");
        return -1;
    }

    return 0;
}

/* Aftr mmap. TODO vs mmap, when can this happen at a different time than mmap? */
static void vm_open(struct vm_area_struct *vma)
{
    //pr_info("ssd1963: vm_open\n");
}

static struct vm_operations_struct vm_ops =
{
    .close = vm_close,
    .fault = vm_fault,
    .open = vm_open,
};

static int mmap(struct file *filp, struct vm_area_struct *vma)
{
    //pr_info("ssd1963: mmap\n");
    vma->vm_ops = &vm_ops;
    vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
    vma->vm_private_data = filp->private_data;
    vm_open(vma);
    return 0;
}

static int open(struct inode *inode, struct file *filp)
{
    struct mmap_info *info = &info_global;

    //pr_info("ssd1963: open\n");
    //info = kmalloc(sizeof(struct mmap_info), GFP_KERNEL);
    //pr_info("virt_to_phys = 0x%lx -> 0x%llx\n", (unsigned long)info,(unsigned long long)virt_to_phys((void *)info));
    //info->data = (char *)get_zeroed_page(GFP_KERNEL);
    info->data = (char *)__get_free_pages(GFP_KERNEL, PAGES_ORDER);
    if(info->data == NULL)
    {
        pr_err("Unable to allocate framebuffer!\n");
        return -ENOMEM;
    }
    //pr_info("(0x%lx)\n", (unsigned long)info->data);
    memset(info->data, 0, PAGE_SIZE << PAGES_ORDER);    //zero the buffer
    filp->private_data = info;
    framebuffer = info->data;
    return 0;
}

static ssize_t read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
    struct mmap_info *info;
    int ret;

    //pr_info("ssd1963: read\n");
    info = filp->private_data;
    ret = min(len, (size_t)BUFFER_SIZE);
    if (copy_to_user(buf, info->data, ret)) {
        ret = -EFAULT;
    }
    return ret;
}

static ssize_t write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
    struct mmap_info *info;

    //pr_info("ssd1963: write %d bytes\n", len);
    info = filp->private_data;
    if (copy_from_user(info->data, buf, min(len, (size_t)BUFFER_SIZE))) {
        return -EFAULT;
    } else {
        return len;
    }
}

static int release(struct inode *inode, struct file *filp)
{
    struct mmap_info *info = filp->private_data;
    struct page *page;

	//pr_info("release (0x%lx)\n", (unsigned long)info->data);
    framebuffer = NULL;
	//info = filp->private_data;
	//free_page((unsigned long)info->data);
    page = virt_to_page(info->data);
    put_page(page);
    free_pages((unsigned long)info->data, PAGES_ORDER);
	//kfree(info);
	filp->private_data = NULL;
    
    return 0;
}

static const struct file_operations fops = {
    .mmap = mmap,
    .open = open,
    .release = release,
    .read = read,
    .write = write,
};

static int fbinit(void)
{
    proc_create(filename, 0, NULL, &fops);
    return 0;
}

static void fbexit(void)
{
    remove_proc_entry(filename, NULL);
}

